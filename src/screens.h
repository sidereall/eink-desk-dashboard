// screens.h — the data the screens read, and the functions that draw them.
#pragma once
#include "config.h"
#include <Adafruit_GFX.h>

enum ScreenId : uint8_t {
  SCREEN_CLOCK = 0,
  SCREEN_TASKS,   // 1
  SCREEN_WEATHER, // 2
  SCREEN_MARKETS, // 3

  // = 4. Everything above is in the rotation.
  SCREEN_ROTATION_COUNT,

  // Also 4, so `% SCREEN_ROTATION_COUNT` can never land on INFO.
  SCREEN_INFO = SCREEN_ROTATION_COUNT,
  SCREEN_COUNT // = 5
};

enum WxIcon : uint8_t { WX_CLEAR = 0, WX_PARTLY_CLOUDY, WX_OVERCAST, WX_RAIN, WX_THUNDERSTORM, WX_SNOW };

struct Task {
  char title[TASK_TITLE_MAX];
  bool done;
};

struct WeatherDay {
  char name[4]; // e.g. "MON"
  int8_t temp;  // Given day's max temperature
  uint8_t icon; // Weather icon (WxIcon)
};

// 78 = one NYSE session (6.5h / 5min)
// 79 = plus yesterday's close, so the line starts from the % change baseline
constexpr uint8_t MARKET_SPARK_MAX = 79;

struct MarketData {
  bool configured; // API key entered
  bool valid;      // fetched at least once
  bool stale;      // last fetch is x hours old
  bool marketOpen; // from the API is_market_open flag

  char name[12];      // e.g. "SPY"
  char price[12];     // e.g. "741.23"
  char changePct[10]; // e.g. "0.32%", since the previous close
  bool rising;        // change up or down

  char updated[6]; // "HH:MM" of the last good fetch

  // The day's prices, oldest first
  float spark[MARKET_SPARK_MAX];
  uint8_t sparkCount;

  // Set the top and bottom of the chart. Equal values mean a flat day, no chart.
  float sparkMin;
  float sparkMax;
};

struct WeatherData {
  bool configured; // location set on the web page
  bool valid;      // fetched at least once
  bool stale;      // last fetch is x hours old

  char location[WX_NAME_MAX]; // e.g. "BERLIN"
  char status[20];            // e.g. "PARTLY CLOUDY"
  char updated[6];            // "HH:MM" of the last good fetch

  int8_t tempNow;
  int8_t tempHigh; // today's high
  int8_t tempLow;  // today's low
  uint8_t iconNow; // Weather icon (WxIcon)

  WeatherDay days[WX_CARD_COUNT]; // the next four days
};

// Everything the screens draw from. Each subsystem fills its own section.
struct AppState {
  // TIME (clock_time.cpp) ---------------------------------------------------------------------
  bool timeSynced; // false until NTP fetched
  uint8_t hour;
  uint8_t minute;
  bool showDate;              // date line on the clock screen, set on the web page
  char clockDate[32];         // e.g. "Fri, 6 Mar 2026"
  char tasksDate[32];         // e.g. "FRIDAY, 6 MAR"
  char tzLabel[TZ_LABEL_MAX]; // shown on INFO

  // TASKS (tasks.cpp) --------------------------------------------------------------------------
  Task tasks[MAX_TASKS];
  uint8_t taskCount;

  // NETWORK (wifi_net.cpp) ---------------------------------------------------------------------
  bool wifiConnected;
  char ip[16]; // e.g. "192.168.1.10"
  char ssid[33];
  char security[12]; // negotiated mode: "WPA3", "WPA2/WPA3"
  int8_t rssi;       // dBm
  uint32_t uptimeSec;

  // WEATHER (Open-Meteo; weather.cpp) -------------------------------------------------------
  WeatherData weather;

  // MARKETS (Twelve Data; markets.cpp) ------------------------------------------------------
  MarketData markets;

  // DISPLAY (main.cpp) -------------------------------------------------------
  bool darkMode; // inverts the panel
};

struct Rect {
  int16_t x, y, w, h;
};

// The area to redraw when Wi-Fi changes: just the icon on most screens.
// A bigger area on INFO, which also shows the IP, SSID, security and signal.
Rect wifiDirtyRect(ScreenId id);

// "Clock", "Tasks", ... - used by the web page so the names match the enum.
const char *screenName(ScreenId id);

// Draws the screen for the given id.
void drawScreen(Adafruit_GFX &g, ScreenId id, const AppState &s);

// Exposed individually so one can be called directly while testing.
void drawClock(Adafruit_GFX &g, const AppState &s);
void drawTasks(Adafruit_GFX &g, const AppState &s);
void drawWeather(Adafruit_GFX &g, const AppState &s);
void drawMarkets(Adafruit_GFX &g, const AppState &s);
void drawInfo(Adafruit_GFX &g, const AppState &s);
