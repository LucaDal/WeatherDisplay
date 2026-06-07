#include <Arduino.h>

#include <ArduinoJson.h>
#include <SensirionI2cSht4x.h>
#include <Wire.h>

#include "CommonDebug.h"
#include "DeviceSetupManager.h"
#include "LiteWiFiManager.h"
#include "MQTTManager.h"
#include "MyDeviceProperties.h"
#include "SparkFun_SCD30_Arduino_Library.h" //Click here to get the library: http://librarymanager/All#SparkFun_SCD30
#include "TimeSyncManager.h"
#include "WeatherAPI.h"
#include "WeatherDisplayUi.h"
#include <SimpleOTA.h>
#include <ctime>
#include <memory>

const uint16_t MQTT_PORT = 8883;
const uint8_t FORECAST_SLOTS = 4;
#define LOG(msg) DBG_LOG("", msg)
#define LOGF(fmt, ...) DBG_LOGF("", fmt, ##__VA_ARGS__)
const unsigned long REFRESH_TIME_AQI_MS = 60UL * 60UL * 1000UL;
// Sensor values are sampled more often than they are drawn. Temperature and
// humidity use the last valid sample, while CO2 is averaged over the minute.
const unsigned long SENSOR_SAMPLE_INTERVAL_MS = 10UL * 1000UL;
const unsigned long SENSOR_DISPLAY_INTERVAL_MS = 60UL * 1000UL;
const unsigned long SCREEN_TIME_INTERVAL_MS = 60UL * 1000UL;
const unsigned long FORECAST_REFRESH_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL;
const unsigned long FORECAST_RETRY_INTERVAL_MS = 10UL * 60UL * 1000UL;
// The e-paper panel is powered off shortly after each refresh. The framebuffer
// content remains visible and the next partial refresh wakes the panel.
const unsigned long DISPLAY_POWER_OFF_IDLE_MS = 10UL * 1000UL;
const time_t EXT_SENSOR_TIMEOUT_SEC = 20UL * 60UL;
bool debug = false;

SimpleOTA simpleOTA;
MyDeviceProperties devProps;
LiteWiFiManager wifiProvision;
DeviceSetupManager setupMgr;
TimeSyncManager timeMgr;

SCD30 airSensor;
SensirionI2cSht4x sht;
// in order: CS, DC, RES, BUSY
WeatherPanel display(GxEPD2_420_GDEY042T81(SS, 3, 2, 1)); // 400x300, SSD1683
WeatherDisplayUi ui(display);
// API key, latitude and longitude
std::unique_ptr<WeatherAPI> weather;

unsigned long nowMs = 0;
time_t nowEpoch = 0;
unsigned long lastAirUpdateMs = 0;
unsigned long lastSensorSampleMs = 0;
unsigned long lastSensorDisplayMs = 0;
unsigned long lastScreenTimeUpdateMs = 0;
double lastSensorTemp = 0.0;
double lastSensorHumid = 0.0;
uint32_t sensorCo2Sum = 0;
uint16_t sensorSamples = 0;
uint16_t lastDisplayedCo2 = 0;
bool hasDisplayedCo2 = false;
Forecast *forecasts = nullptr;
size_t forecastsCount = 0;

DisplayPoint sensorPoint, forecastPoint, pollutionPoint, extTermIgroPoint,
    co2ValuesPoint, timePoint;

uint8_t currForecastIdx = 0;

MQTTManager mqttClient;

struct TermoIgro {
    double temp = 0;
    uint8_t humid = 0;
    time_t dt = 0;
} extTermIgro;
bool extAcquired;

void updateLoopClock() {
    nowMs = millis();
    nowEpoch = time(nullptr);
}

bool intervalElapsed(unsigned long &lastUpdateMs, unsigned long intervalMs) {
    if (lastUpdateMs == 0 || nowMs - lastUpdateMs >= intervalMs) {
        lastUpdateMs = nowMs;
        return true;
    }
    return false;
}

void resetSensorSamples() {
    sensorCo2Sum = 0;
    sensorSamples = 0;
}

void connectToMQTT() {
    const char *mqtt_topic = devProps.Get("topic");
    String client_id = setupMgr.deviceId();
    LOGF("Connecting to MQTT Broker with client cert as %s...\n",
         client_id.c_str());
    LOGF("topic: [%s]\n", mqtt_topic);
    if (mqttClient.connect(client_id.c_str())) {
        LOG("Connected to MQTT broker");
        mqttClient.subscribe(mqtt_topic);
    } else {
        LOGF("Failed to connect to MQTT broker, rc=%d\n", mqttClient.state());
        delay(5000);
    }
}

