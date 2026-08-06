// weather.cpp
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "app_settings.h"
#include "clock_time.h"
#include "config.h"
#include "weather.h"

// Root certificates built into the firmware, so HTTPS is actually verified.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

static const size_t ROOTCA_BUNDLE_SIZE = (size_t)(rootca_crt_bundle_end - rootca_crt_bundle_start);

// The location, copied in by the loop task. The fetch task never reads NVS.
struct WxConfig {
  bool configured;
  float lat;
  float lon;
  char name[WX_NAME_MAX];
};

static SemaphoreHandle_t s_lock = nullptr;
static WxConfig s_cfg;         // guarded
static WeatherData s_data;     // guarded
static bool s_updated = false; // guarded
static volatile bool s_forceFetch = false;
static uint32_t s_lastOkMs = 0; // guarded
static bool s_everOk = false;   // guarded

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
static void weekdayFromIso(const char *iso, char *out, size_t n) {
  int y = 0, m = 0, d = 0;
  if (!iso || sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3) {
    snprintf(out, n, "?");
    return;
  }

  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = 12; // midday, so DST can't shift the date
  mktime(&t);     // works out the weekday

  char buf[8];
  strftime(buf, sizeof(buf), "%a", &t);
  for (char *p = buf; *p; p++)
    *p = (char)toupper((unsigned char)*p);
  snprintf(out, n, "%s", buf);
}

static void upperAscii(char *s) {
  for (; *s; s++)
    *s = (char)toupper((unsigned char)*s);
}

// One fetch. Runs on the weather task.
static bool fetchOnce(const WxConfig &cfg, WeatherData &out) {
  // printf can't print floats on the ESP32, so convert them to text first.
  const String latStr = String(cfg.lat, 4);
  const String lonStr = String(cfg.lon, 4);

  char url[288];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast"
           "?latitude=%s&longitude=%s"
           "&current=temperature_2m,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&timezone=auto&forecast_days=5",
           latStr.c_str(), lonStr.c_str());

  Serial.printf("[wx] heap=%u\n", (unsigned)ESP.getFreeHeap());
  Serial.printf("[wx] GET %s\n", url);

  bool ok = false;

  // Scoped so TLS frees its ~40KB before this returns.
  {
    WiFiClientSecure client;
    client.setCACertBundle(rootca_crt_bundle_start, ROOTCA_BUNDLE_SIZE);
    client.setTimeout(WEATHER_HTTP_TIMEOUT_MS / 1000); // seconds

    HTTPClient http;
    http.setConnectTimeout(WEATHER_HTTP_TIMEOUT_MS); // milliseconds
    http.setTimeout(WEATHER_HTTP_TIMEOUT_MS);
    http.setReuse(false);

    if (!http.begin(client, url)) {
      Serial.println("[wx] http begin failed");
      return false;
    }

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
      // -1 usually means TLS or DNS, not a real HTTP status.
      Serial.printf("[wx] http %d | heap=%u\n", status, (unsigned)ESP.getFreeHeap());
      http.end();
      return false;
    }

    // getString(), not getStream(). The reply is chunked, and the raw stream
    // still has the chunk markers in it, which the JSON parser rejects.
    const String payload = http.getString();
    const int declared = http.getSize();
    http.end();

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, payload);

    if (err) {
      Serial.printf("[wx] json: %s | content-length=%d | got %u bytes\n", err.c_str(), declared,
                    (unsigned)payload.length());
      Serial.printf("[wx] body starts: %.80s\n", payload.c_str());
      return false;
    }

    JsonObject cur = doc["current"];
    if (cur.isNull()) {
      Serial.println("[wx] no 'current' in response");
      return false;
    }

    const int codeNow = cur["weather_code"] | -1;
    if (codeNow < 0)
      return false;

    out.tempNow = (int8_t)lroundf(cur["temperature_2m"] | 0.0f);
    out.iconNow = (uint8_t)iconFromWmo(codeNow);
    snprintf(out.status, sizeof(out.status), "%s", textFromWmo(codeNow));

    JsonObject daily = doc["daily"];
    JsonArray dTime = daily["time"];
    JsonArray dCode = daily["weather_code"];
    JsonArray dMax = daily["temperature_2m_max"];
    JsonArray dMin = daily["temperature_2m_min"];

    if (dTime.size() < (size_t)(WX_CARD_COUNT + 1)) {
      Serial.println("[wx] short daily array");
      return false;
    }

    out.tempHigh = (int8_t)lroundf(dMax[0] | 0.0f); // index 0 is today
    out.tempLow = (int8_t)lroundf(dMin[0] | 0.0f);

    for (uint8_t i = 0; i < WX_CARD_COUNT; i++) { // 1 to 4 are the cards
      const size_t k = (size_t)i + 1;
      weekdayFromIso(dTime[k] | "", out.days[i].name, sizeof(out.days[i].name));
      out.days[i].temp = (int8_t)lroundf(dMax[k] | 0.0f);
      out.days[i].icon = (uint8_t)iconFromWmo(dCode[k] | 0);
    }

    ok = true;
  }

  snprintf(out.location, sizeof(out.location), "%s", cfg.name);
  upperAscii(out.location);

  // updated is filled in by weatherCopyInto(), not here
  out.configured = true;
  out.valid = true;
  out.stale = false;
  return ok;
}

