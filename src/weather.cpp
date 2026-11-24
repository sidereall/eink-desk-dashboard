// Fetches current conditions and a four-day forecast from Open-Meteo.

#include <Arduino.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "app_settings.h"
#include "clock_time.h"
#include "config.h"
#include "weather.h"
#include "wifi_net.h"

// The location, copied in by the loop task. The fetch task never reads NVS.
struct WxConfig {
  bool configured;
  bool fahrenheit;
  float lat;
  float lon;
  char name[WX_NAME_MAX];
};

static SemaphoreHandle_t s_lock = nullptr;
static WxConfig s_cfg;
static WeatherData s_data;
static bool s_updated = false;
static bool s_forceFetch = false;
static uint32_t s_configVersion = 0;
static uint32_t s_lastOkMs = 0;

static inline void lock() {
  if (s_lock)
    xSemaphoreTake(s_lock, portMAX_DELAY);
}
static inline void unlock() {
  if (s_lock)
    xSemaphoreGive(s_lock);
}

// Open-Meteo's weather codes, mapped onto our six icons.
static WxIcon iconFromWmo(int code) {
  if (code == 0)
    return WX_CLEAR;
  if (code == 1 || code == 2)
    return WX_PARTLY_CLOUDY;
  if (code == 3)
    return WX_OVERCAST;
  if (code == 45 || code == 48)
    return WX_OVERCAST; // fog
  if (code >= 95)
    return WX_THUNDERSTORM;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86)
    return WX_SNOW;
  return WX_RAIN;
}

// The same codes as text for the panel.
static const char *textFromWmo(int code) {
  switch (code) {
  case 0:
    return "CLEAR";
  case 1:
    return "MAINLY CLEAR";
  case 2:
    return "PARTLY CLOUDY";
  case 3:
    return "OVERCAST";
  case 45:
  case 48:
    return "FOG";
  case 51:
  case 53:
  case 55:
    return "DRIZZLE";
  case 56:
  case 57:
    return "FREEZING DRIZZLE";
  case 61:
  case 63:
  case 65:
    return "RAIN";
  case 66:
  case 67:
    return "FREEZING RAIN";
  case 71:
  case 73:
  case 75:
    return "SNOW";
  case 77:
    return "SNOW GRAINS";
  case 80:
  case 81:
  case 82:
    return "RAIN SHOWERS";
  case 85:
  case 86:
    return "SNOW SHOWERS";
  case 95:
    return "THUNDERSTORM";
  case 96:
  case 99:
    return "THUNDERSTORM HAIL";
  default:
    return "-";
  }
}

// "2026-07-14" -> "TUE", using the API's date so other timezones stay right.
static bool weekdayFromIso(const char *iso, char *out, size_t n) {
  int y, m, d;
  char extra;
  if (!iso || sscanf(iso, "%d-%d-%d%c", &y, &m, &d, &extra) != 3 || y < 1970 || y > 9999 ||
      m < 1 || m > 12 || d < 1 || d > 31)
    return false;

  struct tm t{};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = 12;
  t.tm_isdst = -1;
  if (mktime(&t) == (time_t)-1 || t.tm_mday != d)
    return false;

  char buf[8];
  strftime(buf, sizeof(buf), "%a", &t);
  for (char *p = buf; *p; p++)
    *p = (char)toupper((unsigned char)*p);
  snprintf(out, n, "%s", buf);
  return true;
}

static void upperAscii(char *s) {
  for (; *s; s++)
    *s = (char)toupper((unsigned char)*s);
}

