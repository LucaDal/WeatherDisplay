#include <Arduino.h>

#include <Wire.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>

#include "SparkFun_SCD30_Arduino_Library.h" //Click here to get the library: http://librarymanager/All#SparkFun_SCD30
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include "WeatherAPI.h"
#include "icons/IconPack.h"
#include "Icons.h"
#include "secret_data.h"
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
//un'ora
#define REFRESH_TIME_AQI 3600000;
bool debug = false;

SCD30 airSensor;
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(/*CS=5*/ SS, /*DC=*/3, /*RES=*/2, /*BUSY=*/1)); // 400x300, SSD1683
//API key, latitude and longitude
WeatherAPI weather(WEATHER_API_KEY, LATITUDE, LONGITUDE);

uint64_t nextAirRequest = 0;
uint64_t nextSensorRequest = 0;
//usato per caricare icone in setup;
bool lableIcon = false;
Forecast* forecasts = NULL;

struct Point {
  int x;
  int y;
}sensorPoint, forecastPoint, pollutionPoint, extTermIgroPoint, co2ValuesPoint;

WiFiUDP udpCleint;
NTPClient timeClient(udpCleint, 7200);

uint8_t currForecastIdx = 0;

WiFiClientSecure esp_client;
PubSubClient mqtt_client(esp_client);

struct TermoIgro{ 
  double temp = 0;
  uint8_t humid = 0;
  uint32_t dt = 0;
} extTermIgro;
bool extAcquired;

void writePartial(String text, uint16_t x, uint16_t y, uint8_t textSize, uint8_t* retHeight = NULL, bool useFont = true) {
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
  }
  else {
    display.setTextSize(textSize);
  }
  //display.setTextSize(textSize);
  display.getTextBounds(text, x, y, &x1, &y1, &width, &height);
  if (retHeight != NULL)
    *retHeight = height;
  display.setPartialWindow(x, y, width + x_offset, height + y_offset);
  display.firstPage();
  do {
    display.fillRect(x, y, width+x_offset, height, GxEPD_WHITE);
    uint8_t offset = 0;
    if (useFont)
      offset = height;
    display.setCursor(x, y + offset);
    display.println(text);
  } while (display.nextPage());
}

void drawImage(const uint8_t* image, size_t x, size_t y, size_t iconSize = 64) {
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
      //Serial.printf("Connecting to MQTT Broker as %s...\n", client_id.c_str());
      if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
          //Serial.println("Connected to MQTT broker");
          mqtt_client.subscribe(mqtt_topic);
      } else {
          //Serial.print("Failed to connect to MQTT broker, rc=");
          Serial.print(mqtt_client.state());
          delay(5000);
      }
  }
}

void drawHourForecast(uint8_t startIndex, Point point) {
  uint8_t iconSize = 64;
  uint8_t spacing = 5;
  //used for hour
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
    }
    else {
      if (forecasts == NULL)
        return;
      drawImage(forecasts[i].icon, x, y);
      x += 15;
      y += iconSize;
      //temp
      sprintf(buffer, "%.1f\n", forecasts[i].temp);
      writePartial((String)buffer, x, y += spacing, fontSize, &retHeight, false);
      //percived temp
      sprintf(buffer, "%.1f\n", forecasts[i].percivedTemp);
      writePartial((String)buffer, x, y += (retHeight + spacing), fontSize, &retHeight, false);
      //humid
      sprintf(buffer, "%02d\n", forecasts[i].humidity);
      writePartial(buffer, x, y += (retHeight + spacing), fontSize, &retHeight, false);
      //hour
      uint8_t oldRot = display.getRotation();
      display.setRotation(1);
      sprintf(buffer, "%02d:%02d\n", forecasts[i].timeStamp.hour, forecasts[i].timeStamp.minute);
      writePartial((String)buffer, hourVerticalSpace, display.height() - (x - spacing - 20), fontSize - 1, NULL, false);
      //Serial.println(buffer);
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
  uint8_t yLableOffset = 8;
  uint16_t x = point.x;
  uint16_t y = point.y;

  if (lableIcon) {
    drawImage(THERMOMETER, x + xLableOffset, y + yLableOffset, 16);
    drawImage(HUMIDITY, x + xLableOffset, y += 32 + spacing + yLableOffset, 16);
  }
  else {
    //temp
    if(temp > -100){
      sprintf(buffer, "%02.1f\n", temp);
      writePartial((String)buffer, x, y, fontSize, &retHeight, false);
    }else{
      writePartial("----", x, y, fontSize, &retHeight, false);
    }
    //humid
    if(humid > -1){
      sprintf(buffer, "%02d\n", humid);
      writePartial(buffer, x, y += (retHeight + spacing), fontSize, &retHeight, false);
    }else{
      writePartial("----", x, y += (retHeight + spacing), fontSize, &retHeight, false);
    }
  }
}

void drawAirQuality(AirQuality* aqi, Point point) {
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
  }
  else {
    if (aqi == NULL)
      return;
    writePartial(aqi->AQIToString(), x + xLableOffset, y, 2, &retHeight, false);
    //pm2
    sprintf(buffer, "%d\n", aqi->pm2_5);
    writePartial(buffer, x + xLableOffset, y += space + retHeight, 2, &retHeight, false);
    //pm10
    sprintf(buffer, "%d\n", aqi->pm10);
    writePartial(buffer, x + xLableOffset, y += space + retHeight, 2, &retHeight, false);
  }
}