// Time until the next fetch. Falls back to a plain interval without a clock.
static uint32_t msUntilNextSlot() {
  struct tm t;
  if (!clockGetLocal(t))
    return WEATHER_FETCH_MS;

  const int32_t slotSecs = (int32_t)WEATHER_SLOT_MINUTES * 60;
  const int32_t offset = WEATHER_SLOT_OFFSET_SEC;
  const int32_t now = (int32_t)t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;

  // Last slot boundary, plus the offset.
  int32_t target = (now / slotSecs) * slotSecs + offset;

  // Step on until it's in the future, so we don't fetch the same slot twice.
  while (target - now <= FETCH_SLOT_MIN_GAP_SEC)
    target += slotSecs;

  return (uint32_t)(target - now) * 1000UL;
}

static void logNextSlot(uint32_t inMs) {
  struct tm t;
  if (!clockGetLocal(t)) {
    Serial.printf("[wx] next fetch in %lus\n", (unsigned long)(inMs / 1000));
    return;
  }
  const time_t at = time(nullptr) + (time_t)(inMs / 1000UL);
  struct tm tt;
  localtime_r(&at, &tt);
  Serial.printf("[wx] next fetch at %02d:%02d (in %lum)\n", tt.tm_hour, tt.tm_min, (unsigned long)(inMs / 60000UL));
}

static void weatherTask(void *) {
  uint32_t nextAt = millis() + FETCH_FIRST_DELAY_MS; // let Wi-Fi settle first

  for (;;) {
    const bool due = (int32_t)(millis() - nextAt) >= 0;

    // Wait for NTP so the data isn't stamped "--:--", but give up eventually.
    if (!clockIsSynced() && millis() < FETCH_NTP_GRACE_MS) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if ((due || s_forceFetch) && WiFi.status() == WL_CONNECTED) {
      WxConfig cfg;
      lock();
      cfg = s_cfg;
      unlock();

      if (cfg.configured) {
        s_forceFetch = false;

        WeatherData fresh;
        memset(&fresh, 0, sizeof(fresh));

        if (fetchOnce(cfg, fresh)) {
          // Publish it all in one go, so the loop task can't see half of it.
          lock();
          s_data = fresh;
          s_lastOkMs = millis();
          s_everOk = true;
          s_updated = true;
          unlock();

          const uint32_t wait = msUntilNextSlot();
          nextAt = millis() + wait;
          Serial.printf("[wx] ok: %d C, %s\n", (int)fresh.tempNow, fresh.status);
          logNextSlot(wait);
        } else {
          // Keep the old data and retry soon, rather than waiting for the next
          // slot half an hour away.
          nextAt = millis() + WEATHER_RETRY_MS;
          Serial.printf("[wx] fetch failed, retrying in %lum\n", (unsigned long)(WEATHER_RETRY_MS / 60000UL));
        }

        // Near zero here means the stack is too small, which reads as a random
        // reboot when TLS overruns it.
        Serial.printf("[wx] task stack free: %u bytes | heap=%u\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                      (unsigned)ESP.getFreeHeap());
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // check again in a second
  }
}

void weatherBegin() {
  s_lock = xSemaphoreCreateMutex();
  memset(&s_cfg, 0, sizeof(s_cfg));
  memset(&s_data, 0, sizeof(s_data));

  weatherApplySettings();
  xTaskCreate(weatherTask, "weather", WEATHER_TASK_STACK, nullptr, 1, nullptr);
}

// Loop task only, the one place NVS is read for weather.
void weatherApplySettings() {
  WxConfig c;
  memset(&c, 0, sizeof(c));
  settingsGetWeather(&c.lat, &c.lon, c.name, sizeof(c.name));
  c.configured = settingsWeatherConfigured();

  lock();
  s_cfg = c;
  unlock();
}

void weatherRequestFetch() { s_forceFetch = true; }

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
  const bool everOk = s_everOk;
  const uint32_t okMs = s_lastOkMs;
  unlock();

  s.weather.configured = cfg.configured;
  snprintf(s.weather.location, sizeof(s.weather.location), "%s", cfg.name);
  upperAscii(s.weather.location);

  if (!everOk) {
    s.weather.valid = false;
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
