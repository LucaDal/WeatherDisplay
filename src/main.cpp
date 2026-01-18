#include <Arduino.h>

#include <ArduinoJson.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_BW.h>
#include <SensirionI2cSht4x.h>
#include <Wire.h>

#include "CommonDebug.h"
#include "DeviceSetupManager.h"
#include "EEPROM.h"
#include "Icons.h"
#include "LiteWiFiManager.h"
#include "MyDeviceProperties.h"
#include "SHTSensor.h"
#include "SparkFun_SCD30_Arduino_Library.h" //Click here to get the library: http://librarymanager/All#SparkFun_SCD30
#include "WeatherAPI.h"
#include "cert.h"
#include "icons/IconPack.h"
#include "secret_data.h"
#include <NTPClient.h>
#include <PubSubClient.h>
#include <SimpleOTA.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <memory>

const int MQTT_PORT = 8883;
const int EEPROM_SIZE = 4096;
const uint8_t FORECAST_SLOTS = 4;
#define LOG(msg) DBG_LOG("", msg)
#define LOGF(fmt, ...) DBG_LOGF("", fmt, ##__VA_ARGS__)
const unsigned long REFRESH_TIME_AQI = 3600; // un'ora - seconds
bool debug = false;

MyDeviceProperties deviceProperties(EEPROM_SIZE, 0, 3, 1024, true);
DeviceSetupManager setupMgr(EEPROM_SIZE, 3);
LiteWiFiManager wifiProvision;
SimpleOTA simpleOTA;
const char *DEVICE_ID;

SCD30 airSensor;
SensirionI2cSht4x sht;

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
    display(GxEPD2_420_GDEY042T81(/*CS=5*/ SS, /*DC=*/3, /*RES=*/2,
                                  /*BUSY=*/1)); // 400x300, SSD1683
// API key, latitude and longitude
std::unique_ptr<WeatherAPI> weather;

unsigned long nextAirRequest = 0;
unsigned long nextSensorRequest = 0;
unsigned long nextScreenTimeUpdate = 0;
// usato per caricare icone in setup
bool labelIcon = false;
Forecast *forecasts = nullptr;
size_t forecastsCount = 0;

struct Point {
  int x;
  int y;
} sensorPoint, forecastPoint, pollutionPoint, extTermIgroPoint, co2ValuesPoint,
    timePoint;

void drawTempAndHumidValues(Point point, double temp, int humid);
void drawCo2Values(Point point, uint16_t width, uint16_t height, uint16_t co2);
void drawHourForecast(uint8_t startIndex, Point point);

WiFiUDP udpClient;
NTPClient timeClient(udpClient, 7200);

uint8_t currForecastIdx = 0;

WiFiClientSecure esp_client;
PubSubClient mqtt_client(esp_client);

struct TermoIgro {
  double temp = 0;
  uint8_t humid = 0;
  uint32_t dt = 0;
} extTermIgro;
bool extAcquired;

