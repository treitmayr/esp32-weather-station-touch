// SPDX-FileCopyrightText: 2023 ThingPulse Ltd., https://thingpulse.com
// SPDX-License-Identifier: MIT

#include <esp_bt.h>
#include <LittleFS.h>

#include <OpenFontRender.h>
#include <TJpg_Decoder.h>

#include "fonts/open-sans.h"
#include "GfxUi.h"

#include <JsonListener.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>
#include <SunMoonCalc.h>

#include "connectivity.h"
#include "display.h"
#include "persistence.h"
#include "settings.h"
#include "util.h"



// ----------------------------------------------------------------------------
// Globals
// ----------------------------------------------------------------------------
OpenFontRender ofr;
FT6236 ts = FT6236(TFT_HEIGHT, TFT_WIDTH);
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite timeSprite = TFT_eSprite(&tft);
GfxUi ui = GfxUi(&tft, &ofr);

// time management variables
int updateIntervalMillis = UPDATE_INTERVAL_MINUTES * 60 * 1000;
unsigned long lastTimeSyncMillis = 0;
unsigned long lastUpdateMillis = 0;
static unsigned long timeUpdate = millis();
static unsigned long lastTouchedTime = millis();

int16_t centerWidth;

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapForecastData forecasts[NUMBER_OF_FORECASTS];


// ----------------------------------------------------------------------------
// Function prototypes (declarations)
// ----------------------------------------------------------------------------
static void drawProgress(const char *text, int8_t percentage);
static void drawTimeAndDate();
static String getWeatherIconName(uint16_t id, bool today);
static void initJpegDecoder();
static void initOpenFontRender();
static bool pushImageToTft(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);
static void syncTime();
static void repaint();
static void updateData(boolean updateProgressBar);


// ----------------------------------------------------------------------------
// setup() & loop()
// ----------------------------------------------------------------------------
void setup(void) {
  Serial.begin(115200);
  delay(1000);

  logBanner();
  logMemoryStats();

  startWiFi();

  initJpegDecoder();
  initTouchScreen(&ts);
  initTft(&tft);
  centerWidth = tft.width() / 2;
  timeSprite.createSprite(320, 83);
  logDisplayDebugInfo(&tft);

  initFileSystem();
  initOpenFontRender();

  waitWifiStarted();
  syncTime();

  unsigned long currentMillis = millis();
  timeUpdate = currentMillis;
  lastTouchedTime = currentMillis;
}

void loop(void) {
  unsigned long currentMillis = millis();
  unsigned long dly;

#ifdef TFT_SLEEP_DELAY_SECONDS
  dly = 50;

  if (ts.touched() > 0) {
    lastTouchedTime = currentMillis;
    log_i("TFT touched");
    if (!isTftAwake()) {
      tftSleepOut(&tft);
      log_i("TFT woke up");
    }
  } else if (isTftAwake() &&
             (currentMillis > lastTouchedTime + TFT_SLEEP_DELAY_SECONDS * 1000)) {
      log_i("TFT going to sleep");
      tftSleepIn(&tft);
  }
#endif

  if (isTftAwake()) {
    dly = 1000;
    // update if
    // - never (successfully) updated before OR
    // - last sync too far back
    if (lastTimeSyncMillis == 0 ||
        lastUpdateMillis == 0 ||
        (currentMillis - lastUpdateMillis) > updateIntervalMillis) {
      repaint();
    } else {
      drawTimeAndDate();
    }
  }
  // make sure to not extend the interval by drawing time
  currentMillis = millis();
  while (timeUpdate <= currentMillis) {
    timeUpdate += dly;
  }
  delay(timeUpdate - currentMillis);

  // if (ts.touched()) {
  //   TS_Point p = ts.getPoint();

  //   uint16_t touchX = p.x;
  //   uint16_t touchY = p.y;

  //   log_d("Touch coordinates: x=%d, y=%d", touchX, touchY);
  //   // Debouncing; avoid returning the same touch multiple times.
  //   delay(50);
  // }
}

// ----------------------------------------------------------------------------
// Functions
// ----------------------------------------------------------------------------

