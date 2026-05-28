#include "WeatherAPI.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

WeatherAPI::WeatherAPI(String API_KEY, String lat, String lon) {

    this->API_KEY = API_KEY;
    this->lat = lat;
    this->lon = lon;
    client.setInsecure();
}

bool WeatherAPI::connecClient(String URL, JsonDocument &data, size_t capacity){
    (void)capacity;
    data.clear();
    if (!WiFi.isConnected())
        return false;
    if (!client.connect(server.c_str(), 443)) {
        Serial.println("Connection failed");
        return false;
    }

    //Serial.println("GET " + URL + " HTTP/1.0");
    client.println("GET " + URL + " HTTP/1.0");
    client.println("Host: " + this->server);
    client.println("Connection: close");
    client.println();

    if(!client.connected())
        return false;
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            //Serial.println("headers received");
            break;
        }
    }

    String jsonData = client.readString();
    DeserializationError err = deserializeJson(data, jsonData);
    if (err) {
        Serial.printf("deserializeJson failed: %s\n", err.f_str());
        return false;
    }

    return true;
}
/// @brief return forecast of the next 5 days with 3 hours span
///
/// @return
Forecast * WeatherAPI::GetForecast(size_t maxRequest) {

    if (!WiFi.isConnected()) {
        forecastsCount = 0;
        return NULL;
    }

    forecastsCount = 0;
    String URL = "https://" + this->server +
        "/data/2.5/forecast?lat=" + this->lat +
        "&lon=" + this->lon +
        "&units=metric" +
        "&lang=it" +
        "&appid=" + this->API_KEY;

    // The API returns many items, reserve enough memory for full payload
    JsonDocument data;
    if(!connecClient(URL, data, 32768)){
        Serial.println("returned datra is null");
        return NULL;
    }

    const char *apiCityName = data["city"]["name"].as<const char *>();
    if (apiCityName != nullptr)
        cityName = apiCityName;

    size_t Cnt = data["cnt"].as<size_t>();
    Cnt = maxRequest < Cnt ? maxRequest : Cnt;
    Forecast *newForecasts = (Forecast *) malloc(sizeof(Forecast) * Cnt);
    if (newForecasts == NULL) {
        forecastsCount = 0;
        return NULL;
    }
    for (size_t idx = 0; idx < Cnt; idx++ ){
        newForecasts[idx] = Forecast(data["list"][idx].as<JsonObject>());
    }

    forecastsCount = Cnt;
    if(this->forecsts != NULL)
        free(this->forecsts);
    this->forecsts = newForecasts;
    return this->forecsts;
}

size_t WeatherAPI::GetForecastCount(){
    return forecastsCount;
}

const char * WeatherAPI::GetCityName() const {
    return cityName.c_str();
}

AirQuality * WeatherAPI::GetAirPollution(){
    if (!WiFi.isConnected())
        return NULL;

    String URL = "https://" + this->server +
        "/data/2.5/air_pollution?lat=" + this->lat +
        "&lon=" + this->lon +
        "&appid=" + this->API_KEY;

    JsonDocument data;
    if(!connecClient(URL, data, 4096))
        return this->airQuality;
    if(this->airQuality != nullptr)
        free(this->airQuality);
    this->airQuality = (AirQuality *) malloc(sizeof(AirQuality));
    *this->airQuality = AirQuality(data["list"][0].as<JsonObject>());
    return this->airQuality;
}
