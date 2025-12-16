#include "AirQuality.h"

AirQuality::AirQuality(JsonDocument json){
    uint8_t retAqi = json["main"]["aqi"].as<uint8_t>(); 
    this->aqi = static_cast<AQI>(retAqi);
    this->pm2_5 = json["components"]["pm2_5"].as<uint16_t>();
    this->pm10 = json["components"]["pm10"].as<uint16_t>();
    this->time = json["dt"].as<uint64_t>(); 
}
AirQuality::AirQuality(){
    this->aqi = static_cast<AQI>(1);
    this->pm2_5 = 2;
    this->pm10 = 17;
    this->time = 17000;
}
String AirQuality::AQIToString(){
    switch (this->aqi){
        case AQI::Good:
            return "Good";
            break;
        case AQI::Fair:
            return "Fair";
            break;
        case AQI::Moderate:
            return "Mod.";
            break;
        case AQI::Poor:
            return "Poor";
            break;
        default:
            break;
    }
    return "";
}
//api response:
// {
//     "coord": {
//         "lon": 11.071,
//         "lat": 44.8948
//     },
//     "list": [
//         {
//             "main": {
//                 "aqi": 2
//             },
//             "components": {
//                 "co": 340.46,
//                 "no": 0,
//                 "no2": 5.83,
//                 "o3": 85.83,
//                 "so2": 0.63,
//                 "pm2_5": 14.95,
//                 "pm10": 17.46,
//                 "nh3": 13.81
//             },
//             "dt": 1742664982
//         }
//     ]
// }