void updateAirPollution() {
    if (!intervalElapsed(lastAirUpdateMs, REFRESH_TIME_AQI_MS))
        return;

    LOGF("AirUpdate: epoch=%lu, lastMs=%lu\n", (unsigned long)nowEpoch,
         lastAirUpdateMs);
    AirQuality *aqi = nullptr;
    if (debug) {
        static AirQuality debugAqi;
        debugAqi = AirQuality();
        aqi = &debugAqi;
    } else if (weather != nullptr) {
        aqi = weather->GetAirPollution();
    }
    if (aqi != nullptr)
        ui.drawAirQuality(aqi, pollutionPoint);
}

bool getForecast() {
    if (weather == nullptr)
        return false;

    // 12 slots cover 36 hours. The display uses 4 slots, and the cache remains
    // useful if a scheduled refresh is delayed or temporarily fails.
    const size_t maxSize = 12;
    Forecast *newForecasts = weather->GetForecast(maxSize);
    size_t newForecastsCount = weather->GetForecastCount();

    if (newForecasts == nullptr || newForecastsCount == 0)
        return false;

    forecasts = newForecasts;
    forecastsCount = newForecastsCount;
    return true;
}

void loadDebugForecasts() {
    forecasts = (Forecast *)malloc(4 * sizeof(Forecast));
    forecasts[0] = Forecast(SUN_01D, 23.2, 24.54, 30);
    forecasts[1] = Forecast(MOON_02N, 15.2, 24.54, 30);
    forecasts[2] = Forecast(CLOUDSUN_02D, 25.2, 24.54, 30);
    forecasts[3] = Forecast(SHOWERRAIN_09, 23.2, 24.54, 30);
    forecastsCount = 4;
}

bool loadForecasts() {
    if (debug) {
        loadDebugForecasts();
        return true;
    } else {
        return getForecast();
    }
}

void advanceForecastIndex(time_t currentEpoch) {
    if (forecasts == nullptr || forecastsCount == 0)
        return;
    if (currForecastIdx >= forecastsCount)
        currForecastIdx = forecastsCount - 1;

    while (currForecastIdx + 1 < forecastsCount &&
           currentEpoch >= forecasts[currForecastIdx + 1].timeStamp.getUnix()) {
        currForecastIdx++;
    }
}

bool forecastWindowNeedsRefresh() {
    if (forecasts == nullptr || forecastsCount < FORECAST_SLOTS)
        return true;

    return (size_t)currForecastIdx + FORECAST_SLOTS > forecastsCount;
}

void updateCityName() {
    static String lastCityName;
    if (weather == nullptr)
        return;

    const char *cityName = weather->GetCityName();
    if (cityName == nullptr || cityName[0] == '\0' || lastCityName == cityName)
        return;

    ui.drawCityName(cityName);
    lastCityName = cityName;
}

void updateForecast() {
    static bool firstDraw = true;
    static bool forecastStatusShown = false;
    static unsigned long lastForecastAttemptMs = 0;
    static unsigned long lastForecastRefreshMs = 0;

    uint8_t previousForecastIdx = currForecastIdx;
    const bool hasForecasts = forecasts != nullptr && forecastsCount > 0;
    if (hasForecasts)
        advanceForecastIndex(nowEpoch);

    const bool retryElapsed =
        lastForecastAttemptMs == 0 ||
        nowMs - lastForecastAttemptMs >= FORECAST_RETRY_INTERVAL_MS;
    const bool refreshElapsed =
        lastForecastRefreshMs == 0 ||
        nowMs - lastForecastRefreshMs >= FORECAST_REFRESH_INTERVAL_MS;
    const bool shouldFetch =
        !hasForecasts || refreshElapsed || forecastWindowNeedsRefresh();

    if (shouldFetch && retryElapsed) {
        bool loaded = loadForecasts();
        lastForecastAttemptMs = nowMs;
        if (loaded) {
            updateCityName();
            currForecastIdx = 0;
            firstDraw = true;
            lastForecastRefreshMs = nowMs;
        }
    }

    if (forecasts == nullptr || forecastsCount == 0) {
        if (!forecastStatusShown) {
            ui.drawForecastStatus(forecastPoint, FORECAST_SLOTS,
                                  "Error fetching  data");
            forecastStatusShown = true;
        }
        return;
    }

    advanceForecastIndex(nowEpoch);

    bool shouldDraw = firstDraw || currForecastIdx != previousForecastIdx;
    if (shouldDraw) {
        LOGF("updateForecast: %lu, %lu\n", (unsigned long)nowEpoch,
             forecasts[currForecastIdx].timeStamp.getUnix());
        ui.drawForecast(forecasts, forecastsCount, currForecastIdx,
                        forecastPoint, FORECAST_SLOTS);
        firstDraw = false;
        forecastStatusShown = false;
    }
}