static bool readTemperature(JsonVariantConst field, int16_t &temperature) {
  if (!field.is<float>())
    return false;
  const float value = field.as<float>();
  if (!isfinite(value) || value < INT16_MIN || value > INT16_MAX)
    return false;
  temperature = (int16_t)lroundf(value);
  return true;
}
static bool fetchOnce(const WxConfig &cfg, WeatherData &out) {
  const String latStr = String(cfg.lat, 4);
  const String lonStr = String(cfg.lon, 4);

  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast"
           "?latitude=%s&longitude=%s"
           "&current=temperature_2m,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&timezone=auto&forecast_days=5%s",
           latStr.c_str(), lonStr.c_str(), cfg.fahrenheit ? "&temperature_unit=fahrenheit" : "");

  JsonDocument doc;
  if (!wifiGetJson(url, WEATHER_HTTP_TIMEOUT_MS, doc))
    return false;

  JsonObjectConst current = doc["current"];
  if (!current["weather_code"].is<int>() || !readTemperature(current["temperature_2m"], out.tempNow))
    return false;
  const int code = current["weather_code"];
  if (code < 0)
    return false;
  out.iconNow = iconFromWmo(code);
  snprintf(out.status, sizeof(out.status), "%s", textFromWmo(code));

  JsonObjectConst daily = doc["daily"];
  if (!readTemperature(daily["temperature_2m_max"][0], out.tempHigh) ||
      !readTemperature(daily["temperature_2m_min"][0], out.tempLow))
    return false;

  for (uint8_t i = 0; i < WX_CARD_COUNT; i++) {
    const size_t day = i + 1;
    WeatherDay &card = out.days[i];
    JsonVariantConst weatherCode = daily["weather_code"][day];
    if (!weatherCode.is<int>() || weatherCode.as<int>() < 0 ||
        !readTemperature(daily["temperature_2m_max"][day], card.temp) ||
        !weekdayFromIso(daily["time"][day].as<const char *>(), card.name, sizeof(card.name)))
      return false;
    card.icon = iconFromWmo(weatherCode.as<int>());
  }
  out.valid = true;
  return true;
}

static void weatherTask(void *) {
  uint32_t nextAt = millis() + FETCH_FIRST_DELAY_MS;

  for (;;) {
    if (wifiIsConnected() && (clockIsSynced() || millis() >= FETCH_NTP_GRACE_MS)) {
      WxConfig cfg;
      lock();
      const bool fetch = s_cfg.configured && (s_forceFetch || (int32_t)(millis() - nextAt) >= 0);
      const uint32_t version = s_configVersion;
      if (fetch) {
        cfg = s_cfg;
        s_forceFetch = false;
      }
      unlock();

      if (fetch) {
        WeatherData fresh{};
        const bool ok = fetchOnce(cfg, fresh);
        lock();
        // A save or factory reset can change the settings during HTTPS.
        if (version == s_configVersion) {
          if (ok) {
            s_data = fresh;
            s_lastOkMs = millis();
          }
          s_updated = true;
        }
        unlock();

        const uint32_t delay = ok ? clockNextFetchDelay(WEATHER_FETCH_MS, WEATHER_SLOT_OFFSET_SEC) : WEATHER_RETRY_MS;
        nextAt = millis() + delay;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void weatherBegin() {
  s_lock = xSemaphoreCreateMutex();

  weatherApplySettings();
  if (s_lock)
    xTaskCreate(weatherTask, "weather", WEATHER_TASK_STACK, nullptr, 1, nullptr);
}

// Loop task only, the one place NVS is read for weather.
void weatherApplySettings() {
  WxConfig c{};
  settingsGetWeather(&c.lat, &c.lon, c.name, sizeof(c.name));
  c.configured = c.name[0] != '\0';
  c.fahrenheit = settingsFahrenheit();

  lock();
  const bool same = c.lat == s_cfg.lat && c.lon == s_cfg.lon && c.fahrenheit == s_cfg.fahrenheit &&
                    strcmp(c.name, s_cfg.name) == 0;
  if (!same) {
    s_cfg = c;
    ++s_configVersion;
    s_data = {};
  }
  unlock();
}

void weatherRequestFetch() {
  lock();
  s_forceFetch = true;
  unlock();
}

bool weatherTakeUpdate() {
  lock();
  const bool v = s_updated;
  s_updated = false;
  unlock();
  return v;
}

void weatherCopyInto(AppState &s) {
  lock();
  s.weather = s_data;
  const WxConfig cfg = s_cfg;
  const uint32_t okMs = s_lastOkMs;
  unlock();

  s.weather.configured = cfg.configured;
  s.weather.fahrenheit = cfg.fahrenheit;
  snprintf(s.weather.location, sizeof(s.weather.location), "%s", cfg.name);
  upperAscii(s.weather.location);

  if (!s.weather.valid) {
    s.weather.stale = false;
    snprintf(s.weather.updated, sizeof(s.weather.updated), "--:--");
    return;
  }

  const uint32_t ageMs = millis() - okMs;
  s.weather.stale = ageMs > WEATHER_STALE_MS;

  // Worked out from how long ago the fetch was, so data fetched before the
  // clock was set still gets a real time once it is.
  if (clockIsSynced()) {
    const time_t at = time(nullptr) - (time_t)(ageMs / 1000UL);
    struct tm t;
    localtime_r(&at, &t);
    strftime(s.weather.updated, sizeof(s.weather.updated), "%H:%M", &t);
  } else {
    snprintf(s.weather.updated, sizeof(s.weather.updated), "--:--");
  }
}
