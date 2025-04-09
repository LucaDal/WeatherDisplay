#include "SimpleOTA.h"
#include "WiFiManager.h"
SimpleOTA * simpleOTA  = new SimpleOTA();

void setup() {
    Serial.begin(115200);
    Serial.println("test");
  int EEPROMsize = 64;
  //connect the board to internet first
  //if you want an https connection set useTLS = true:
  //otherwise:
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect("WeatherStation");
  simpleOTA->begin(EEPROMsize,"lucadalessandro.freeddns.org","WEATHER_DISPLAY", true);
}

void loop() {
  //check update every 30 seconds
  Serial.println("test");

  simpleOTA->checkUpdates(10); 
  // put your main code here, to run repeatedly:
  delay(5000);
}