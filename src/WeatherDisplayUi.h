#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>

#include "WeatherAPI.h"

struct DisplayPoint {
    int x;
    int y;
};

using WeatherPanel =
    GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>;

class WeatherDisplayUi {
  public:
    explicit WeatherDisplayUi(WeatherPanel &display);

    /// Initializes the e-paper panel with the rotation and text defaults used by
    /// the dashboard.
    void begin();

    /// Draws a full-screen startup/error message. The next static layout refresh
    /// intentionally clears it.
    void drawCenteredStatus(const char *text);

    /// Performs the only full-screen refresh used after boot. All static labels
    /// and icons are drawn in the same page cycle to avoid slow icon-by-icon
    /// updates.
    void drawStaticLayout(DisplayPoint sensorPoint, DisplayPoint forecastPoint,
                          DisplayPoint pollutionPoint,
                          DisplayPoint extTermIgroPoint,
                          DisplayPoint co2ValuesPoint,
                          uint8_t forecastSlots);

    /// Updates the forecast area in a single partial refresh so all visible
    /// columns change together.
    void drawForecast(Forecast *forecasts, size_t forecastsCount,
                      uint8_t startIndex, DisplayPoint point,
                      uint8_t forecastSlots);
    void drawForecastStatus(DisplayPoint point, uint8_t forecastSlots,
                            const char *text);

    void drawTempHumidity(DisplayPoint point, double temp, int humid,
                          bool showHumidity = true);
    void drawAirQuality(AirQuality *aqi, DisplayPoint point);
    void drawCo2(DisplayPoint point, uint16_t width, uint16_t co2,
                 int16_t deltaPpm, bool hasPreviousValue);
    void drawCityName(const char *name);
    void drawClock(DisplayPoint point, const char *text);

    /// Powers the panel off after the last partial/full refresh has been idle
    /// long enough. Any later partial refresh wakes it again through the driver.
    void updatePowerState(unsigned long nowMs, unsigned long idleMs);

    uint16_t contentWidth(uint16_t horizontalMargin) const;

  private:
    void markUpdated();
    uint16_t textWidth(const char *text, const GFXfont *font,
                       uint16_t *retHeight = nullptr);
    uint16_t temperatureTextWidth(const char *text, const GFXfont *font);
    void drawText(const char *text, int16_t x, int16_t y,
                  const GFXfont *font, uint16_t *retHeight = nullptr);
    void drawTemperatureText(const char *text, int16_t x, int16_t y,
                             const GFXfont *font);
    void drawBitmapIcon(const uint8_t *image, size_t x, size_t y,
                        size_t iconSize = 64);
    void drawCenteredTextInWindow(const char *text, int16_t windowX,
                                  int16_t y, uint16_t windowW,
                                  const GFXfont *font,
                                  uint16_t *retHeight = nullptr);
    void drawLayoutGuides(DisplayPoint sensorPoint, DisplayPoint forecastPoint,
                          DisplayPoint pollutionPoint,
                          DisplayPoint extTermIgroPoint,
                          DisplayPoint co2ValuesPoint);
    void drawAirQualityLabels(DisplayPoint point);

    WeatherPanel &display;
    bool drawingStaticLayout = false;
    unsigned long lastDisplayUpdateMs = 0;
    bool powerOffPending = false;
};