void writePartial(String text, uint16_t x, uint16_t y, uint8_t textSize,
                  uint8_t *retHeight = nullptr, bool useFont = true) {
  const uint8_t x_offset = 0;
  const uint8_t y_offset = 3;
  int16_t x1, y1;
  uint16_t width, height;
  struct PartialCache {
    uint16_t x;
    uint16_t y;
    uint8_t textSize;
    bool useFont;
    uint16_t width;
    uint16_t height;
    bool used;
  };
  static PartialCache cache[16] = {};
  static uint8_t cacheNext = 0;

  if (useFont) {
    switch (textSize) {
    case 9:
      display.setFont(&FreeSans9pt7b);
      break;
    case 12:
      display.setFont(&FreeSans12pt7b);
      break;
    case 18:
      display.setFont(&FreeSans18pt7b);
      break;
    default:
      display.setFont(nullptr);
    }
  } else {
    display.setFont(nullptr);
    display.setTextSize(textSize);
  }
  // display.setTextSize(textSize);
  display.getTextBounds(text, x, y, &x1, &y1, &width, &height);
  if (retHeight != nullptr)
    *retHeight = height;
  int16_t clearWidth = (int16_t)width + x_offset + 2;
  int16_t clearHeight = (int16_t)height + y_offset + 2;
  if (clearWidth < 0)
    clearWidth = 0;
  if (clearHeight < 0)
    clearHeight = 0;
  bool cacheHit = false;
  for (size_t i = 0; i < (sizeof(cache) / sizeof(cache[0])); i++) {
    if (!cache[i].used)
      continue;
    if (cache[i].x == x && cache[i].y == y && cache[i].textSize == textSize &&
        cache[i].useFont == useFont) {
      if (cache[i].width > (uint16_t)clearWidth)
        clearWidth = cache[i].width;
      if (cache[i].height > (uint16_t)clearHeight)
        clearHeight = cache[i].height;
      cache[i].width = clearWidth;
      cache[i].height = clearHeight;
      cacheHit = true;
      break;
    }
  }
  if (!cacheHit) {
    cache[cacheNext] = {x, y, textSize, useFont, (uint16_t)clearWidth,
                        (uint16_t)clearHeight, true};
    cacheNext = (cacheNext + 1) % (sizeof(cache) / sizeof(cache[0]));
  }
  display.setPartialWindow(x, y, (uint16_t)clearWidth,
                           (uint16_t)clearHeight);
  display.firstPage();
  do {
    display.fillRect(x, y, (uint16_t)clearWidth, (uint16_t)clearHeight,
                     GxEPD_WHITE);
    uint8_t offset = 0;
    if (useFont)
      offset = height;
    display.setCursor(x, y + offset);
    display.println(text);
  } while (display.nextPage());
}

void drawImage(const uint8_t *image, size_t x, size_t y, size_t iconSize = 64) {
  display.drawImage(image, x, y, iconSize, iconSize, false, false);
}

void initDisplay() {
  display.init(115200, true, 50, false);
  display.setRotation(4);
  display.setTextColor(GxEPD_BLACK);
  display.setTextWrap(false);
}

void clearScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
}

void drawCenteredStatus(const char *text) {
  int16_t x1, y1;
  uint16_t width, height;
  display.setFont(&FreeSans12pt7b);
  display.getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
  int16_t x = (display.width() - width) / 2 - x1;
  int16_t y = (display.height() - height) / 2 - y1;
  for (uint8_t i = 0; i < 2; i++) {
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());
  }
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print(text);
  } while (display.nextPage());
}

void drawLabelText(const char *text, int16_t x, int16_t y, uint8_t textSize,
                   uint16_t *retHeight = nullptr) {
  int16_t x1, y1;
  uint16_t width, height;
  display.setFont(nullptr);
  display.setTextSize(textSize);
  display.getTextBounds(text, x, y, &x1, &y1, &width, &height);
  if (retHeight != nullptr)
    *retHeight = height;
  display.setCursor(x, y);
  display.print(text);
}

void drawLabelTextCleared(const char *text, int16_t x, int16_t y,
                          uint8_t textSize, uint16_t *retHeight = nullptr) {
  int16_t x1, y1;
  uint16_t width, height;
  display.setFont(nullptr);
  display.setTextSize(textSize);
  display.getTextBounds(text, x, y, &x1, &y1, &width, &height);
  if (retHeight != nullptr)
    *retHeight = height;
  int16_t rx = x1 - 1;
  int16_t ry = y1 - 1;
  int16_t rw = (int16_t)width + 2;
  int16_t rh = (int16_t)height + 2;
  if (rx < 0) {
    rw = rw + rx;
    rx = 0;
  }
  if (ry < 0) {
    rh = rh + ry;
    ry = 0;
  }
  if (rw < 0)
    rw = 0;
  if (rh < 0)
    rh = 0;
  display.fillRect(rx, ry, (uint16_t)rw, (uint16_t)rh, GxEPD_WHITE);
  display.setCursor(x, y);
  display.print(text);
}

