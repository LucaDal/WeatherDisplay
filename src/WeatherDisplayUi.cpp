#include "WeatherDisplayUi.h"

#include <Fonts/FreeSans12pt7b.h>

#include "Icons.h"
#include "icons/IconPack.h"

namespace {
constexpr uint8_t kForecastIconSize = 64;
constexpr uint8_t kForecastSpacing = 5;
constexpr uint8_t kForecastHourBlockH = 24;
constexpr uint8_t kForecastFontSize = 2;
constexpr uint8_t kForecastHourFontSize = 2;
constexpr uint8_t kForecastContextIconOffset = 15;
constexpr uint8_t kForecastColumnWidth = 102;
constexpr uint8_t kClockFontSize = 3;

const char *co2Status(uint16_t co2) {
    if (co2 == 0)
        return "--";
    if (co2 < 800)
        return "GOOD";
    if (co2 < 1000)
        return "OK";
    if (co2 < 1400)
        return "HIGH";
    return "DANGER";
}

const char *co2Trend(int16_t deltaPpm, bool hasPreviousValue) {
    if (!hasPreviousValue)
        return nullptr;
    if (deltaPpm >= 100)
        return "UP";
    if (deltaPpm <= -100)
        return "DOWN";
    return nullptr;
}
} // namespace

WeatherDisplayUi::WeatherDisplayUi(WeatherPanel &display) : display(display) {}

void WeatherDisplayUi::begin() {
    display.init(115200, true, 50, false);
    display.setRotation(4);
    display.setTextColor(GxEPD_BLACK);
    display.setTextWrap(false);
}

uint16_t WeatherDisplayUi::contentWidth(uint16_t horizontalMargin) const {
    const uint16_t screenWidth = display.width();
    if (horizontalMargin * 2 >= screenWidth)
        return 0;
    return screenWidth - (horizontalMargin * 2);
}

void WeatherDisplayUi::markUpdated() {
    lastDisplayUpdateMs = millis();
    powerOffPending = true;
}

void WeatherDisplayUi::updatePowerState(unsigned long nowMs,
                                        unsigned long idleMs) {
    if (!powerOffPending || nowMs - lastDisplayUpdateMs < idleMs)
        return;

    display.powerOff();
    powerOffPending = false;
}

void WeatherDisplayUi::drawCenteredStatus(const char *text) {
    int16_t x1, y1;
    uint16_t width, height;
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds(text, 0, 0, &x1, &y1, &width, &height);

    const int16_t x = (display.width() - width) / 2 - x1;
    const int16_t y = (display.height() - height) / 2 - y1;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(x, y);
        display.print(text);
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawText(const char *text, int16_t x, int16_t y,
                                uint8_t textSize, uint16_t *retHeight) {
    int16_t x1, y1;
    uint16_t width, height;
    display.setFont(nullptr);
    display.setTextSize(textSize);
    display.getTextBounds(text, x, y, &x1, &y1, &width, &height);
    if (retHeight != nullptr)
        *retHeight = height;

    display.setCursor(x, y);
    display.print(text);
}

void WeatherDisplayUi::drawBitmapIcon(const uint8_t *image, size_t x, size_t y,
                                      size_t iconSize) {
    if (image == nullptr)
        return;

    const size_t bytesPerRow = (iconSize + 7) / 8;
    for (size_t row = 0; row < iconSize; row++) {
        for (size_t col = 0; col < iconSize; col++) {
            const uint8_t value =
                pgm_read_byte(image + (row * bytesPerRow) + col / 8);
            if ((value & (0x80 >> (col % 8))) == 0)
                display.drawPixel(x + col, y + row, GxEPD_BLACK);
        }
    }
}

void WeatherDisplayUi::drawStaticLayout(DisplayPoint sensorPoint,
                                        DisplayPoint forecastPoint,
                                        DisplayPoint pollutionPoint,
                                        DisplayPoint extTermIgroPoint,
                                        DisplayPoint co2ValuesPoint,
                                        uint8_t forecastSlots) {
    drawingStaticLayout = true;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawText("INT", sensorPoint.x + 42, 4, 1);
        drawText("EXT", extTermIgroPoint.x + 42, 4, 1);
        drawTempHumidity(sensorPoint, 0.0, 0);
        drawCo2(co2ValuesPoint, contentWidth(10), 0, 0, false);
        drawAirQualityLabels(pollutionPoint);
        drawForecast(nullptr, 0, 0, forecastPoint, forecastSlots);
        drawTempHumidity(extTermIgroPoint, -200, -100);
    } while (display.nextPage());

    drawingStaticLayout = false;
    markUpdated();
}

