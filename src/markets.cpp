// markets.cpp - Twelve Data: quote and intraday time series for SPY.
//
// Two endpoints per refresh: /quote for the price and open/closed flag,
// /time_series for the sparkline. That's 2 of the free tier's 800 daily
// credits, 48 times a day.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_settings.h"
#include "clock_time.h"
#include "config.h"
#include "markets.h"

// Root certificates built into the firmware, so HTTPS is actually verified.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
static const size_t ROOTCA_BUNDLE_SIZE = (size_t)(rootca_crt_bundle_end - rootca_crt_bundle_start);

// The API key, copied in by the loop task. The fetch task never reads NVS.
struct MktConfig {
  bool configured;
  char key[MARKET_KEY_MAX];
};

static SemaphoreHandle_t s_lock = nullptr;
static MktConfig s_cfg;        // guarded
static MarketData s_data;      // guarded
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

// Fetches a URL and parses the reply as JSON. Used by both endpoints.
//
// getString(), not getStream(). The reply is chunked, and the raw stream still
// has the chunk markers in it, which the JSON parser rejects.
static bool httpsGetJson(const char *url, JsonDocument &doc) {
  bool ok = false;
  // In its own block so the TLS memory is freed before the function returns.
  {
    WiFiClientSecure client;
    client.setCACertBundle(rootca_crt_bundle_start, ROOTCA_BUNDLE_SIZE);
    client.setTimeout(MARKET_HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setConnectTimeout(MARKET_HTTP_TIMEOUT_MS);
    http.setTimeout(MARKET_HTTP_TIMEOUT_MS);
    http.setReuse(false);

    if (!http.begin(client, url)) {
      Serial.println("[mkt] http begin failed");
      return false;
    }

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
      Serial.printf("[mkt] http %d | heap=%u\n", status, (unsigned)ESP.getFreeHeap());
      http.end();
      return false;
    }

    const String body = http.getString();
    http.end();

    const DeserializationError err = deserializeJson(doc, body);
    if (err) {
      Serial.printf("[mkt] json: %s | %u bytes\n", err.c_str(), (unsigned)body.length());
      Serial.printf("[mkt] body starts: %.80s\n", body.c_str());
      return false;
    }
    ok = true;
  }
  return ok;
}

// A bad key or a rate limit comes back as HTTP 200 with an error in the body.
static bool isApiError(JsonDocument &doc) {
  const char *st = doc["status"] | "";
  if (strcmp(st, "error") == 0) {
    Serial.printf("[mkt] api error: %s\n", (const char *)(doc["message"] | "?"));
    return true;
  }
  return false;
}

// Current price, previous close, and whether the market is open.
static bool fetchQuote(const char *key, MarketData &out) {
  char url[224];
  snprintf(url, sizeof(url), "https://api.twelvedata.com/quote?symbol=%s&apikey=%s", MARKET_SYMBOL, key);

  JsonDocument doc;
  if (!httpsGetJson(url, doc))
    return false;
  if (isApiError(doc))
    return false;

  // Numbers come back as strings, so read them as text and convert.
  const char *priceS = doc["close"] | "";
  const char *prevS = doc["previous_close"] | "";
  if (priceS[0] == '\0' || prevS[0] == '\0') {
    Serial.println("[mkt] quote missing close/previous_close");
    return false;
  }

  const double price = atof(priceS);
  const double prev = atof(prevS);
  const double pct = (prev != 0.0) ? (price - prev) / prev * 100.0 : 0.0;

  snprintf(out.name, sizeof(out.name), "%s", MARKET_LABEL);
  snprintf(out.price, sizeof(out.price), "%.2f", price);
  snprintf(out.changePct, sizeof(out.changePct), "%.2f%%", fabs(pct));
  out.rising = (pct >= 0.0);
  out.marketOpen = doc["is_market_open"] | false;
  return true;
}

// The session's 5-minute closes, for the sparkline.
static bool fetchSeries(const char *key, MarketData &out) {
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.twelvedata.com/time_series"
           "?symbol=%s&interval=5min&outputsize=%u&apikey=%s",
           MARKET_SYMBOL, (unsigned)MARKET_SPARK_MAX, key);

  JsonDocument doc;
  if (!httpsGetJson(url, doc))
    return false;
  if (isApiError(doc))
    return false;

  JsonArray values = doc["values"];
  if (values.isNull() || values.size() == 0) {
    Serial.println("[mkt] time_series empty");
    return false;
  }

  const size_t avail = values.size();
  const uint8_t n = (avail > MARKET_SPARK_MAX) ? MARKET_SPARK_MAX : (uint8_t)avail;
  out.sparkCount = n;
  out.sparkMin = 1e30f;
  out.sparkMax = -1e30f;

  // The API returns newest first, the chart needs oldest first, so fill the
  // array backwards. Min and max are tracked on the way through.
  for (uint8_t i = 0; i < n; i++) {
    const char *cS = values[i]["close"] | "0";
    const float v = (float)atof(cS);
    out.spark[n - 1 - i] = v;
    if (v < out.sparkMin)
      out.sparkMin = v;
    if (v > out.sparkMax)
      out.sparkMax = v;
  }
  return true;
}