void drawAirQualityLabels(Point point) {
  const uint8_t space = 5;
  uint16_t x = point.x;
  uint16_t y = point.y;
  uint16_t retHeight = 0;

  drawLabelText("AQI", x, y, 2, &retHeight);
  y += space + retHeight;
  drawLabelText("pm2", x, y, 2, &retHeight);
  y += space + retHeight;
  drawLabelText("pm10", x, y, 2, &retHeight);
}

void drawForecastContextIcons(int16_t x, int16_t y, uint8_t iconSize,
                              uint8_t spacing, uint8_t contextIconOff) {
  y += iconSize;
  drawImage(THERMOMETER, x - contextIconOff, y += spacing, 16);
  drawImage(HUMAN, x - contextIconOff, y += 16 + spacing, 16);
  drawImage(HUMIDITY, x - contextIconOff, y += 16 + spacing, 16);
}

void drawStaticLayout() {
  labelIcon = true;
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawLabelText("int", 50, 2, 1);
    drawLabelText("ext", 250, 2, 1);
    drawTempAndHumidValues(sensorPoint, 0.0, 0);
    drawCo2Values(co2ValuesPoint, display.width() - (10 * 2), 10, 0);
    drawAirQualityLabels(pollutionPoint);
    drawHourForecast(0, forecastPoint);
    drawTempAndHumidValues(extTermIgroPoint, -200, -100);
  } while (display.nextPage());
  labelIcon = false;
}

void connectToMQTT() {
  JsonDocument &doc = deviceProperties.json();
  const char *mqtt_topic = doc["topic_igrometro"] | "";
  const char *mqtt_password = doc["password"] | "";
  const char *mqtt_username = doc["username"] | "";
  String client_id = "esp32-client-" + String(WiFi.macAddress());
  LOGF("Connecting to MQTT Broker as %s...\n", client_id.c_str());
  LOGF("topic: [%s], pw:  [%s], user: [%s]\n", mqtt_topic, mqtt_password,
       mqtt_username);
  if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
    LOG("Connected to MQTT broker");
    mqtt_client.subscribe(mqtt_topic);
  } else {
    LOGF("Failed to connect to MQTT broker, rc=%d\n", mqtt_client.state());
    delay(5000);
  }
}

void drawHourForecast(uint8_t startIndex, Point point) {
  const uint8_t iconSize = 64;
  const uint8_t spacing = 5;
  // used for hour
  const uint8_t hourVerticalSpace = point.y + 18;
  const uint8_t fontSize = 2;
  const uint8_t contextIconOff = 15;
  int x;
  int y;
  if (labelIcon) {
    for (size_t i = 0; i < FORECAST_SLOTS; i++) {
      x = point.x + (i * 102);
      drawForecastContextIcons(x, point.y, iconSize, spacing, contextIconOff);
    }
    return;
  }
  if (forecasts == nullptr || forecastsCount == 0 ||
      startIndex >= forecastsCount)
    return;
  size_t end = startIndex + FORECAST_SLOTS;
  if (end > forecastsCount)
    end = forecastsCount;
  for (size_t i = startIndex; i < end; i++) {
    size_t column = i - startIndex;
    x = point.x + (column * 102);

    y = point.y;

    char buffer[10];
    uint8_t retHeight = 0;

    drawImage(forecasts[i].icon, x, y);
    x += 15;
    y += iconSize;
    // temp
    sprintf(buffer, "%.1f\n", forecasts[i].temp);
    writePartial((String)buffer, x, y += spacing, fontSize, &retHeight, false);
    // percived temp
    sprintf(buffer, "%.1f\n", forecasts[i].percivedTemp);
    writePartial((String)buffer, x, y += (retHeight + spacing), fontSize,
                 &retHeight, false);
    // humid
    sprintf(buffer, "%02d\n", forecasts[i].humidity);
    writePartial(buffer, x, y += (retHeight + spacing), fontSize, &retHeight,
                 false);
    // hour
    uint8_t oldRot = display.getRotation();
    display.setRotation(1);
    sprintf(buffer, "%02d:%02d\n", forecasts[i].timeStamp.hour,
            forecasts[i].timeStamp.minute);
    writePartial((String)buffer, hourVerticalSpace,
                 display.height() - (x - spacing - 20), fontSize - 1, nullptr,
                 false);
    LOG(buffer);
    display.setRotation(oldRot);
  }
}

