#include "WeatherAPI.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

WeatherAPI::WeatherAPI(String API_KEY, String lat, String lon) {

    this->API_KEY = API_KEY;
    this->lat = lat;
    this->lon = lon;
    client.setInsecure();
}

JsonDocument WeatherAPI::connecClient(String URL){

    JsonDocument data;
    
    if (!client.connect(server.c_str(), 443)) {
        Serial.println("Connection failed");
        return data;
    }

    //Serial.println("GET " + URL + " HTTP/1.0");
    client.println("GET " + URL + " HTTP/1.0");
    client.println("Host: " + this->server);
    client.println("Connection: close");
    client.println();

    if(!client.connected())
        return data;
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            //Serial.println("headers received");
            break;
        }
    }

    String jsonData = client.readString();
    deserializeJson(data, jsonData);

    return data;
}
/// @brief return forecast of the next 5 days with 3 hours span
///         
/// @return 
Forecast * WeatherAPI::GetForecast(size_t maxRequest) {

    if (!WiFi.isConnected())
        return NULL;
    
    String URL = "https://" + this->server + 
        "/data/2.5/forecast?lat=" + this->lat +
        "&lon=" + this->lon +
        "&units=metric" +
        "&lang=it" +
        "&appid=" + this->API_KEY;

    JsonDocument data = connecClient(URL);
    
    if(data.isNull()){
        Serial.println("returned datra is null");
        return NULL;
    }

    size_t Cnt = data["cnt"].as<size_t>();
    Cnt = maxRequest < Cnt ? maxRequest : Cnt;
    //libero la memoria precedentemente utilizzata
    if(this->forecsts != NULL)
        free(this->forecsts);
    //creo gli oggetti
    this->forecsts = (Forecast *) malloc(sizeof(Forecast) * Cnt);
    for (size_t idx = 0; idx < Cnt; idx++ ){
        forecsts[idx] = Forecast(data["list"][idx].as<JsonObject>());
    }
    return this->forecsts;
}

AirQuality * WeatherAPI::GetAirPollution(){
    if (!WiFi.isConnected())
        return this->airQuality;
        
    String URL = "https://" + this->server + 
        "/data/2.5/air_pollution?lat=" + this->lat +
        "&lon=" + this->lon +
        "&appid=" + this->API_KEY;

    JsonDocument data = connecClient(URL);
    
    if(data.isNull())
        return this->airQuality;
    if(this->airQuality != nullptr)
        free(this->airQuality);
    this->airQuality = (AirQuality *) malloc(sizeof(AirQuality));
    *this->airQuality = AirQuality(data["list"][0].as<JsonObject>());
    return this->airQuality;
}