static void drawAstro(uint16_t top, uint16_t left, uint16_t right) {
  const uint16_t width = right - left;
  const uint16_t center = left + width / 2;

  time_t tnow = time(nullptr);
  struct tm *nowUtc = gmtime(&tnow);

  SunMoonCalc smCalc = SunMoonCalc(mkgmtime(nowUtc), currentWeather.lat, currentWeather.lon);
  const SunMoonCalc::Result result = smCalc.calculateSunAndMoonData();

  uint32_t sunCenter = (width / 2 - 37) / 2;
  uint32_t moonCenter = width - sunCenter;
  sunCenter += left;
  moonCenter += left;

  ofr.setFontSize(24);
  ofr.cdrawString(SUN_MOON_LABEL[0].c_str(), sunCenter, top + 5);
  ofr.cdrawString(SUN_MOON_LABEL[1].c_str(), moonCenter, top + 5);

  ofr.setFontSize(18);
  // Sun
  strftime(timestampBuffer, 26, UI_TIME_FORMAT_NO_SECONDS, localtime(&result.sun.rise));
  ofr.cdrawString(timestampBuffer, sunCenter, top + 40);
  strftime(timestampBuffer, 26, UI_TIME_FORMAT_NO_SECONDS, localtime(&result.sun.set));
  ofr.cdrawString(timestampBuffer, sunCenter, top + 65);

  // Moon
  strftime(timestampBuffer, 26, UI_TIME_FORMAT_NO_SECONDS, localtime(&result.moon.rise));
  ofr.cdrawString(timestampBuffer, moonCenter, top + 40);
  strftime(timestampBuffer, 26, UI_TIME_FORMAT_NO_SECONDS, localtime(&result.moon.set));
  ofr.cdrawString(timestampBuffer, moonCenter, top + 65);

  // Moon icon
  int imageIndex = round(result.moon.age * NUMBER_OF_MOON_IMAGES / LUNAR_MONTH);
  if (imageIndex == NUMBER_OF_MOON_IMAGES) imageIndex = NUMBER_OF_MOON_IMAGES - 1;
  ui.drawBmp("/moon/m-phase-" + String(imageIndex) + ".bmp", center - 37, top + 10);

  if (tft.width() < tft.height()) {
    ofr.setFontSize(14);
    ofr.cdrawString(MOON_PHASES[result.moon.phase.index].c_str(), center, top + 95);
  }

  log_i("Moon phase: %s, illumination: %f, age: %f -> image index: %d",
        result.moon.phase.name.c_str(), result.moon.illumination, result.moon.age, imageIndex);
}

static void drawCurrentWeather(uint16_t top, uint16_t left, uint16_t right) {
  const uint16_t width = right - left;
  const uint16_t center = left + width / 2;
  // re-use variable throughout function
  String text = "";

  // icon
  String weatherIcon = getWeatherIconName(currentWeather.weatherId, true);
  ui.drawBmp("/weather/" + weatherIcon + ".bmp", left + 5, top + 35);
  // tft.drawRect(left + 5, top + 30, 100, 100, 0x4228);

  // condition string
  ofr.setFontSize(24);
  ofr.cdrawString(currentWeather.description.c_str(), center, top + 5);

  // temperature incl. symbol
  String temp = String(currentWeather.temp, 1) + "°";
  if (OPEN_WEATHER_MAP_LANGUAGE == "de") {
    temp.replace('.', ',');
  }

  int windAngleIndex = round(currentWeather.windDeg * 8 / 360);
  if (windAngleIndex > 7) {
    windAngleIndex = 0;
  }

  String windSpeed = IS_METRIC ? String(currentWeather.windSpeed * 3.6, 0) + " km/h"
                               : String(currentWeather.windSpeed, 0) + " mph";

  if (width >= 300) {
    // temperature, slightly shifted to the right to find better balance due to the ° symbol
    ofr.setFontSize(48);
    ofr.cdrawString(temp.c_str(), center + 10, top + 30);

    ofr.setFontSize(18);

    // humidity
    text = String(currentWeather.humidity) + " %";
    ofr.cdrawString(text.c_str(), center, top + 88);

    // pressure
    text = String(currentWeather.pressure) + " hPa";
    ofr.cdrawString(text.c_str(), center, top + 110);

    // wind rose icon
    ui.drawBmp("/wind/" + WIND_ICON_NAMES[windAngleIndex] + ".bmp", right - 80, top + 35);
    // tft.drawRect(right - 80, top + 35, 75, 75, 0x4228);

    // wind speed
    ofr.cdrawString(windSpeed.c_str(), right - 43, top + 110);
  } else {
    int rulerX = right - 75;

    // temperature, slightly shifted to the right to find better balance due to the ° symbol
    ofr.setFontSize(48);
    ofr.cdrawString(temp.c_str(), rulerX + 10, top + 30);

    ofr.setFontSize(18);

    // humidity
    text = String(currentWeather.humidity) + " %";
    ofr.cdrawString(text.c_str(), rulerX, top + 88);

    // wind speed
    text = windSpeed + ", " + WIND_DIR_NAMES[windAngleIndex];
    ofr.cdrawString(text.c_str(), rulerX, top + 110);
  }
}