void drawTempAndHumidValues(Point point, double temp, int humid) {
  char buffer[10];
  uint8_t retHeight = 0;
  const uint8_t spacing = 10;
  const uint8_t fontSize = 4;
  const uint8_t xLableOffset = 105;
  const uint8_t yLableOffset = 5;
  uint16_t x = point.x;
  uint16_t y = point.y;

  if (labelIcon) {
    drawImage(THERMOMETER, x + xLableOffset, y + yLableOffset, 16);
    drawImage(HUMIDITY, x + xLableOffset, y += 32 + spacing + yLableOffset, 16);
  } else {
    // temp
    if (temp > -100) {
      sprintf(buffer, "%02.1f\n", temp);
      writePartial((String)buffer, x, y, fontSize, &retHeight, false);
    } else {
      writePartial("----", x, y, fontSize, &retHeight, false);
    }
    // humid
    if (humid > -1) {
      sprintf(buffer, "%02d  \n", humid);
      writePartial(buffer, x, y += (retHeight + spacing), fontSize, &retHeight,
                   false);
    } else {
      writePartial("----", x, y += (retHeight + spacing), fontSize, &retHeight,
                   false);
    }
  }
}

void drawAirQuality(AirQuality *aqi, Point point) {
  uint8_t retHeight;
  const uint8_t space = 5;
  const uint8_t xLableOffset = 60;
  uint16_t x = point.x;
  uint16_t y = point.y;
  char buffer[10];

  if (labelIcon) {
    drawAirQualityLabels(point);
    return;
  }
  if (aqi == nullptr)
    return;
  writePartial(aqi->AQIToString(), x + xLableOffset, y, 2, &retHeight, false);
  // pm2
  sprintf(buffer, "%d  \n", aqi->pm2_5);
  writePartial(buffer, x + xLableOffset, y += space + retHeight, 2,
               &retHeight, false);
  // pm10
  sprintf(buffer, "%d  \n", aqi->pm10);
  writePartial(buffer, x + xLableOffset, y += space + retHeight, 2,
               &retHeight, false);
}

void drawCo2Values(Point point, uint16_t width, uint16_t height, uint16_t co2) {
  int x = point.x;
  int y = point.y;

  const int range = 3000;
  double resolution = range / width;

  if (labelIcon) {
    const int xLableOffset = 75;
    drawImage(CO2, x + xLableOffset, y, 16);
    // dati raccolti online
    uint excelentX = x;
    uint goodX = x + (800 / (int)resolution);
    uint fairX = x + (1000 / (int)resolution);
    uint sleepyX = x + (1400 / (int)resolution);
    uint badX = x + (1800 / (int)resolution);
    uint8_t offsetFaceState = 25 + height;
    drawImage(FACE_EXCELLENT, excelentX, y + offsetFaceState, 16);
    drawImage(FACE_GOOD, goodX, y + offsetFaceState, 16);
    drawImage(FACE_FAIR, fairX, y + offsetFaceState, 16);
    drawImage(FACE_SLEEPY, sleepyX, y + offsetFaceState, 16);
    drawImage(FACE_BAD, badX, y + offsetFaceState, 16);
    return;
  }
  char buffer[6];
  uint8_t retHeight;
  sprintf(buffer, "%05d\n", co2);
  // metto gli spazi al posto dei leading zeros
  for (size_t i = 0; i < strlen(buffer); i++) {
    if (buffer[i] == '0' && i < 4)
      buffer[i] = ' ';
    else
      break;
  }

  writePartial(buffer, x, y, 2, &retHeight, false);
  y += retHeight + 3;
  co2 = co2 > range ? range : co2;
  int co2Width = co2 / (int)resolution;

  display.setPartialWindow(x, y, width, height);
  display.firstPage();
  do {
    display.fillRect(x, y, width, height, GxEPD_WHITE);
    display.drawRect(x, y, width, height, GxEPD_BLACK);
    display.fillRect(x, y, co2Width, height, GxEPD_BLACK);
    ;
  } while (display.nextPage());
}

