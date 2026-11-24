// Defines screen identifiers, display data, and drawing entry points.

#pragma once
#include "config.h"
#include <Adafruit_GFX.h>

enum ScreenId : uint8_t {
  SCREEN_CLOCK = 0,
  SCREEN_TASKS,
  SCREEN_WEATHER,
  SCREEN_MARKETS,
  SCREEN_ROTATION_COUNT,
  SCREEN_INFO = SCREEN_ROTATION_COUNT
};

enum WxIcon : uint8_t { WX_CLEAR = 0, WX_PARTLY_CLOUDY, WX_OVERCAST, WX_RAIN, WX_THUNDERSTORM, WX_SNOW };

struct Task {
  char title[TASK_TITLE_MAX];
  bool done;
};

struct WeatherDay {
  char name[4]; // e.g. "MON"
  int16_t temp;  // Given day's max temperature
  uint8_t icon; // Weather icon (WxIcon)
};

// Five-minute closes, oldest to newest on the chart.
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
  bool fahrenheit; // picks which degree glyph to draw

  char location[WX_NAME_MAX]; // e.g. "BERLIN"
  char status[20];            // e.g. "PARTLY CLOUDY"
  char updated[6];            // "HH:MM" of the last good fetch

  int16_t tempNow;
  int16_t tempHigh; // today's high
  int16_t tempLow;  // today's low
  uint8_t iconNow; // Weather icon (WxIcon)

  WeatherDay days[WX_CARD_COUNT]; // the next four days
};

// Everything the screens draw from. Each subsystem fills its own section.
struct AppState {
  bool timeSynced; // false until NTP fetched
  uint8_t hour;
  uint8_t minute;
  bool showDate;              // date line on the clock screen, set on the web page
  char clockDate[32];         // e.g. "Fri, 6 Mar 2026"
  char tasksDate[32];         // e.g. "FRIDAY, 6 MAR"
  char tzLabel[TZ_LABEL_MAX]; // shown on INFO

  Task tasks[MAX_TASKS];
  uint8_t taskCount;

  bool wifiConnected;
  char ip[16]; // e.g. "192.168.1.10"
  char ssid[33];
  char security[12]; // negotiated mode: "WPA3", "WPA2/WPA3"
  int8_t rssi;       // dBm
  uint32_t uptimeSec;

  uint8_t batteryPct; // 0/25/50/75/100, or BATTERY_UNKNOWN

  WeatherData weather;

  MarketData markets;

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