static void drawTodaysForecast(uint16_t top, uint16_t left, uint16_t right) {
  static const uint16_t fillColor = tft.color565(0x66, 0x55, 0x00);
  static const uint16_t lineColor = tft.color565(0x77, 0x77, 0x11);

  // padding
  left += 10;
  right -= 10;

  const uint16_t height = 50;
  const uint16_t width = right - left;
  const uint16_t center = left + width / 2;
  char ctext[16];

  ofr.setFontSize(12);

  int numberOfForecasts = min((unsigned int)NUMBER_OF_CLOSE_FORECASTS, width / (ofr.getTextWidth("%s", "00:00 ")));
  int distX = width / (numberOfForecasts - 1);
  float minTemp = 1000;
  float maxTemp = -1000;

  for (int i = 0; i < numberOfForecasts; i++) {
    minTemp = min(minTemp, forecasts[i].temp);
    maxTemp = max(maxTemp, forecasts[i].temp);
  }
  float factorY = (maxTemp > minTemp) ? (height - 11) / (maxTemp - minTemp) : 1;

  for (int i = 0; i < numberOfForecasts; i++) {
    if (i > 0) {
      int32_t x1 = left + (i - 1) * distX;
      int32_t y1 = top + height;
      int32_t x2 = x1;
      int32_t y2 = y1 - (int32_t)round((forecasts[i - 1].temp - minTemp) * factorY) - 1;
      int32_t x3 = x2 + distX;
      int32_t y3 = y1 - (int32_t)round((forecasts[i].temp - minTemp) * factorY) - 1;
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, fillColor);
      tft.fillTriangle(x1, y1, x3, y3, x3, y1, fillColor);
      tft.drawLine(x2, y2, x3, y3, lineColor);

      // time
      time_t forecastTimeUtc = forecasts[i - 1].observationTime;
      struct tm *forecastLocalTime = localtime(&forecastTimeUtc);
      snprintf(ctext, sizeof(ctext), "%02d:%02d", forecastLocalTime->tm_hour, forecastLocalTime->tm_min);
      ofr.drawString(ctext, x1, y1 + 1);

      // temperature
      snprintf(ctext, sizeof(ctext), "%.0f°", forecasts[i - 1].temp);
      ofr.drawString(ctext, x2, y2 - 18);
    }
  }
}

static void drawForecast(uint16_t top, uint16_t left, uint16_t right) {
  const uint16_t width = right - left;
  const uint16_t center = left + width / 2;

  int numberOfDays = min(NUMBER_OF_DAY_FORECASTS, width / 70);

  DayForecast* dayForecasts = calculateDayForecasts(forecasts);
  for (int i = 0; i < numberOfDays; i++) {
    log_i("[%d] condition code: %d, hour: %d, temp: %.1f/%.1f", dayForecasts[i].day,
          dayForecasts[i].conditionCode, dayForecasts[i].conditionHour, dayForecasts[i].minTemp,
          dayForecasts[i].maxTemp);
  }

  int singleWidthHalf = width / (2 * numberOfDays);
  for (int i = 0; i < numberOfDays; i++) {
    int x = left + singleWidthHalf * ((i * 2) + 1);
    ofr.setFontSize(24);
    ofr.cdrawString(WEEKDAYS_ABBR[dayForecasts[i].day].c_str(), x, top + 5);
    ofr.setFontSize(18);
    String minTemp(dayForecasts[i].minTemp, 0);
    String maxTemp(dayForecasts[i].maxTemp, 0);
    if (minTemp != maxTemp) {
      ofr.cdrawString(String(minTemp + "-" + maxTemp + "°").c_str(), x, top + 37);
    } else {
      ofr.cdrawString(String(minTemp + "°").c_str(), x, top + 37);
    }
    ui.drawBmp("/weather-small/" + getWeatherIconName(dayForecasts[i].conditionCode, false) + ".bmp", x - 25, top + 65);
  }
}