void updateAirPollution() {
  const unsigned long currTime = timeClient.getEpochTime();
  LOGF("AirUpdate: %d, %d\n", currTime, nextAirRequest);
  if (currTime >= nextAirRequest) {
    nextAirRequest = currTime + REFRESH_TIME_AQI;
    AirQuality *aqi = nullptr;
    if (debug) {
      static AirQuality debugAqi;
      debugAqi = AirQuality();
      aqi = &debugAqi;
    } else {
      aqi = weather->GetAirPollution();
    }
    if (aqi != nullptr)
      drawAirQuality(aqi, pollutionPoint);
  }
}

/// @brief uso questa funzione poiche'
/// a volte, dopo il refresh, parte dalla previsione di 3 ore dopo
void getForecast() {
  // min is 7, otherwise on update -> indexOutOfBounds
  const size_t maxSize = 8;
  size_t previousCount = forecastsCount;
  Forecast *temp = nullptr;
  if (forecasts != nullptr && previousCount > 0) {
    temp = (Forecast *)malloc(sizeof(Forecast) * previousCount);
    if (temp != nullptr)
      memcpy(temp, forecasts, sizeof(Forecast) * previousCount);
  }
  forecasts = weather->GetForecast(maxSize);
  forecastsCount = weather->GetForecastCount();
  // primo avvio
  if (forecasts == nullptr || forecastsCount == 0) {
    if (temp != nullptr)
      free(temp);
    return;
  }
  unsigned long currTime = timeClient.getEpochTime();
  size_t currentIdx = forecastsCount;
  for (size_t i = 0; i < forecastsCount; i++) {
    if (forecasts[i].timeStamp.getUnix() <= currTime)
      currentIdx = i;
  }

  if (currentIdx == forecastsCount) {
    if (temp != nullptr && previousCount > 0 &&
        previousCount <= forecastsCount) {
      memcpy(forecasts, temp, sizeof(Forecast) * previousCount);
      forecastsCount = previousCount;
    }
    if (temp != nullptr)
      free(temp);
    return;
  }

  if (currentIdx > 0) {
    size_t newCount = forecastsCount - currentIdx;
    for (size_t i = 0; i < newCount; i++) {
      forecasts[i] = forecasts[i + currentIdx];
    }
    forecastsCount = newCount;
  }

  if (temp != nullptr)
    free(temp);
}

void loadDebugForecasts() {
  forecasts = (Forecast *)malloc(4 * sizeof(Forecast));
  forecasts[0] = Forecast(SUN_01D, 23.2, 24.54, 30);
  forecasts[1] = Forecast(MOON_02N, 15.2, 24.54, 30);
  forecasts[2] = Forecast(CLOUDSUN_02D, 25.2, 24.54, 30);
  forecasts[3] = Forecast(SHOWERRAIN_09, 23.2, 24.54, 30);
  forecastsCount = 4;
}

void loadForecasts() {
  if (debug) {
    loadDebugForecasts();
  } else {
    getForecast();
  }
}

void updateForecast() {
  static bool firstDraw = true;

  if (forecasts == nullptr || forecastsCount == 0) {
    loadForecasts();
    currForecastIdx = 0;
    firstDraw = true;
  } else if (currForecastIdx >= (FORECAST_SLOTS - 1)) {
    // aggiorno dati ogni 9 ore: 2 *
    loadForecasts();
    currForecastIdx = 0;
    firstDraw = true;
  }

  if (forecasts == nullptr || forecastsCount == 0)
    return;

  const unsigned long now = timeClient.getEpochTime();
  uint8_t newIdx = currForecastIdx;
  while (newIdx + 1 < forecastsCount &&
         now > forecasts[newIdx + 1].timeStamp.getUnix()) {
    newIdx++;
  }

  if (firstDraw || newIdx != currForecastIdx) {
    currForecastIdx = newIdx;
    LOGF("updateForecast: %d, %d\n", now,
         forecasts[currForecastIdx].timeStamp.getUnix());
    drawHourForecast(currForecastIdx, forecastPoint);
    firstDraw = false;
  }
}

