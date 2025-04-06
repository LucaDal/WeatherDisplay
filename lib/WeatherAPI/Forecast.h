#ifndef FORECAST
#define FORECAST

#include <Arduino.h>
#include <ArduinoJson.h>
#include <UnixTime.h>
#include "icons/IconPack.h"

class Forecast{
    public:
        const unsigned char * icon;
        double temp;
        double percivedTemp;
        int humidity;
        double windSpeed;
        UnixTime timeStamp = UnixTime(3);
        //json recived from the api
        Forecast(JsonDocument data);
        Forecast(const unsigned char * icon, double temp, double percivedTemp, int humidity);
    private:
        const uint8_t * getIconPointer(const char iconName);
};

#endif