static void drawProgress(const char *text, int8_t percentage) {
  ofr.setFontSize(24);
  int pbWidth = tft.width() - 100;
  int pbX = (tft.width() - pbWidth)/2;
  int pbY = 260;
  int progressTextY = 210;

  tft.fillRect(0, progressTextY, tft.width(), 40, TFT_BLACK);
  ofr.cdrawString(text, centerWidth, progressTextY);
  ui.drawProgressBar(pbX, pbY, pbWidth, 15, percentage, TFT_WHITE, TFT_TP_BLUE);
}

static void drawHorizSeparator(uint16_t y) {
  const int16_t padding = 10;
  tft.drawFastHLine(padding, y, tft.width() - 2 * padding, 0x4228);
}

static void drawHorizSeparator(uint16_t x, uint16_t width, uint16_t y) {
  const int16_t padding = 10;
  tft.drawFastHLine(x + padding, y, width - 2 * padding, 0x4228);
}

static void drawVertSeparator(uint16_t x, uint16_t y, uint16_t height) {
  const int16_t padding = 10;
  tft.drawFastVLine(x, y + padding, height - 2 * padding, 0x4228);
}

static void drawTimeAndDate() {
  static uint32_t timeOffsetX = 0;
  int16_t centerSpriteWidth = timeSprite.width() / 2;

  timeSprite.fillSprite(TFT_BLACK);
  ofr.setDrawer(timeSprite);

  // Date
  ofr.setFontSize(16);
  ofr.cdrawString(
    String(WEEKDAYS[getCurrentWeekday()] + ", " + getCurrentTimestamp(UI_DATE_FORMAT)).c_str(),
    centerSpriteWidth,
    0
  );

  // Time
  ofr.setFontSize(53);  // 48
  if (timeOffsetX == 0) {
#ifdef UI_TIME_FORMAT_CENTER
    timeOffsetX = ofr.getTextWidth("%s", getCurrentTimestamp(UI_TIME_FORMAT_CENTER).c_str());
#else
    timeOffsetX = ofr.getTextWidth("%s", getCurrentTimestamp(UI_TIME_FORMAT).c_str());
#endif
    timeOffsetX = centerSpriteWidth - timeOffsetX / 2;
  }
  ofr.drawString(getCurrentTimestamp(UI_TIME_FORMAT).c_str(), timeOffsetX, 15);
  timeSprite.pushSprite(centerWidth - centerSpriteWidth, 5);

  // set the drawer back since we temporarily changed it to the time sprite above
  ofr.setDrawer(tft);
}

static String getWeatherIconName(uint16_t id, bool today) {
  // Weather condition codes: https://openweathermap.org/weather-conditions#Weather-Condition-Codes-2

  // For the 8xx group we also have night versions of the icons.
  if (today && id/100 == 8 &&
      (currentWeather.observationTime < currentWeather.sunrise ||
       currentWeather.observationTime > currentWeather.sunset)) {
    id += 1000;
  }

  if (id/100 == 2) return "thunderstorm";
  if (id/100 == 3) return "drizzle";
  if (id == 500) return "light-rain";
  if (id == 504) return "extrem-rain";
  else if (id == 511) return "sleet";
  else if (id/100 == 5) return "rain";
  if (id >= 611 && id <= 616) return "sleet";
  else if (id/100 == 6) return "snow";
  if (id/100 == 7) return "fog";
  if (id == 800) return "clear-day";
  if (id >= 801 && id <= 803) return "partly-cloudy-day";
  else if (id/100 == 8) return "cloudy";
  // night icons
  if (id == 1800) return "clear-night";
  if (id == 1801) return "partly-cloudy-night";
  else if (id/100 == 18) return "cloudy";

  return "unknown";
}

