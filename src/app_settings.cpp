// app_settings.cpp
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "app_settings.h"
#include "config.h"

static Preferences prefs;

// NVS keys, max 15 characters each.
static const char *NS = "dashboard";
static const char *KEY_INFO_SEEN = "info_seen";
static const char *KEY_TZ_POSIX = "tz_posix";
static const char *KEY_TZ_LABEL = "tz_label";
static const char *KEY_SHOW_DATE = "show_date";
static const char *KEY_DISP_MODE = "disp_mode";
static const char *KEY_DISP_SCRN = "disp_scrn";
static const char *KEY_DWELL_MIN = "dwell_min";
static const char *KEY_WX_LAT = "wx_lat";
static const char *KEY_WX_LON = "wx_lon";
static const char *KEY_WX_NAME = "wx_name";
static const char *KEY_MKT_KEY = "mkt_key";

void settingsBegin() { prefs.begin(NS, /*readOnly=*/false); }

void settingsFactoryReset() {
  prefs.clear();
  Serial.println("[settings] factory reset - back to UTC, info screen re-armed");
}

// INFO -------------------------------------------------------------------
bool settingsInfoShown() { return prefs.getBool(KEY_INFO_SEEN, false); }

void settingsMarkInfoShown() {
  if (prefs.getBool(KEY_INFO_SEEN, false))
    return; // already set, no need to write flash again
  prefs.putBool(KEY_INFO_SEEN, true);
  Serial.println("[settings] info screen acknowledged");
}

// CLOCK  ------------------------------------------------------------------
void settingsGetTz(char *posix, size_t nPosix, char *label, size_t nLabel) {
  const String p = prefs.getString(KEY_TZ_POSIX, DEFAULT_TZ_POSIX);
  const String l = prefs.getString(KEY_TZ_LABEL, DEFAULT_TZ_LABEL);
  snprintf(posix, nPosix, "%s", p.c_str());
  snprintf(label, nLabel, "%s", l.c_str());
}

void settingsSetTz(const char *posix, const char *label) {
  prefs.putString(KEY_TZ_POSIX, posix);
  prefs.putString(KEY_TZ_LABEL, label);
  Serial.printf("[settings] timezone saved: %s (%s)\n", label, posix);
}

bool settingsShowDate() { return prefs.getBool(KEY_SHOW_DATE, true); }

void settingsSetShowDate(bool show) { prefs.putBool(KEY_SHOW_DATE, show); }

// DISPLAY ----------------------------------------------------------------
uint8_t settingsDisplayMode() { return prefs.getUChar(KEY_DISP_MODE, DEFAULT_DISPLAY_MODE); }

void settingsSetDisplayMode(uint8_t mode) { prefs.putUChar(KEY_DISP_MODE, mode); }

uint8_t settingsStaticScreen() { return prefs.getUChar(KEY_DISP_SCRN, DEFAULT_STATIC_SCREEN); }

void settingsSetStaticScreen(uint8_t screen) { prefs.putUChar(KEY_DISP_SCRN, screen); }

uint8_t settingsDwellMinutes() { return prefs.getUChar(KEY_DWELL_MIN, DEFAULT_DWELL_MINUTES); }

void settingsSetDwellMinutes(uint8_t minutes) { prefs.putUChar(KEY_DWELL_MIN, minutes); }

// WEATHER ----------------------------------------------------------------
bool settingsWeatherConfigured() { return prefs.getString(KEY_WX_NAME, "").length() > 0; }

void settingsGetWeather(float *lat, float *lon, char *name, size_t nName) {
  if (lat)
    *lat = prefs.getFloat(KEY_WX_LAT, 0.0f);
  if (lon)
    *lon = prefs.getFloat(KEY_WX_LON, 0.0f);
  if (name) {
    const String n = prefs.getString(KEY_WX_NAME, "");
    snprintf(name, nName, "%s", n.c_str());
  }
}

void settingsSetWeather(float lat, float lon, const char *name) {
  prefs.putFloat(KEY_WX_LAT, lat);
  prefs.putFloat(KEY_WX_LON, lon);
  prefs.putString(KEY_WX_NAME, name);
  // printf can't print floats on the ESP32, so convert them to text first.
  Serial.printf("[settings] weather location: %s (%s, %s)\n", name, String(lat, 4).c_str(), String(lon, 4).c_str());
}

// MARKETS ----------------------------------------------------------------
bool settingsMarketConfigured() { return prefs.getString(KEY_MKT_KEY, "").length() > 0; }

void settingsGetMarketKey(char *key, size_t nKey) {
  const String k = prefs.getString(KEY_MKT_KEY, "");
  snprintf(key, nKey, "%s", k.c_str());
}

void settingsSetMarketKey(const char *key) {
  prefs.putString(KEY_MKT_KEY, key);
  // Log the length only, never the key itself.
  Serial.printf("[settings] market API key saved (%u chars)\n", (unsigned)strlen(key));
}
