#ifndef WEATHER_API
#define WEATHER_API

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include "Forecast.h"
#include "AirQuality.h"

class WeatherAPI {
    private:
        String API_KEY;
        String lat;
        String lon;
        WiFiClientSecure client;
        String server = "api.openweathermap.org";
        AirQuality * airQuality = NULL;
        bool connecClient(String URL, JsonDocument &data, size_t capacity);
        size_t forecastsCount = 0;

        public:
        Forecast * forecsts = NULL;
        WeatherAPI(String API_KEY, String lat, String lon);
        //3hoursFor5Day
        Forecast * GetForecast(size_t maxRequest);
        size_t GetForecastCount();
        AirQuality * GetAirPollution();
};

#endif
