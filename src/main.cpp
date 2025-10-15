#include <Arduino.h>

#include <GxEPD2_3C.h>
#include <GxEPD2_BW.h>
#include <Wire.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <SensirionI2cSht4x.h>

#include "Icons.h"
#include "SparkFun_SCD30_Arduino_Library.h" //Click here to get the library: http://librarymanager/All#SparkFun_SCD30
#include "WeatherAPI.h"
#include "cert.h"
#include "icons/IconPack.h"
#include "secret_data.h"
#include <NTPClient.h>
#include <PubSubClient.h>
#include <SimpleOTA.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include "SHTSensor.h"

// un'ora - seconds
#define REFRESH_TIME_AQI 3600;
bool debug = false;

SimpleOTA simpleOTA;
SCD30 airSensor;
SensirionI2cSht4x sht;

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
    display(GxEPD2_420_GDEY042T81(/*CS=5*/ SS, /*DC=*/3, /*RES=*/2,
                                  /*BUSY=*/1)); // 400x300, SSD1683
// API key, latitude and longitude
WeatherAPI weather(WEATHER_API_KEY, LATITUDE, LONGITUDE);

unsigned long nextAirRequest = 0;
unsigned long nextSensorRequest = 0;
unsigned long nextScreenTimeUpdate = 0;
// usato per caricare icone in setup;
bool lableIcon = false;
Forecast *forecasts = NULL;

struct Point {
  int x;
  int y;
} sensorPoint, forecastPoint, pollutionPoint, extTermIgroPoint, co2ValuesPoint,
    timePoint;

WiFiUDP udpCleint;
NTPClient timeClient(udpCleint, 7200);

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
                  uint8_t *retHeight = NULL, bool useFont = true) {
  uint8_t x_offset = 0;
  uint8_t y_offset = 3;
  int16_t x1, y1;
  uint16_t width, height;

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
      display.setFont(NULL);
    }
  } else {
    display.setTextSize(textSize);
  }
  // display.setTextSize(textSize);
  display.getTextBounds(text, x, y, &x1, &y1, &width, &height);
  if (retHeight != NULL)
    *retHeight = height;
  display.setPartialWindow(x, y, width + x_offset, height + y_offset);
  display.firstPage();
  do {
    display.fillRect(x, y, width + x_offset, height, GxEPD_WHITE);
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

void connectToMQTT() {
  while (!mqtt_client.connected()) {
    String client_id = "esp32-client-" + String(WiFi.macAddress());
    // Serial.printf("Connecting to MQTT Broker as %s...\n", client_id.c_str());
    if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      // Serial.println("Connected to MQTT broker");
      mqtt_client.subscribe(mqtt_topic);
    } else {
      // Serial.print("Failed to connect to MQTT broker, rc=");
      Serial.print(mqtt_client.state());
      delay(5000);
    }
  }
}

void drawHourForecast(uint8_t startIndex, Point point) {
  uint8_t iconSize = 64;
  uint8_t spacing = 5;
  // used for hour
  uint8_t hourVerticalSpace = point.y + 18;
  uint8_t fontSize = 2;
  uint8_t contextIconOff = 15;
  int x;
  int y;
  for (size_t i = startIndex; i < startIndex + 4; i++) {

    if (i == 0)
      x = point.x;
    else
      x = i * (102) + 22;

    y = point.y;

    char buffer[10];
    uint8_t retHeight = 0;

    if (lableIcon) {
      y += iconSize;
      drawImage(THERMOMETER, x - contextIconOff, y += spacing, 16);
      drawImage(HUMAN, x - contextIconOff, y += 16 + spacing, 16);
      drawImage(HUMIDITY, x - contextIconOff, y += 16 + spacing, 16);
    } else {
      if (forecasts == NULL)
        return;
      drawImage(forecasts[i].icon, x, y);
      x += 15;
      y += iconSize;
      // temp
      sprintf(buffer, "%.1f\n", forecasts[i].temp);
      writePartial((String)buffer, x, y += spacing, fontSize, &retHeight,
                   false);
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
                   display.height() - (x - spacing - 20), fontSize - 1, NULL,
                   false);
      // Serial.println(buffer);
      display.setRotation(oldRot);
    }
  }
}