static void initJpegDecoder() {
    // The JPEG image can be scaled by a factor of 1, 2, 4, or 8 (default: 0)
  TJpgDec.setJpgScale(1);
  // The decoder must be given the exact name of the rendering function
  TJpgDec.setCallback(pushImageToTft);
}

static void initOpenFontRender() {
  ofr.loadFont(opensans, sizeof(opensans));
  ofr.setDrawer(tft);
  ofr.setFontColor(TFT_WHITE);
  ofr.setBackgroundColor(TFT_BLACK);
}

// Function will be called as a callback during decoding of a JPEG file to
// render each block to the TFT.
static bool pushImageToTft(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  // Stop further decoding as image is running off bottom of screen
  if (y >= tft.height()) {
    return 0;
  }

  // Automatically clips the image block rendering at the TFT boundaries.
  tft.pushImage(x, y, w, h, bitmap);

  // Return 1 to decode next block
  return 1;
}

static void syncTime() {
  if (initTime()) {
    lastTimeSyncMillis = millis();
    setTimezone(TIMEZONE);
    log_i("Current local time: %s", getCurrentTimestamp(SYSTEM_TIMESTAMP_FORMAT).c_str());
  }
}

#if 0
static void repaint_original_unused() {
  tft.fillScreen(TFT_BLACK);
  ui.drawLogo();

  ofr.setFontSize(16);
  ofr.cdrawString(APP_NAME, centerWidth, tft.height() - 50);
  ofr.cdrawString(VERSION, centerWidth, tft.height() - 30);

  drawProgress("Starting WiFi...", 10);
  if (WiFi.status() != WL_CONNECTED) {
    startWiFi();
  }

  drawProgress("Synchronizing time...", 30);
  syncTime();

  updateData(true);

  drawProgress("Ready", 100);
  lastUpdateMillis = millis();

  tft.fillScreen(TFT_BLACK);

  drawTimeAndDate();
  drawHorizSeparator(90);

  drawCurrentWeather();
  drawHorizSeparator(230);

  drawForecast();
  drawHorizSeparator(355);

  drawAstro();
}
#endif

static void repaint() {
  wakeModemSleep();

  tft.fillRect(0, 91, tft.width(), tft.height(), TFT_BLACK);

  drawTimeAndDate();
  drawHorizSeparator(90);

  updateData(false);
  lastUpdateMillis = millis();

  if (tft.width() < tft.height()) {
    drawCurrentWeather(90, 0, tft.width());
    drawHorizSeparator(230);
    drawForecast(230, 0, tft.width());
    drawHorizSeparator(355);
    drawAstro(360, 0, tft.width());
  } else {
    drawVertSeparator(centerWidth, 90, tft.height() - 90);
    drawCurrentWeather(90, 0, centerWidth);
    //drawHorizSeparator(0, centerWidth, 240);
    drawTodaysForecast(240, 0, centerWidth);
    drawForecast(90, centerWidth, tft.width());
    drawHorizSeparator(centerWidth, centerWidth, 210);
    drawAstro(220, centerWidth, tft.width());
  }

  delay(100);
  setModemSleep();
}

static void updateData(boolean updateProgressBar) {
  if(updateProgressBar) drawProgress("Updating weather...", 70);
  OpenWeatherMapCurrent *currentWeatherClient = new OpenWeatherMapCurrent();
  currentWeatherClient->setMetric(IS_METRIC);
  currentWeatherClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient->updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_API_KEY, OPEN_WEATHER_MAP_LOCATION_ID);
  delete currentWeatherClient;
  currentWeatherClient = nullptr;
  log_i("Current weather in %s: %s, %.1f °C", currentWeather.cityName, currentWeather.description.c_str(), currentWeather.feelsLike);

  if(updateProgressBar) drawProgress("Updating forecast...", 90);
  OpenWeatherMapForecast *forecastClient = new OpenWeatherMapForecast();
  forecastClient->setMetric(IS_METRIC);
  forecastClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  forecastClient->setAllowedHours(forecastHoursUtc, sizeof(forecastHoursUtc));
  forecastClient->updateForecastsById(forecasts, OPEN_WEATHER_MAP_API_KEY, OPEN_WEATHER_MAP_LOCATION_ID, NUMBER_OF_FORECASTS);
  delete forecastClient;
  forecastClient = nullptr;
}