void drawCo2Values(Point point, uint16_t width, uint16_t height) {
  int x = point.x;
  int y = point.y;

  int range = 3000;
  double resolution = range / width;

  if(lableIcon){
    int xLableOffset = 75;
    drawImage(CO2, x + xLableOffset, y, 16);
    //dati raccolti online 
    uint excelentX = x;
    uint goodX = x + (450/(int)resolution);
    uint fairX = x + (700/(int)resolution);
    uint sleepyX = x + (1000/(int)resolution);
    uint badX = x + (2500/(int)resolution);
    uint8_t offsetFaceState = 25 + height;
    drawImage(FACE_EXCELLENT, excelentX , y+offsetFaceState, 16);
    drawImage(FACE_GOOD, goodX , y+offsetFaceState, 16);
    drawImage(FACE_FAIR, fairX , y+offsetFaceState, 16);
    drawImage(FACE_SLEEPY, sleepyX , y+offsetFaceState, 16);
    drawImage(FACE_BAD, badX , y+offsetFaceState, 16);
    return;
  }
  char buffer[10];
  int co2 = airSensor.getCO2();
  uint8_t retHeight;
  sprintf(buffer, "%05d\n", co2);
  writePartial(buffer, x, y, 2, &retHeight, false);
  y += retHeight + 3;
  co2 = co2 > range ? range : co2;
  int co2Width = co2/(int)resolution;

  display.setPartialWindow(x, y, width , height);
  display.firstPage();
  do {
    display.fillRect(x, y, width, height, GxEPD_WHITE);
    display.drawRect(x, y, width, height, GxEPD_BLACK);
    display.fillRect(x, y, co2Width, height, GxEPD_BLACK);;
  } while (display.nextPage());
}

void drawLables() {
  lableIcon = true;
  writePartial("int", 50, 2, 1, nullptr, false);
  writePartial("ext", 250, 2, 1, nullptr, false);
  drawTempAndHumidValues(sensorPoint, 0.0, 0);
  drawCo2Values(co2ValuesPoint, display.width()-(10*2), 10);
  drawAirQuality(NULL, pollutionPoint);
  drawHourForecast(0, forecastPoint);
  drawTempAndHumidValues(extTermIgroPoint, 0, 0);
  lableIcon = false;
}

void UpdateAirPollution(){
  uint64_t currTime = timeClient.getEpochTime();
  if (currTime >= nextAirRequest) {
    nextAirRequest = currTime + REFRESH_TIME_AQI;
    AirQuality* aqi;
    if(debug){
      aqi = (AirQuality*)malloc(sizeof(AirQuality));
      *aqi = AirQuality();
    }else{
      aqi = weather.GetAirPollution();
    }
    if(aqi != NULL)
        drawAirQuality(aqi, pollutionPoint);
  }
}

void UpdateForecast(){
  if(forecasts == NULL || timeClient.getEpochTime() > forecasts[currForecastIdx].timeStamp.getUnix()){
    //aggiorno dati oltre le 9 ore: 2 *
    if(forecasts == NULL || currForecastIdx >=3) {
      if(debug){
        //DEBUG
        forecasts = (Forecast*)malloc(4 * sizeof(Forecast));
        forecasts[0] = Forecast(SUN_01D, 23.2, 24.54, 30);
        forecasts[1] = Forecast(MOON_02N, 15.2, 24.54, 30);
        forecasts[2] = Forecast(CLOUDSUN_02D, 25.2, 24.54, 30);
        forecasts[3] = Forecast(SHOWERRAIN_09, 23.2, 24.54, 30);
      }else{
        forecasts = weather.GetForecast(8);
      }
      currForecastIdx = 0;
    }
    drawHourForecast(currForecastIdx, forecastPoint);
    currForecastIdx ++;
  }
}

void setPoints(){
  sensorPoint.x = 10;
  sensorPoint.y = 15;

  extTermIgroPoint.x = 155;
  extTermIgroPoint.y = 15;

  forecastPoint.x = 22;
  forecastPoint.y = 170;

  pollutionPoint.x = 290;
  pollutionPoint.y = 25;

  co2ValuesPoint.x = 5;
  co2ValuesPoint.y = 105;
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

void setup() {
  Serial.begin(115200);
  Wire.begin();
  airSensor.begin(Wire);
  airSensor.setAltitudeCompensation(18);
  airSensor.setAutoSelfCalibration(true);
  initDisplay();

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect("WeatherStation");
  if(WiFi.isConnected())
    timeClient.begin();
  timeClient.setUpdateInterval(600000);//10 minuti

  //esp_client.setCACert(ca_cert);
  esp_client.setInsecure();
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setKeepAlive(60);
  mqtt_client.setCallback(mqttCallback);
  connectToMQTT();

  setPoints();

  drawLables();
  drawHourForecast(0, forecastPoint);
}

void updateSensorValues(){
  if (millis() > nextSensorRequest && airSensor.dataAvailable())  {
    nextSensorRequest = millis() + 20000;
    drawTempAndHumidValues(sensorPoint, airSensor.getTemperature(), (int)airSensor.getHumidity());
    drawCo2Values(co2ValuesPoint, display.width()-(10*2), 15);
  }
}

void loop() {
  //SENSORS
  updateSensorValues();
  //AQI
  UpdateAirPollution();
  //FORECAST
  UpdateForecast();
  //MQTT
  if (!mqtt_client.connected())
    connectToMQTT();
  mqtt_client.loop();
  //EXT_SENSOR
  if(extAcquired && timeClient.getEpochTime() > extTermIgro.dt + 1200000){//20 muinuti
    drawTempAndHumidValues(extTermIgroPoint, -200, -100);
    extAcquired = false;
  }
}