void setPoints() {
  sensorPoint.x = 10;
  sensorPoint.y = 15;

  extTermIgroPoint.x = 155;
  extTermIgroPoint.y = 15;

  forecastPoint.x = 22;
  forecastPoint.y = 170;

  pollutionPoint.x = 290;
  pollutionPoint.y = 30;

  co2ValuesPoint.x = 5;
  co2ValuesPoint.y = 105;

  timePoint.x = 310;
  timePoint.y = 5;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err)
    return;
  extTermIgro.temp = doc["temp"].as<double>();
  extTermIgro.humid = doc["umid"].as<uint8_t>();
  extTermIgro.dt = timeClient.getEpochTime();
  extAcquired = true;
  drawTempAndHumidValues(extTermIgroPoint, extTermIgro.temp, extTermIgro.humid);
}

void updateTime() {
  // aggiorno ogni 60 secondi
  const unsigned long now = timeClient.getEpochTime();
  if (now > nextScreenTimeUpdate) {
    nextScreenTimeUpdate = now + 60;
    char buffer[6];
    sprintf(buffer, "%02d:%02d\n", timeClient.getHours(),
            timeClient.getMinutes());
    writePartial(buffer, timePoint.x, timePoint.y, 2, nullptr, false);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  airSensor.begin(Wire, true);
  airSensor.setAltitudeCompensation(18);
  sht.begin(Wire, SHT41_I2C_ADDR_44);
  initDisplay();
  drawCenteredStatus("Connessione in corso...");
  wifiProvision.begin("Weather Display");
  size_t nextOffset = 0;
  // setup device salvato in precedenza
  setupMgr.begin();

  DEVICE_ID = setupMgr.readCString(0, &nextOffset);
  deviceProperties.begin(PORTAL_SERVER_IP, DEVICE_ID, nextOffset);
  drawCenteredStatus("Ricerca aggiornamento...");
  deviceProperties.fetchAndStoreIfChanged();
  simpleOTA.begin(EEPROM_SIZE, PORTAL_SERVER_IP, DEVICE_ID, true);
  timeClient.begin();

  JsonDocument &doc = deviceProperties.json();
  const char *lat = doc["latitude"] | "";
  const char *lon = doc["longitude"] | "";
  LOGF("lat: %s, lon %s\n", lat, lon);
  weather.reset(new WeatherAPI(WEATHER_API_KEY, lat, lon));
  // sincronizzo ogni 60 minuti
  timeClient.setUpdateInterval(36e5); // 60 minuti

  esp_client.setCACert(ssl_ca_cert);
  esp_client.setInsecure();
  mqtt_client.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt_client.setKeepAlive(60);
  mqtt_client.setCallback(mqttCallback);
  connectToMQTT();

  setPoints();
  clearScreen();
  drawStaticLayout();
}

void updateSensorValues() {
  const unsigned long now = millis();
  const unsigned long sensorIntervalMs = 20000;
  if (now > nextSensorRequest && airSensor.dataAvailable()) {
    nextSensorRequest = now + sensorIntervalMs;
    float shtTemp, shtHum;
    sht.measureHighPrecision(shtTemp, shtHum);
    drawTempAndHumidValues(sensorPoint, shtTemp, (int)shtHum);
    drawCo2Values(co2ValuesPoint, display.width() - (10 * 2), 15,
                  airSensor.getCO2());
  }
}

void updateExternalTemperature() {
  const unsigned long now = timeClient.getEpochTime();
  // se oltre 20 minuti non ricevo nulla, azzero
  if (extAcquired && now > extTermIgro.dt + 1200) { // 20 minuti
    drawTempAndHumidValues(extTermIgroPoint, -200, -100);
    extAcquired = false;
  }
}

void loop() {
  simpleOTA.checkUpdates(86400); // 24 ore

  timeClient.update();
  updateTime();
  updateSensorValues();
  updateAirPollution();
  updateForecast();
  if (!mqtt_client.connected())
    connectToMQTT();
  mqtt_client.loop();
  updateExternalTemperature();
  // display.hibernate();
}