void drawTempAndHumidValues(Point point, double temp, int humid) {
  char buffer[10];
  uint8_t retHeight = 0;
  uint8_t spacing = 10;
  uint8_t fontSize = 4;
  uint8_t xLableOffset = 105;
  uint8_t yLableOffset = 5;
  uint16_t x = point.x;
  uint16_t y = point.y;

  if (lableIcon) {
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
  uint8_t space = 5;
  uint8_t xLableOffset = 60;
  uint16_t x = point.x;
  uint16_t y = point.y;
  char buffer[10];

  if (lableIcon) {
    writePartial("AQI", x, y, 2, &retHeight, false);
    writePartial("pm2", x, y += space + retHeight, 2, &retHeight, false);
    writePartial("pm10", x, y += space + retHeight, 2, &retHeight, false);
  } else {
    if (aqi == NULL)
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
}

void drawCo2Values(Point point, uint16_t width, uint16_t height, uint16_t co2) {
  int x = point.x;
  int y = point.y;

  int range = 3000;
  double resolution = range / width;

  if (lableIcon) {
    int xLableOffset = 75;
    drawImage(CO2, x + xLableOffset, y, 16);
    // dati raccolti online
    uint excelentX = x;
    uint goodX = x + (450 / (int)resolution);
    uint fairX = x + (700 / (int)resolution);
    uint sleepyX = x + (1000 / (int)resolution);
    uint badX = x + (2500 / (int)resolution);
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

void drawLables() {
  lableIcon = true;
  writePartial("int", 50, 2, 1, nullptr, false);
  writePartial("ext", 250, 2, 1, nullptr, false);
  drawTempAndHumidValues(sensorPoint, 0.0, 0);
  drawCo2Values(co2ValuesPoint, display.width() - (10 * 2), 10, 0);
  drawAirQuality(NULL, pollutionPoint);
  drawHourForecast(0, forecastPoint);
  drawTempAndHumidValues(extTermIgroPoint, -200, -100);
  lableIcon = false;
}

void UpdateAirPollution() {
  unsigned long currTime = timeClient.getEpochTime();
  // Serial.printf("AirUpdate: %d, %d\n",currTime, nextAirRequest);
  if (currTime >= nextAirRequest) {
    nextAirRequest = currTime + REFRESH_TIME_AQI;
    AirQuality *aqi;
    if (debug) {
      aqi = (AirQuality *)malloc(sizeof(AirQuality));
      *aqi = AirQuality();
    } else {
      aqi = weather.GetAirPollution();
    }
    if (aqi != NULL)
      drawAirQuality(aqi, pollutionPoint);
  }
}

/// @brief utilizzo questa funzione poiché
/// capita che quando richiedo nuovi dati parta 3 ore dopo
void getForecast() {
  // min is 7, otherwise on update -> indexOutOfBound
  int size = 8;
  Forecast *temp = NULL;
  if (forecasts != NULL) {
    temp = (Forecast *)malloc(sizeof(Forecast) * size);
    memcpy(temp, forecasts, sizeof(Forecast) * size);
  }
  forecasts = weather.GetForecast(size);
  // primo avvio
  if (temp == NULL || forecasts == NULL)
    return;
  unsigned long currTime = timeClient.getEpochTime();
  // valuto se il primo oggetto rientra nella previsione corrente
  if (forecasts[0].timeStamp.getUnix() < currTime)
    return;
  // altrimenti copio il vecchio elemento nella prima posizione;
  int positionOldArray = 0;
  for (size_t i = 0; i < size; i++) {
    if ((temp[i].timeStamp.getUnix() + (3600 * 3)) > currTime) {
      // shifto in avanti
      for (size_t j = size - 1; j >= 1; j--) {
        forecasts[j] = forecasts[j - 1];
      }
      positionOldArray = i;
      break;
    }
  }
  forecasts[0] = temp[positionOldArray];
  free(temp);
}

void UpdateForecast() {

  if (forecasts == NULL ||
      timeClient.getEpochTime() >
          forecasts[currForecastIdx + 1].timeStamp.getUnix()) {
    // aggiorno dati ogni 9 ore: 2 *
    if (forecasts == NULL || currForecastIdx >= 3) {
      if (debug) {
        // DEBUG
        forecasts = (Forecast *)malloc(4 * sizeof(Forecast));
        forecasts[0] = Forecast(SUN_01D, 23.2, 24.54, 30);
        forecasts[1] = Forecast(MOON_02N, 15.2, 24.54, 30);
        forecasts[2] = Forecast(CLOUDSUN_02D, 25.2, 24.54, 30);
        forecasts[3] = Forecast(SHOWERRAIN_09, 23.2, 24.54, 30);
      } else {
        getForecast();
      }
      currForecastIdx = 0;
    }
    // Serial.printf("UpdateForecast: %d, %d\n",timeClient.getEpochTime() ,
    // forecasts[currForecastIdx].timeStamp.getUnix());
    drawHourForecast(currForecastIdx, forecastPoint);
    currForecastIdx++;
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
  deserializeJson(doc, payload);
  extTermIgro.temp = doc["temp"].as<double>();
  extTermIgro.humid = doc["umid"].as<uint8_t>();
  extTermIgro.dt = timeClient.getEpochTime();
  extAcquired = true;
  drawTempAndHumidValues(extTermIgroPoint, extTermIgro.temp, extTermIgro.humid);
}

void updateTime() {
  // aggiorno ogni 60 secondi
  if (timeClient.getEpochTime() > nextScreenTimeUpdate) {
    nextScreenTimeUpdate = timeClient.getEpochTime() + 60;
    char buffer[6];
    sprintf(buffer, "%02d:%02d\n", timeClient.getHours(),
            timeClient.getMinutes());
    writePartial(buffer, timePoint.x, timePoint.y, 2, NULL, false);
  }
}

void setup() {
  if (debug)
    Serial.begin(115200);
  Wire.begin();
  airSensor.begin(Wire, true);
  airSensor.setAltitudeCompensation(18);
  sht.begin(Wire, SHT41_I2C_ADDR_44);
  initDisplay();

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect("WeatherStation");
  wifiManager.setDarkMode(true);
  wifiManager.setShowInfoUpdate(false);

  simpleOTA.begin(64, ota_server_url, token_id, true);

  timeClient.begin();

  // sincronizzo ogni 60 minuti
  timeClient.setUpdateInterval(36e5); // 10 minuti

  esp_client.setCACert(ssl_ca_cert);
  esp_client.setInsecure();
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setKeepAlive(60);
  mqtt_client.setCallback(mqttCallback);
  connectToMQTT();

  setPoints();
  drawLables();
}

void updateSensorValues() {
  if (millis() > nextSensorRequest && airSensor.dataAvailable()) {
    nextSensorRequest = millis() + 20000;
    float shtTemp, shtHum;
    sht.measureHighPrecision(shtTemp, shtHum);
    drawTempAndHumidValues(sensorPoint, shtTemp, (int)shtHum);
    drawCo2Values(co2ValuesPoint, display.width() - (10 * 2), 15, airSensor.getCO2());
  }
}

void updateExternalTemperature() {
  // se oltre 20 minuti non ricevo nulla azzero
  if (extAcquired &&
      timeClient.getEpochTime() > extTermIgro.dt + 1200) { // 20 muinuti
    drawTempAndHumidValues(extTermIgroPoint, -200, -100);
    extAcquired = false;
  }
}

void loop() {
  simpleOTA.checkUpdates(86400); // 24 ore

  timeClient.update();
  updateTime();
  updateSensorValues();
  // AQI
  UpdateAirPollution();
  // FORECAST
  UpdateForecast();
  // MQTT
  if (!mqtt_client.connected())
    connectToMQTT();
  mqtt_client.loop();
  // EXT_SENSOR
  updateExternalTemperature();
  // display.hibernate();
}