// Both endpoints, one after the other. Either failing fails the whole fetch.
static bool fetchOnce(const MktConfig &cfg, MarketData &out) {
  Serial.printf("[mkt] fetching %s | heap=%u\n", MARKET_SYMBOL, (unsigned)ESP.getFreeHeap());

  if (!fetchQuote(cfg.key, out))
    return false;
  if (!fetchSeries(cfg.key, out))
    return false;

  struct tm now;
  if (clockGetLocal(now))
    strftime(out.updated, sizeof(out.updated), "%H:%M", &now);
  else
    snprintf(out.updated, sizeof(out.updated), "--:--");

  out.configured = true;
  out.valid = true;
  out.stale = false;
  return true;
}

// Time until the next fetch. Uses a different offset to weather, so the two don't fire at once.
static uint32_t msUntilNextSlot() {
  struct tm t;
  if (!clockGetLocal(t))
    return MARKET_FETCH_MS;

  const int32_t slotSecs = (int32_t)(MARKET_FETCH_MS / 1000);
  const int32_t offset = MARKET_SLOT_OFFSET_SEC;
  const int32_t now = (int32_t)t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;

  int32_t target = (now / slotSecs) * slotSecs + offset;
  while (target - now <= FETCH_SLOT_MIN_GAP_SEC)
    target += slotSecs;
  return (uint32_t)(target - now) * 1000UL;
}

static void marketsTask(void *) {
  // Starts a beat after weather, so the two TLS spikes don't overlap.
  uint32_t nextAt = millis() + FETCH_FIRST_DELAY_MS + FETCH_FIRST_STAGGER_MS;

  for (;;) {
    // Wait for the clock, so the data isn't stamped "--:--". Gives up after the grace period.
    if (!clockIsSynced() && millis() < FETCH_NTP_GRACE_MS) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    const bool due = (int32_t)(millis() - nextAt) >= 0;

    if ((due || s_forceFetch) && WiFi.status() == WL_CONNECTED) {
      MktConfig cfg;
      lock();
      cfg = s_cfg;
      unlock();

      if (cfg.configured) {
        s_forceFetch = false;

        MarketData fresh;
        memset(&fresh, 0, sizeof(fresh));

        if (fetchOnce(cfg, fresh)) {
          // Publish it all in one go, so the loop task can't see half of it.
          lock();
          s_data = fresh;
          s_lastOkMs = millis();
          s_everOk = true;
          s_updated = true;
          unlock();
          nextAt = millis() + msUntilNextSlot();
          Serial.printf("[mkt] ok: %s %s%s  open=%d\n", fresh.price, fresh.rising ? "+" : "-", fresh.changePct,
                        (int)fresh.marketOpen);
        } else {
          // Keep the old data and retry sooner than the next slot.
          nextAt = millis() + MARKET_RETRY_MS;
          Serial.println("[mkt] fetch failed, will retry");
        }

        // Near zero here means the stack is too small, which reads as a random reboot when TLS overruns it.
        Serial.printf("[mkt] task stack free: %u bytes | heap=%u\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                      (unsigned)ESP.getFreeHeap());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // check again in a second
  }
}

void marketsBegin() {
  s_lock = xSemaphoreCreateMutex();
  memset(&s_cfg, 0, sizeof(s_cfg));
  memset(&s_data, 0, sizeof(s_data));
  marketsApplySettings();
  xTaskCreate(marketsTask, "markets", MARKET_TASK_STACK, nullptr, 1, nullptr);
}

// Loop task only, the one place NVS is read for markets.
void marketsApplySettings() {
  MktConfig c;
  memset(&c, 0, sizeof(c));
  settingsGetMarketKey(c.key, sizeof(c.key));
  c.configured = settingsMarketConfigured();
  lock();
  s_cfg = c;
  unlock();
}

void marketsRequestFetch() { s_forceFetch = true; }

bool marketsTakeUpdate() {
  lock();
  const bool v = s_updated;
  s_updated = false;
  unlock();
  return v;
}

void marketsCopyInto(AppState &s) {
  lock();
  s.markets = s_data;
  const bool configured = s_cfg.configured;
  const bool everOk = s_everOk;
  const uint32_t okMs = s_lastOkMs;
  unlock();

  s.markets.configured = configured;

  // Never fetched successfully, so there's nothing to show yet.
  if (!everOk) {
    s.markets.valid = false;
    s.markets.stale = false;
    snprintf(s.markets.updated, sizeof(s.markets.updated), "--:--");
    return;
  }
  s.markets.stale = (millis() - okMs) > MARKET_STALE_MS;
}
