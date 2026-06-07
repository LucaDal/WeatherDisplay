#include "WeatherDisplayUi.h"

#include <math.h>

#include "Icons.h"
#include "Roboto_Medium12pt7b.h"
#include "Roboto_Medium18pt7b.h"
#include "Roboto_Medium9pt7b.h"
#include "icons/IconPack.h"

namespace {
constexpr uint8_t kForecastIconSize = 64;
constexpr uint8_t kForecastSpacing = 5;
constexpr uint8_t kForecastHourBlockH = 24;
constexpr uint8_t kForecastTopLineY = 100;
constexpr uint8_t kForecastContextIconOffset = 15;
constexpr uint8_t kForecastColumnWidth = 102;
constexpr uint8_t kCo2IconSize = 24;
constexpr uint8_t kCo2IconGap = 10;
constexpr uint8_t kCo2BarHeight = 10;
constexpr uint8_t kCo2BarMargin = 10;
constexpr uint16_t kCo2DisplayMax = 2000;
constexpr uint8_t kClockBandBottomY = 32;
const GFXfont *const kFontTiny = &Roboto_Medium9pt7b;
const GFXfont *const kFontSmall = &Roboto_Medium12pt7b;
const GFXfont *const kFontMedium = &Roboto_Medium18pt7b;

const char *co2Status(uint16_t co2) {
    if (co2 == 0)
        return "--";
    if (co2 < 800)
        return "GOOD";
    if (co2 < 1000)
        return "OK";
    if (co2 < 1400)
        return "HIGH";
    return "BAD";
}

uint16_t co2BarFillWidth(uint16_t co2, uint16_t barWidth) {
    if (co2 == 0 || barWidth == 0)
        return 0;
    if (co2 >= kCo2DisplayMax)
        return barWidth;
    return (uint32_t)co2 * barWidth / kCo2DisplayMax;
}

void drawCo2ThresholdTick(WeatherPanel &display, int16_t x, int16_t y,
                          uint16_t barWidth, uint16_t threshold) {
    const int16_t tickX = x + ((uint32_t)threshold * barWidth / kCo2DisplayMax);
    display.drawFastVLine(tickX, y - 2, kCo2BarHeight + 4, GxEPD_BLACK);
}

int16_t roundedForecastTemperature(double value) {
    const double lower = floor(value);
    return value <= lower + 0.5 ? (int16_t)lower : (int16_t)(lower + 1);
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
    display.setFont(kFontMedium);
    display.setTextSize(1);
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

uint16_t WeatherDisplayUi::textWidth(const char *text, const GFXfont *font,
                                     uint16_t *retHeight) {
    int16_t x1, y1;
    uint16_t width, height;
    display.setFont(font);
    display.setTextSize(1);
    display.getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
    if (retHeight != nullptr)
        *retHeight = height;
    return width;
}

uint16_t WeatherDisplayUi::temperatureTextWidth(const char *text,
                                                const GFXfont *font) {
    const uint8_t radius = font == kFontMedium ? 3 : 2;
    return textWidth(text, font) + (radius * 2) + 4;
}

void WeatherDisplayUi::drawText(const char *text, int16_t x, int16_t y,
                                const GFXfont *font, uint16_t *retHeight) {
    int16_t x1, y1;
    uint16_t width, height;
    display.setFont(font);
    display.setTextSize(1);
    display.getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
    if (retHeight != nullptr)
        *retHeight = height;

    display.setCursor(x - x1, y - y1);
    display.print(text);
}

void WeatherDisplayUi::drawTemperatureText(const char *text, int16_t x,
                                           int16_t y, const GFXfont *font) {
    const uint16_t width = textWidth(text, font);
    drawText(text, x, y, font);

    const int16_t radius = font == kFontMedium ? 3 : 2;
    const int16_t degreeX = x + width + radius + 2;
    const int16_t degreeY = y + radius + 2;
    display.drawCircle(degreeX, degreeY, radius, GxEPD_BLACK);
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

void WeatherDisplayUi::drawCenteredTextInWindow(const char *text,
                                                int16_t windowX, int16_t y,
                                                uint16_t windowW,
                                                const GFXfont *font,
                                                uint16_t *retHeight) {
    const uint16_t textW = textWidth(text, font, retHeight);
    const int16_t textX =
        windowX + (windowW > textW ? (int16_t)((windowW - textW) / 2) : 0);
    drawText(text, textX, y, font);
}

void WeatherDisplayUi::drawLayoutGuides(DisplayPoint sensorPoint,
                                        DisplayPoint forecastPoint,
                                        DisplayPoint pollutionPoint,
                                        DisplayPoint extTermIgroPoint,
                                        DisplayPoint co2ValuesPoint) {
    const int16_t topBottom = kForecastTopLineY;
    const int16_t forecastLineY =
        co2ValuesPoint.y > 6 ? co2ValuesPoint.y - 6 : co2ValuesPoint.y;
    const int16_t topMargin = sensorPoint.x > 0 ? sensorPoint.x : 10;
    const int16_t firstSplit =
        extTermIgroPoint.x > topMargin ? extTermIgroPoint.x - topMargin
                                       : extTermIgroPoint.x;
    const int16_t secondSplit =
        pollutionPoint.x > topMargin + 2 ? pollutionPoint.x - topMargin - 2
                                         : pollutionPoint.x;
    const uint16_t screenW = display.width();

    display.drawFastHLine(0, topBottom, screenW, GxEPD_BLACK);
    display.drawFastHLine(0, forecastLineY, screenW, GxEPD_BLACK);

    if (firstSplit > 0 && firstSplit < screenW)
        display.drawFastVLine(firstSplit, 0, topBottom, GxEPD_BLACK);
    if (secondSplit > 0 && secondSplit < screenW)
        display.drawFastVLine(secondSplit, 0, topBottom, GxEPD_BLACK);
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
        drawLayoutGuides(sensorPoint, forecastPoint, pollutionPoint,
                         extTermIgroPoint, co2ValuesPoint);
        drawCenteredTextInWindow("Room", 0, 4,
                                 extTermIgroPoint.x - sensorPoint.x,
                                 kFontTiny);
        drawTempHumidity(sensorPoint, 0.0, 0);
        drawCo2(co2ValuesPoint, contentWidth(10), 0, 0, false);
        drawAirQualityLabels(pollutionPoint);
        drawForecast(nullptr, 0, 0, forecastPoint, forecastSlots);
        drawTempHumidity(extTermIgroPoint, -200, -100);
    } while (display.nextPage());

    drawingStaticLayout = false;
    markUpdated();
}

void WeatherDisplayUi::drawForecast(Forecast *forecasts, size_t forecastsCount,
                                    uint8_t startIndex, DisplayPoint point,
                                    uint8_t forecastSlots) {
    if (drawingStaticLayout)
        return;

    if (forecasts == nullptr || forecastsCount == 0 ||
        startIndex >= forecastsCount)
        return;

    size_t end = startIndex + forecastSlots;
    if (end > forecastsCount)
        end = forecastsCount;

    const size_t visibleCount = end - startIndex;
    const int firstIconX = point.x;
    const int iconY = point.y;
    int windowLeft = firstIconX - kForecastContextIconOffset;
    if (windowLeft < 0)
        windowLeft = 0;
    int windowTop = iconY - kForecastHourBlockH;
    if (windowTop < 0)
        windowTop = 0;

    uint16_t windowW =
        (uint16_t)((visibleCount * kForecastColumnWidth) +
                   (firstIconX - windowLeft));
    if (windowLeft + windowW > display.width())
        windowW = display.width() - windowLeft;

    uint16_t lineHeight = 0;
    textWidth("00:00", kFontTiny, &lineHeight);
    const int textBlockH = (lineHeight * 2) + kForecastSpacing;
    const uint16_t windowH =
        (iconY - windowTop) + kForecastIconSize + kForecastSpacing +
        textBlockH;

    display.setPartialWindow(windowLeft, windowTop, windowW, windowH);
    display.firstPage();
    do {
        display.fillRect(windowLeft, windowTop, windowW, windowH, GxEPD_WHITE);
        for (size_t i = startIndex; i < end; i++) {
            const size_t column = i - startIndex;
            const int iconX = point.x + (column * kForecastColumnWidth);
            char buffer[10];

            snprintf(buffer, sizeof(buffer), "%02d:%02d",
                     forecasts[i].timeStamp.hour,
                     forecasts[i].timeStamp.minute);
            drawCenteredTextInWindow(buffer, iconX, windowTop + 3,
                                     kForecastIconSize, kFontTiny);
            drawBitmapIcon(forecasts[i].icon, iconX, iconY);

            const int textY = iconY + kForecastIconSize + kForecastSpacing;
            char tempBuffer[8];
            char humidityBuffer[8];
            snprintf(tempBuffer, sizeof(tempBuffer), "%d",
                     roundedForecastTemperature(forecasts[i].temp));
            snprintf(humidityBuffer, sizeof(humidityBuffer), "%d%%",
                     forecasts[i].humidity);

            const uint16_t tempW =
                temperatureTextWidth(tempBuffer, kFontTiny);
            const uint16_t humidityW = textWidth(humidityBuffer, kFontTiny);
            uint16_t rowW = 16 + kForecastSpacing + tempW;
            const uint16_t humidityRowW = 16 + kForecastSpacing + humidityW;
            if (humidityRowW > rowW)
                rowW = humidityRowW;

            const int rowX =
                iconX + (kForecastIconSize > rowW
                             ? (int)((kForecastIconSize - rowW) / 2)
                             : 0);
            const int valueX = rowX + 16 + kForecastSpacing;
            const int16_t iconOffsetY =
                lineHeight > 16 ? (int16_t)((lineHeight - 16) / 2) : 0;
            drawBitmapIcon(THERMOMETER, rowX, textY + iconOffsetY, 16);
            drawTemperatureText(tempBuffer, valueX, textY, kFontTiny);
            drawBitmapIcon(HUMIDITY, rowX,
                           textY + lineHeight + kForecastSpacing + iconOffsetY,
                           16);
            drawText(humidityBuffer, valueX,
                     textY + lineHeight + kForecastSpacing, kFontTiny);
        }
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawForecastStatus(DisplayPoint point,
                                          uint8_t forecastSlots,
                                          const char *text) {
    if (text == nullptr || text[0] == '\0')
        return;

    int windowLeft = point.x - kForecastContextIconOffset;
    if (windowLeft < 0)
        windowLeft = 0;

    int windowTop = point.y - kForecastHourBlockH;
    if (windowTop < 0)
        windowTop = 0;

    uint16_t windowW =
        (uint16_t)((forecastSlots * kForecastColumnWidth) +
                   (point.x - windowLeft));
    if (windowLeft + windowW > display.width())
        windowW = display.width() - windowLeft;

    uint16_t lineHeight = 0;
    textWidth(text, kFontSmall, &lineHeight);
    const uint16_t windowH =
        kForecastHourBlockH + kForecastIconSize + kForecastSpacing +
        (lineHeight * 2) + kForecastSpacing;
    const int16_t textY =
        windowTop + (int16_t)((windowH - lineHeight) / 2);

    display.setPartialWindow(windowLeft, windowTop, windowW, windowH);
    display.firstPage();
    do {
        display.fillRect(windowLeft, windowTop, windowW, windowH, GxEPD_WHITE);
        drawCenteredTextInWindow(text, windowLeft, textY, windowW, kFontSmall);
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawTempHumidity(DisplayPoint point, double temp,
                                        int humid, bool showHumidity) {
    constexpr uint8_t spacing = 8;
    constexpr uint16_t valueWindowW = 118;
    const uint16_t x = point.x;
    uint16_t y = point.y;

    if (drawingStaticLayout)
        return;

    char tempBuf[8];
    char humidBuf[8];
    const bool hasTemp = temp > -100;
    if (hasTemp)
        snprintf(tempBuf, sizeof(tempBuf), "%02.1f", temp);
    else
        snprintf(tempBuf, sizeof(tempBuf), "----");

    if (showHumidity && humid > -1)
        snprintf(humidBuf, sizeof(humidBuf), "%02d%%", humid);
    else
        snprintf(humidBuf, sizeof(humidBuf), "----");

    uint16_t lineH = 0;
    const uint16_t tempW =
        hasTemp ? temperatureTextWidth(tempBuf, kFontMedium)
                : textWidth(tempBuf, kFontMedium, &lineH);
    if (lineH == 0)
        textWidth(tempBuf, kFontMedium, &lineH);
    const uint16_t humidW = textWidth(humidBuf, kFontMedium);

    const uint16_t windowW = valueWindowW;
    const uint16_t lineCount = showHumidity ? 2 : 1;
    const uint16_t windowH =
        (lineH * lineCount) + (showHumidity ? spacing : 0) + 2;

    display.setPartialWindow(x, y, windowW, windowH);
    display.firstPage();
    do {
        uint16_t retHeight = 0;
        display.fillRect(x, y, windowW, windowH, GxEPD_WHITE);
        const int16_t tempX = x + (windowW > tempW ? (windowW - tempW) / 2 : 0);
        if (hasTemp)
            drawTemperatureText(tempBuf, tempX, y, kFontMedium);
        else
            drawText(tempBuf, tempX, y, kFontMedium, &retHeight);
        if (showHumidity) {
            const int16_t humidX =
                x + (windowW > humidW ? (windowW - humidW) / 2 : 0);
            drawText(humidBuf, humidX, y + lineH + spacing, kFontMedium,
                     &retHeight);
        }
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawAirQualityLabels(DisplayPoint point) {
    constexpr uint8_t space = 5;
    constexpr uint8_t horizontalMargin = 12;
    uint16_t x = point.x;
    uint16_t y = point.y;
    const uint16_t windowW = display.width() - x;
    const int16_t valueRight = x + windowW - horizontalMargin;
    uint16_t retHeight = 0;
    const uint16_t emptyW = textWidth("--", kFontTiny);

    drawText("AQI", x, y, kFontTiny, &retHeight);
    drawText("--", valueRight - emptyW, y, kFontTiny);
    y += space + retHeight;
    drawText("pm2", x, y, kFontTiny, &retHeight);
    drawText("--", valueRight - emptyW, y, kFontTiny);
    y += space + retHeight;
    drawText("pm10", x, y, kFontTiny, &retHeight);
    drawText("--", valueRight - emptyW, y, kFontTiny);
}

void WeatherDisplayUi::drawAirQuality(AirQuality *aqi, DisplayPoint point) {
    constexpr uint8_t space = 5;
    constexpr uint8_t horizontalMargin = 12;
    const uint16_t x = point.x;
    const uint16_t y = point.y;

    if (drawingStaticLayout)
        return;
    if (aqi == nullptr)
        return;

    const uint16_t windowW = display.width() - x;
    uint16_t lineH = 0;
    textWidth("AQI", kFontTiny, &lineH);
    const uint16_t windowH = (lineH * 3) + (space * 2) + 2;

    display.setPartialWindow(x, y, windowW, windowH);
    display.firstPage();
    do {
        char buffer[10];
        char valueBuffer[12];
        uint16_t retHeight = 0;
        const int16_t valueRight = x + windowW - horizontalMargin;
        display.fillRect(x, y, windowW, windowH, GxEPD_WHITE);

        snprintf(valueBuffer, sizeof(valueBuffer), "%s",
                 aqi->AQIToString().c_str());
        drawText("AQI", x, y, kFontTiny, &retHeight);
        drawText(valueBuffer, valueRight - textWidth(valueBuffer, kFontTiny),
                 y, kFontTiny);

        const uint16_t secondLineY = y + retHeight + space;
        drawText("pm2", x, secondLineY, kFontTiny, &retHeight);
        snprintf(buffer, sizeof(buffer), "%d", aqi->pm2_5);
        drawText(buffer, valueRight - textWidth(buffer, kFontTiny),
                 secondLineY, kFontTiny);

        const uint16_t thirdLineY = y + (retHeight * 2) + (space * 2);
        drawText("pm10", x, thirdLineY, kFontTiny, &retHeight);
        snprintf(buffer, sizeof(buffer), "%d", aqi->pm10);
        drawText(buffer, valueRight - textWidth(buffer, kFontTiny),
                 thirdLineY, kFontTiny);
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawCo2(DisplayPoint point, uint16_t width,
                               uint16_t co2, int16_t deltaPpm,
                               bool hasPreviousValue) {
    if (width == 0)
        return;

    (void)deltaPpm;
    (void)hasPreviousValue;

    const int x = point.x;
    const int y = point.y;

    if (drawingStaticLayout) {
        drawBitmapIcon(CO2_24, x, y + 6, kCo2IconSize);
        return;
    }

    const int16_t windowX = 0;
    const uint16_t windowW = display.width();
    const int16_t valueX = x + kCo2IconSize + kCo2IconGap;
    if (valueX >= display.width())
        return;

    const int16_t barX = kCo2BarMargin;
    const int16_t barY = display.height() - kCo2BarMargin - kCo2BarHeight;
    const uint16_t barW = display.width() - (kCo2BarMargin * 2);
    const uint16_t windowH = display.height() - y;
    const int16_t rowBottom = barY - 5;
    const int16_t rowH = rowBottom > y ? rowBottom - y : kCo2IconSize;

    display.setPartialWindow(windowX, y, windowW, windowH);
    display.firstPage();
    do {
        char valueBuffer[8];
        char infoBuffer[24];
        display.fillRect(windowX, y, windowW, windowH, GxEPD_WHITE);

        if (co2 == 0)
            snprintf(valueBuffer, sizeof(valueBuffer), "----");
        else
            snprintf(valueBuffer, sizeof(valueBuffer), "%u", (unsigned)co2);

        snprintf(infoBuffer, sizeof(infoBuffer), "%s", co2Status(co2));

        uint16_t valueH = 0;
        const uint16_t valueW = textWidth(valueBuffer, kFontMedium, &valueH);
        uint16_t ppmH = 0;
        const uint16_t ppmW = textWidth("ppm", kFontTiny, &ppmH);
        uint16_t infoH = 0;
        const uint16_t infoW = textWidth(infoBuffer, kFontTiny, &infoH);
        int16_t infoX = display.width() - kCo2BarMargin - infoW;

        const int16_t iconY =
            y + (rowH > kCo2IconSize ? (rowH - kCo2IconSize) / 2 : 0);
        const int16_t valueY = y + (rowH > valueH ? (rowH - valueH) / 2 : 0);
        const int16_t infoY = y + (rowH > infoH ? (rowH - infoH) / 2 : 0);
        int16_t ppmX = valueX + valueW + 5;
        const int16_t maxPpmX = infoX - (int16_t)ppmW - 10;
        if (ppmX > maxPpmX)
            ppmX = maxPpmX;
        const int16_t ppmY = valueY + (valueH > ppmH ? valueH - ppmH : 0);

        drawBitmapIcon(CO2_24, x, iconY, kCo2IconSize);
        drawText(valueBuffer, valueX, valueY, kFontMedium);
        drawText("ppm", ppmX, ppmY, kFontTiny);
        drawText(infoBuffer, infoX, infoY, kFontTiny);

        display.drawRect(barX, barY, barW, kCo2BarHeight, GxEPD_BLACK);
        const uint16_t fillW = co2BarFillWidth(co2, barW);
        if (fillW > 2)
            display.fillRect(barX + 1, barY + 1, fillW - 2,
                             kCo2BarHeight - 2, GxEPD_BLACK);
        drawCo2ThresholdTick(display, barX, barY, barW, 800);
        drawCo2ThresholdTick(display, barX, barY, barW, 1000);
        drawCo2ThresholdTick(display, barX, barY, barW, 1400);
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawCityName(const char *name) {
    if (name == nullptr || name[0] == '\0')
        return;

    char buffer[22];
    snprintf(buffer, sizeof(buffer), "%s", name);

    uint16_t lineH = 0;
    const uint16_t cityW = textWidth(buffer, kFontTiny, &lineH);
    uint16_t windowW = 128;
    if (windowW > display.width())
        windowW = display.width();
    const int16_t windowX = (display.width() - windowW) / 2;
    const int16_t windowY = 3;
    const uint16_t windowH = 16;
    const int16_t textX = windowX + (windowW > cityW ? (windowW - cityW) / 2 : 0);
    const int16_t textY =
        windowY + (windowH > lineH ? (int16_t)((windowH - lineH) / 2) : 0);

    display.setPartialWindow(windowX, windowY, windowW, windowH);
    display.firstPage();
    do {
        display.fillRect(windowX, windowY, windowW, windowH, GxEPD_WHITE);
        drawText(buffer, textX, textY, kFontTiny);
    } while (display.nextPage());

    markUpdated();
}

void WeatherDisplayUi::drawClock(DisplayPoint point, const char *text) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setFont(kFontSmall);
    display.setTextSize(1);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

    int16_t rectX = point.x;
    int16_t rectY = 0;
    uint16_t rectW = display.width() - rectX;
    uint16_t rectH = kClockBandBottomY;
    const int16_t textX = rectX + (rectW > w ? (int16_t)((rectW - w) / 2) : 0);
    const int16_t textY =
        rectY + (rectH > h ? (int16_t)((rectH - h) / 2) : 0);

    display.setPartialWindow(rectX, rectY, rectW, rectH);
    display.firstPage();
    do {
        display.fillRect(rectX, rectY, rectW, rectH, GxEPD_WHITE);
        drawText(text, textX, textY, kFontSmall);
    } while (display.nextPage());

    markUpdated();
}