void WeatherDisplayUi::drawForecastContextIcons(int16_t x, int16_t y,
                                                uint8_t iconSize,
                                                uint8_t spacing,
                                                uint8_t contextIconOffset) {
    y += iconSize;
    drawBitmapIcon(THERMOMETER, x - contextIconOffset, y += spacing, 16);
    drawBitmapIcon(HUMAN, x - contextIconOffset, y += 16 + spacing, 16);
}

void WeatherDisplayUi::drawForecast(Forecast *forecasts, size_t forecastsCount,
                                    uint8_t startIndex, DisplayPoint point,
                                    uint8_t forecastSlots) {
    if (drawingStaticLayout) {
        for (size_t i = 0; i < forecastSlots; i++) {
            const int16_t x = point.x + (i * kForecastColumnWidth);
            drawForecastContextIcons(x, point.y, kForecastIconSize,
                                     kForecastSpacing,
                                     kForecastContextIconOffset);
        }
        return;
    }

    if (forecasts == nullptr || forecastsCount == 0 ||
        startIndex >= forecastsCount)
        return;

    size_t end = startIndex + forecastSlots;
    if (end > forecastsCount)
        end = forecastsCount;

    for (size_t i = startIndex; i < end; i++) {
        const size_t column = i - startIndex;
        const int iconX = point.x + (column * kForecastColumnWidth);
        const int iconY = point.y;
        int windowLeft = iconX - kForecastContextIconOffset;
        if (windowLeft < 0)
            windowLeft = 0;
        int windowTop = iconY - kForecastHourBlockH;
        if (windowTop < 0)
            windowTop = 0;

        uint16_t windowW =
            kForecastColumnWidth + (uint16_t)(iconX - windowLeft);
        if (windowLeft + windowW > display.width())
            windowW = display.width() - windowLeft;

        const int lineHeight = 8 * kForecastFontSize;
        const int textBlockH = (lineHeight * 2) + kForecastSpacing;
        const uint16_t windowH =
            (iconY - windowTop) + kForecastIconSize + kForecastSpacing +
            textBlockH;

        display.setPartialWindow(windowLeft, windowTop, windowW, windowH);
        display.firstPage();
        do {
            char buffer[10];
            display.fillRect(windowLeft, windowTop, windowW, windowH,
                             GxEPD_WHITE);
            drawForecastContextIcons(iconX, point.y, kForecastIconSize,
                                     kForecastSpacing,
                                     kForecastContextIconOffset);

            snprintf(buffer, sizeof(buffer), "%02d:%02d",
                     forecasts[i].timeStamp.hour,
                     forecasts[i].timeStamp.minute);
            drawText(buffer, iconX + 4, windowTop + 4,
                     kForecastHourFontSize);
            drawBitmapIcon(forecasts[i].icon, iconX, iconY);

            const int textX = iconX + 15;
            const int textY = iconY + kForecastIconSize + kForecastSpacing;
            snprintf(buffer, sizeof(buffer), "%.1f", forecasts[i].temp);
            drawText(buffer, textX, textY, kForecastFontSize);
            snprintf(buffer, sizeof(buffer), "%.1f",
                     forecasts[i].percivedTemp);
            drawText(buffer, textX, textY + lineHeight + kForecastSpacing,
                     kForecastFontSize);
        } while (display.nextPage());
    }

    markUpdated();
}

