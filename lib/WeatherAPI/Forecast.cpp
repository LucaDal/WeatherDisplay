#include "Forecast.h"
//debug purpose
Forecast::Forecast(const unsigned char * icon, double temp, double percivedTemp, int humidity){
    this->icon = icon;
    this->temp = temp;
    this->percivedTemp = percivedTemp;
    this->humidity = humidity;
    this->timeStamp.setDateTime(2025, 4, 7, 21, 20, 15);
}

Forecast::Forecast(JsonDocument data){
    this->icon = getIconPointer(data["weather"][0]["icon"].as<const char*>());
    this->temp = data["main"]["temp"].as<double>();
    this->percivedTemp = data["main"]["feels_like"].as<double>();
    this->humidity = data["main"]["humidity"].as<int>();
    this->windSpeed = data["wind"]["speed"].as<int>();
    this->timeStamp.getDateTime(data["dt"].as<unsigned long>());
}

const uint8_t * Forecast::getIconPointer(const char* iconName){
    if(iconName == NULL)
        return nullptr;
    if(strcmp(iconName, "01d") == 0)
        return SUN_01D;
    
    if(strcmp(iconName, "01n") == 0 )
        return MOON_02N;
    
    if(strcmp(iconName, "02d") == 0 )
        return CLOUDSUN_02D;
    
    if(strcmp(iconName, "02n") == 0 )
        return CLOUDMOON_02N;
    
    if(strcmp(iconName, "03d") == 0 || strcmp(iconName, "03n") == 0 )
        return CLOUD_03;
    
    if(strcmp(iconName, "04d") == 0 || strcmp(iconName, "04n") == 0 )
        return BROKENCLOUD_04;
    
    if(strcmp(iconName, "09d") == 0 || strcmp(iconName, "09n") == 0 )
        return SHOWERRAIN_09;
    
    if(strcmp(iconName, "10d") == 0 )
        return RAINSUN_10D;
    
    if(strcmp(iconName, "10n") == 0 )
        return RAINMOON_10N;
    
    if(strcmp(iconName, "11d") == 0 || strcmp(iconName, "11n") == 0 )
        return THUNDERSTORM_11;
    
    if(strcmp(iconName, "13d") == 0 || strcmp(iconName, "13n") == 0 )
        return SNOW_13;
    
    if(strcmp(iconName, "50d") == 0 || strcmp(iconName, "50n") == 0 )
        return MIST_50;
    return nullptr;
}
