// Fetches a quote and five-minute price history from Twelve Data.

#include <Arduino.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_settings.h"
#include "clock_time.h"
#include "config.h"
#include "markets.h"
#include "wifi_net.h"

// The API key and symbol, copied in by the loop task. The fetch task never reads NVS.
struct MktConfig {
  bool configured;
  char key[MARKET_KEY_MAX];
  char symbol[MARKET_SYMBOL_MAX];
};

static SemaphoreHandle_t s_lock = nullptr;
static MktConfig s_cfg;
static MarketData s_data;
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

// A bad key or a rate limit comes back as HTTP 200 with an error in the body.
static bool isApiError(JsonDocument &doc) {
  const char *st = doc["status"] | "";
  if (strcmp(st, "error") == 0) {
    Serial.printf("[mkt] api error: %s\n", (const char *)(doc["message"] | "?"));
    return true;
  }
  return false;
}

// Twelve Data encodes prices as decimal strings.
static bool readPrice(JsonVariantConst field, double &price) {
  const char *text = field.as<const char *>();
  if (!text)
    return false;
  char *end;
  price = strtod(text, &end);
  return end != text && *end == '\0' && isfinite(price);
}

static bool fetchQuote(const char *key, const char *symbol, MarketData &out) {
  char url[224];
  snprintf(url, sizeof(url), "https://api.twelvedata.com/quote?symbol=%s&apikey=%s", symbol, key);

  JsonDocument doc;
  if (!wifiGetJson(url, MARKET_HTTP_TIMEOUT_MS, doc) || isApiError(doc))
    return false;

  double price, prev;
  if (!readPrice(doc["close"], price) || !readPrice(doc["previous_close"], prev) ||
      !doc["is_market_open"].is<bool>()) {
    Serial.println("[mkt] incomplete quote");
    return false;
  }
  const double pct = (prev != 0.0) ? (price - prev) / prev * 100.0 : 0.0;

  snprintf(out.name, sizeof(out.name), "%s", symbol);
  snprintf(out.price, sizeof(out.price), "%.2f", price);
  snprintf(out.changePct, sizeof(out.changePct), "%.2f%%", fabs(pct));
  out.rising = (pct >= 0.0);
  out.marketOpen = doc["is_market_open"] | false;
  return true;
}

// The session's 5-minute closes, for the sparkline.
static bool fetchSeries(const char *key, const char *symbol, MarketData &out) {
  // 5-minute bars, one session's worth. outputsize caps the point count.
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.twelvedata.com/time_series"
           "?symbol=%s&interval=5min&outputsize=%u&apikey=%s",
           symbol, (unsigned)MARKET_SPARK_MAX, key);

  JsonDocument doc;
  if (!wifiGetJson(url, MARKET_HTTP_TIMEOUT_MS, doc) || isApiError(doc))
    return false;

  JsonArray values = doc["values"];
  if (values.isNull() || values.size() == 0) {
    Serial.println("[mkt] time_series empty");
    return false;
  }

  const size_t avail = values.size();
  const uint8_t n = (avail > MARKET_SPARK_MAX) ? MARKET_SPARK_MAX : (uint8_t)avail;
  out.sparkCount = n;

  // The API returns newest first, the chart needs oldest first, so fill the
  // array backwards. Min and max are tracked on the way through.
  for (uint8_t i = 0; i < n; i++) {
    double close;
    if (!readPrice(values[i]["close"], close) || !isfinite((float)close))
      return false;
    const float v = (float)close;
    out.spark[n - 1 - i] = v;
    if (i == 0 || v < out.sparkMin)
      out.sparkMin = v;
    if (i == 0 || v > out.sparkMax)
      out.sparkMax = v;
  }
  return true;
}

// Both endpoints, one after the other. Either failing fails the whole fetch.
static bool fetchOnce(const MktConfig &cfg, MarketData &out) {
  Serial.printf("[mkt] fetching %s | heap=%u\n", cfg.symbol, (unsigned)ESP.getFreeHeap());

  if (!fetchQuote(cfg.key, cfg.symbol, out))
    return false;
  if (!fetchSeries(cfg.key, cfg.symbol, out))
    return false;

  struct tm now;
  if (clockGetLocal(now))
    strftime(out.updated, sizeof(out.updated), "%H:%M", &now);
  else
    snprintf(out.updated, sizeof(out.updated), "--:--");

  out.valid = true;
  return true;
}

static void marketsTask(void *) {
  uint32_t nextAt = millis() + FETCH_FIRST_DELAY_MS + FETCH_FIRST_STAGGER_MS;

  for (;;) {
    if (wifiIsConnected() && (clockIsSynced() || millis() >= FETCH_NTP_GRACE_MS)) {
      MktConfig cfg;
      lock();
      const bool fetch = s_cfg.configured && (s_forceFetch || (int32_t)(millis() - nextAt) >= 0);
      const uint32_t version = s_configVersion;
      if (fetch) {
        cfg = s_cfg;
        s_forceFetch = false;
      }
      unlock();

      if (fetch) {
        MarketData fresh{};
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

        const uint32_t delay = ok ? clockNextFetchDelay(MARKET_FETCH_MS, MARKET_SLOT_OFFSET_SEC) : MARKET_RETRY_MS;
        nextAt = millis() + delay;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void marketsBegin() {
  s_lock = xSemaphoreCreateMutex();
  marketsApplySettings();
  if (s_lock)
    xTaskCreate(marketsTask, "markets", MARKET_TASK_STACK, nullptr, 1, nullptr);
}

// Loop task only, the one place NVS is read for markets.
void marketsApplySettings() {
  MktConfig c{};
  settingsGetMarketKey(c.key, sizeof(c.key));
  settingsGetMarketSymbol(c.symbol, sizeof(c.symbol));
  c.configured = c.key[0] != '\0';
  lock();
  const bool same = strcmp(c.key, s_cfg.key) == 0 && strcmp(c.symbol, s_cfg.symbol) == 0;
  if (!same) {
    s_cfg = c;
    ++s_configVersion;
    s_data = {};
  }
  unlock();
}

void marketsRequestFetch() {
  lock();
  s_forceFetch = true;
  unlock();
}

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
  const uint32_t okMs = s_lastOkMs;
  unlock();

  s.markets.configured = configured;

  // Never fetched successfully, so there's nothing to show yet.
  if (!s.markets.valid) {
    s.markets.stale = false;
    snprintf(s.markets.updated, sizeof(s.markets.updated), "--:--");
    return;
  }
  s.markets.stale = (millis() - okMs) > MARKET_STALE_MS;
}
