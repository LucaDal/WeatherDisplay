#ifndef AIR_QUALITY
#define AIR_QUALITY

#include <ArduinoJson.h>
enum AQI{
    Good = 1,
    Fair = 2,
    Moderate = 3,
    Poor = 5
};

class AirQuality{
    public:
    uint16_t pm2_5;
    uint16_t pm10;
    uint64_t time;
    AQI aqi;
    AirQuality(JsonDocument json);
    //test
    AirQuality();
    String AQIToString();
};

#endif