void setPoints() {
    // Vertical rhythm: top sensors, forecast, then a bottom CO2 band.
    sensorPoint.x = 10;
    sensorPoint.y = 28;

    extTermIgroPoint.x = 140;
    extTermIgroPoint.y = 28;

    forecastPoint.x = 15;
    forecastPoint.y = 126;

    pollutionPoint.x = 282;
    pollutionPoint.y = 32;

    co2ValuesPoint.x = 18;
    co2ValuesPoint.y = 244;

    timePoint.x = 272;
    timePoint.y = 8;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err)
        return;
    extTermIgro.temp = doc["temp"].as<double>();
    extTermIgro.humid = doc["umid"].as<uint8_t>();
    extTermIgro.dt = nowEpoch;
    extAcquired = true;
    ui.drawTempHumidity(extTermIgroPoint, extTermIgro.temp, extTermIgro.humid);
}

void updateTime() {
    if (timeMgr.updateTime())
        updateLoopClock();

    if (intervalElapsed(lastScreenTimeUpdateMs, SCREEN_TIME_INTERVAL_MS)) {
        struct tm timeinfo;
        localtime_r(&nowEpoch, &timeinfo);

        char buffer[6];
        strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);

        ui.drawClock(timePoint, buffer);
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin();
    airSensor.begin(Wire, true);
    airSensor.setAltitudeCompensation(18);
    sht.begin(Wire, SHT41_I2C_ADDR_44);
    ui.begin();
    ui.drawCenteredStatus("Initialization...");
    wifiProvision.begin("Weather Display");
    // setup device salvato in precedenza
    if (!setupMgr.begin()) {
        ui.drawCenteredStatus("Error starting device...");
        LOG("DeviceSetupManager begin failed\n");
        while (1) {
            delay(1000);
        }
    }

    timeMgr.ensureTimeSynced();
    updateLoopClock();
    if (setupMgr.isProvisioningReady()) {
        devProps.begin(setupMgr.portalServerIp(), setupMgr.deviceId(),
                       setupMgr.deviceSecret());
        devProps.fetchAndStoreIfChanged();
        simpleOTA.begin(setupMgr.portalServerIp(), setupMgr.deviceTypeId(),
                        setupMgr.deviceId(), setupMgr.deviceSecret(), true);
    }

    weather.reset(new WeatherAPI(devProps.Get("WEATHER_API_KEY"),
                                 devProps.Get("latitude"),
                                 devProps.Get("longitude")));

    if (mqttClient.begin(devProps.Get("MQTT_BROKER"),
                         devProps.GetInt("MQTT_PORT", MQTT_PORT),
                         mqttCallback)) {
        connectToMQTT();
    } else {
        LOG("MQTT manager init failed\n");
    }

    setPoints();
    ui.drawStaticLayout(sensorPoint, forecastPoint, pollutionPoint,
                        extTermIgroPoint, co2ValuesPoint, FORECAST_SLOTS);
    updateLoopClock();
    lastSensorDisplayMs = nowMs;
}

void updateSensorValues() {
    if (lastSensorSampleMs != 0 &&
        nowMs - lastSensorSampleMs < SENSOR_SAMPLE_INTERVAL_MS) {
        return;
    }

    if (airSensor.dataAvailable()) {
        lastSensorSampleMs = nowMs;

        float shtTemp, shtHum;
        sht.measureHighPrecision(shtTemp, shtHum);
        lastSensorTemp = shtTemp;
        lastSensorHumid = shtHum;
        sensorCo2Sum += airSensor.getCO2();
        sensorSamples++;
    }

    if (sensorSamples == 0 ||
        !intervalElapsed(lastSensorDisplayMs, SENSOR_DISPLAY_INTERVAL_MS)) {
        return;
    }

    uint16_t avgCo2 =
        (uint16_t)((sensorCo2Sum + (sensorSamples / 2)) / sensorSamples);
    int16_t co2Delta = 0;
    if (hasDisplayedCo2)
        co2Delta = (int16_t)avgCo2 - (int16_t)lastDisplayedCo2;

    ui.drawTempHumidity(sensorPoint, lastSensorTemp,
                        (int)(lastSensorHumid + 0.5));
    ui.drawCo2(co2ValuesPoint, ui.contentWidth(10), avgCo2, co2Delta,
               hasDisplayedCo2);
    lastDisplayedCo2 = avgCo2;
    hasDisplayedCo2 = true;
    resetSensorSamples();
}

void updateExternalTemperature() {
    // se oltre 20 minuti non ricevo nulla, azzero
    if (extAcquired && nowEpoch > extTermIgro.dt + EXT_SENSOR_TIMEOUT_SEC) {
        ui.drawTempHumidity(extTermIgroPoint, -200, -100);
        extAcquired = false;
    }
}

void loop() {
    updateLoopClock();
    updateTime();
    simpleOTA.checkUpdates(86400); // 24 ore
    mqttClient.loop();

    updateSensorValues();
    updateAirPollution();
    updateForecast();
    if (!mqttClient.connected())
        connectToMQTT();
    updateExternalTemperature();
    ui.updatePowerState(nowMs, DISPLAY_POWER_OFF_IDLE_MS);
}