void WeatherDisplayUi::drawTempHumidity(DisplayPoint point, double temp,
                                        int humid, bool showHumidity) {
    constexpr uint8_t spacing = 10;
    constexpr uint8_t fontSize = 4;
    constexpr uint8_t xLabelOffset = 105;
    constexpr uint8_t yLabelOffset = 5;
    const uint16_t x = point.x;
    uint16_t y = point.y;

    if (drawingStaticLayout) {
        drawBitmapIcon(THERMOMETER, x + xLabelOffset, y + yLabelOffset, 16);
        if (showHumidity) {
            drawBitmapIcon(HUMIDITY, x + xLabelOffset,
                           y + 32 + spacing + yLabelOffset, 16);
        }
        return;
    }

    char tempBuf[8];
    char humidBuf[8];
    if (temp > -100)
        snprintf(tempBuf, sizeof(tempBuf), "%02.1f", temp);
    else
        snprintf(tempBuf, sizeof(tempBuf), "----");

    if (showHumidity && humid > -1)
        snprintf(humidBuf, sizeof(humidBuf), "%02d", humid);
    else
        snprintf(humidBuf, sizeof(humidBuf), "----");

    int16_t x1, y1;
    uint16_t w, h;
    display.setFont(nullptr);
    display.setTextSize(fontSize);
    display.getTextBounds(tempBuf, x, y, &x1, &y1, &w, &h);
    uint16_t maxW = w;
    const uint16_t lineH = h;
    if (showHumidity) {
        display.getTextBounds(humidBuf, x, y, &x1, &y1, &w, &h);
        if (w > maxW)
            maxW = w;
    }

    const uint16_t windowW = maxW + 2;
    const uint16_t lineCount = showHumidity ? 2 : 1;
    const uint16_t windowH =
        (lineH * lineCount) + (showHumidity ? spacing : 0) + 2;

    display.setPartialWindow(x, y, windowW, windowH);
    display.firstPage();
    do {
        uint16_t retHeight = 0;
        display.fillRect(x, y, windowW, windowH, GxEPD_WHITE);
        drawText(tempBuf, x, y, fontSize, &retHeight);
        if (showHumidity)
            drawText(humidBuf, x, y + lineH + spacing, fontSize, &retHeight);
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawAirQualityLabels(DisplayPoint point) {
    constexpr uint8_t space = 5;
    uint16_t x = point.x;
    uint16_t y = point.y;
    uint16_t retHeight = 0;

    drawText("AQI", x, y, 2, &retHeight);
    y += space + retHeight;
    drawText("pm2", x, y, 2, &retHeight);
    y += space + retHeight;
    drawText("pm10", x, y, 2, &retHeight);
}

void WeatherDisplayUi::drawAirQuality(AirQuality *aqi, DisplayPoint point) {
    constexpr uint8_t space = 5;
    constexpr uint8_t xLabelOffset = 60;
    const uint16_t x = point.x;
    const uint16_t y = point.y;

    if (drawingStaticLayout) {
        drawAirQualityLabels(point);
        return;
    }
    if (aqi == nullptr)
        return;

    const uint16_t valueX = x + xLabelOffset;
    if (valueX >= display.width())
        return;

    uint16_t windowW = display.width() - valueX;
    if (windowW > 70)
        windowW = 70;
    const uint16_t windowH = (8 * 2 * 3) + (space * 2);

    display.setPartialWindow(valueX, y, windowW, windowH);
    display.firstPage();
    do {
        char buffer[10];
        uint16_t retHeight = 0;
        display.fillRect(valueX, y, windowW, windowH, GxEPD_WHITE);
        drawText(aqi->AQIToString().c_str(), valueX, y, 2, &retHeight);
        snprintf(buffer, sizeof(buffer), "%d", aqi->pm2_5);
        drawText(buffer, valueX, y + retHeight + space, 2, &retHeight);
        snprintf(buffer, sizeof(buffer), "%d", aqi->pm10);
        drawText(buffer, valueX, y + (retHeight * 2) + (space * 2), 2,
                 &retHeight);
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawCo2(DisplayPoint point, uint16_t width,
                               uint16_t co2, int16_t deltaPpm,
                               bool hasPreviousValue) {
    if (width == 0)
        return;

    const int x = point.x;
    const int y = point.y;

    char line[40];
    if (co2 == 0) {
        snprintf(line, sizeof(line), "CO2 ---- ppm  --");
    } else if (const char *trend = co2Trend(deltaPpm, hasPreviousValue)) {
        snprintf(line, sizeof(line), "CO2 %u ppm  %s %+d  %s", (unsigned)co2,
                 trend, (int)deltaPpm, co2Status(co2));
    } else {
        snprintf(line, sizeof(line), "CO2 %u ppm  %s", (unsigned)co2,
                 co2Status(co2));
    }

    int16_t x1, y1;
    uint16_t textW, textH;
    display.setFont(nullptr);
    display.setTextSize(2);
    display.getTextBounds(line, x, y, &x1, &y1, &textW, &textH);

    if (drawingStaticLayout) {
        drawText(line, x, y, 2);
        return;
    }

    int16_t windowTop = y1 - 1;
    if (windowTop < 0)
        windowTop = 0;
    const uint16_t windowH = textH + 4;
    display.setPartialWindow(x, windowTop, width, windowH);
    display.firstPage();
    do {
        display.fillRect(x, windowTop, width, windowH, GxEPD_WHITE);
        drawText(line, x, y, 2);
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawClock(DisplayPoint point, const char *text) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setFont(nullptr);
    display.setTextSize(kClockFontSize);
    display.getTextBounds(text, point.x, point.y, &x1, &y1, &w, &h);

    int16_t rectX = x1 - 1;
    int16_t rectY = y1 - 1;
    uint16_t rectW = w + 2;
    uint16_t rectH = h + 2;
    if (rectX < 0) {
        rectW += rectX;
        rectX = 0;
    }
    if (rectY < 0) {
        rectH += rectY;
        rectY = 0;
    }

    display.setPartialWindow(rectX, rectY, rectW, rectH);
    display.firstPage();
    do {
        display.fillRect(rectX, rectY, rectW, rectH, GxEPD_WHITE);
        display.setFont(nullptr);
        display.setTextSize(kClockFontSize);
        display.setCursor(point.x, point.y);
        display.print(text);
    } while (display.nextPage());

    markUpdated();
}
