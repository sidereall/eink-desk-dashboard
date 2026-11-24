// Serves the settings page and handles its HTTP requests.

#include <ArduinoJson.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <string.h>

#include "app_settings.h"
#include "clock_time.h"
#include "config.h"
#include "markets.h"
#include "screens.h"
#include "tasks.h"
#include "timezones.h"
#include "weather.h"
#include "web_ui.h"
#include "wifi_net.h"

static WebServer server(80);
static bool s_running = false;
static bool s_handlersRegistered = false;
static bool s_fsReady = false;
static bool s_dismissRequested = false;
static bool s_syncRequested = false;

static void sendJson(const JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

static void saved() {
  s_syncRequested = true;
  server.send(200, "text/plain", "ok");
}

static bool readInteger(const String &text, long &value) {
  const char *start = text.c_str();
  char *end;
  errno = 0;
  value = strtol(start, &end, 10);
  return end != start && *end == '\0' && errno != ERANGE;
}

static bool readCoordinate(const String &text, float limit, float &value) {
  const char *start = text.c_str();
  char *end;
  value = strtof(start, &end);
  return end != start && *end == '\0' && isfinite(value) && fabsf(value) <= limit;
}

// An omitted checkbox is unchecked.
static bool readCheckbox(const char *name, bool &checked) {
  const String value = server.arg(name);
  checked = value == "1";
  if (!server.hasArg(name) || checked || value == "0")
    return true;
  server.send(400, "text/plain", "bad checkbox value");
  return false;
}

static void handleTimezones() {
  JsonDocument doc;
  JsonArray zones = doc.to<JsonArray>();
  for (const TimeZoneEntry &zone : TIMEZONES) {
    String label(zone.label);
    label.toUpperCase();
    JsonObject item = zones.add<JsonObject>();
    item["label"] = label;
    item["posix"] = zone.posix;
  }
  sendJson(doc);
}

static void handleScreens() {
  JsonDocument doc;
  JsonArray screens = doc.to<JsonArray>();
  for (uint8_t i = 0; i < SCREEN_ROTATION_COUNT; i++) {
    JsonObject item = screens.add<JsonObject>();
    item["id"] = i;
    item["name"] = screenName((ScreenId)i);
  }
  sendJson(doc);
}

static void handleTasksGet() {
  Task slots[MAX_TASKS];
  tasksCopyInto(slots);
  JsonDocument doc;
  JsonArray tasks = doc.to<JsonArray>();
  for (const Task &task : slots) {
    JsonObject item = tasks.add<JsonObject>();
    item["title"] = task.title;
    item["done"] = task.done;
  }
  sendJson(doc);
}

static void handleTasksPost() {
  Task incoming[MAX_TASKS]{};

  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    char key[4];

    snprintf(key, sizeof(key), "t%u", (unsigned)i);
    const String title = server.hasArg(key) ? server.arg(key) : String("");
    snprintf(incoming[i].title, TASK_TITLE_MAX, "%s", title.c_str());

    snprintf(key, sizeof(key), "d%u", (unsigned)i);
    if (!readCheckbox(key, incoming[i].done))
      return;
  }

  // tasksSetAll cleans the titles, drops blanks and closes any gaps.
  tasksSetAll(incoming, MAX_TASKS);

  saved();
}

// Printable ASCII only. Org_01 can't draw anything above 126.
static void asciiOnly(const char *in, char *out, size_t n) {
  size_t j = 0;
  for (; *in && j < n - 1; in++) {
    const unsigned char c = (unsigned char)*in;
    if (c >= 32 && c < 127)
      out[j++] = (char)c;
  }
  out[j] = '\0';
}

static void handleWeatherGet() {
  float lat = 0, lon = 0;
  char name[WX_NAME_MAX];
  settingsGetWeather(&lat, &lon, name, sizeof(name));

  JsonDocument doc;
  doc["lat"] = lat;
  doc["lon"] = lon;
  doc["name"] = name;
  sendJson(doc);
}

static void handleWeatherPost() {
  if (!server.hasArg("lat") || !server.hasArg("lon") || !server.hasArg("name")) {
    server.send(400, "text/plain", "missing field");
    return;
  }

  float lat, lon;
  if (!readCoordinate(server.arg("lat"), 90, lat) || !readCoordinate(server.arg("lon"), 180, lon)) {
    server.send(400, "text/plain", "bad coordinates");
    return;
  }

  char name[WX_NAME_MAX];
  asciiOnly(server.arg("name").c_str(), name, sizeof(name));
  if (name[0] == '\0') {
    server.send(400, "text/plain", "bad name");
    return;
  }

  settingsSetWeather(lat, lon, name);

  weatherApplySettings(); // push the new location to the fetch task
  weatherRequestFetch();  // don't wait for the next slot to try it

  saved();
}

// The temperature unit is separate from the location, so it can be changed
// before one is set.
static void handleUnitsGet() {
  char body[32];
  snprintf(body, sizeof(body), "{\"fahrenheit\":%s}", settingsFahrenheit() ? "true" : "false");
  server.send(200, "application/json", body);
}

static void handleUnitsPost() {
  bool fahrenheit;
  if (!readCheckbox("f", fahrenheit))
    return;
  settingsSetFahrenheit(fahrenheit);
  weatherApplySettings(); // push it to the fetch task
  weatherRequestFetch();  // refetch so the values match the unit
  saved();
}

// Live device state for the page's status strip.
static void handleStatus() {
  WifiStatus w;
  wifiGetStatus(w);

  char now[8];
  struct tm t;
  if (clockGetLocal(t))
    strftime(now, sizeof(now), "%H:%M", &t);
  else
    snprintf(now, sizeof(now), "--:--");

  char body[192];
  snprintf(body, sizeof(body), "{\"ip\":\"%s\",\"wifi\":%s,\"rssi\":%d,\"batt\":%u,\"time\":\"%s\",\"screen\":\"%s\"}",
           w.ip, w.connected ? "true" : "false", (int)w.rssi, (unsigned)webStatusBattery(), now,
           screenName(webStatusScreen()));
  server.send(200, "application/json", body);
}

// Reports whether a key is set, never the key itself.
static void handleMarketsGet() {
  char symbol[MARKET_SYMBOL_MAX];
  settingsGetMarketSymbol(symbol, sizeof(symbol));

  JsonDocument doc;
  doc["configured"] = settingsMarketConfigured();
  doc["symbol"] = symbol;
  sendJson(doc);
}

static void handleMarketsPost() {
  const bool hasKey = server.hasArg("key");
  const bool hasSymbol = server.hasArg("symbol");
  const String key = server.arg("key");
  const String symbol = server.arg("symbol");
  if (!hasKey && !hasSymbol) {
    server.send(400, "text/plain", "nothing to save");
    return;
  }
  if (hasKey && (key.length() < 8 || key.length() >= MARKET_KEY_MAX)) {
    server.send(400, "text/plain", "bad key length");
    return;
  }
  if (hasSymbol && (symbol.isEmpty() || symbol.length() >= MARKET_SYMBOL_MAX)) {
    server.send(400, "text/plain", "bad symbol length");
    return;
  }
  if (hasKey)
    settingsSetMarketKey(key.c_str());
  if (hasSymbol)
    settingsSetMarketSymbol(symbol.c_str());

  marketsApplySettings();
  marketsRequestFetch();
  saved();
}

static void handleClockGet() {
  char posix[TZ_POSIX_MAX], label[TZ_LABEL_MAX];
  settingsGetTz(posix, sizeof(posix), label, sizeof(label));

  char now[48];
  struct tm t;
  if (clockGetLocal(t)) {
    // Same formatter the panel uses, so the two can't drift apart.
    char date[32], hms[16];
    clockFormatDate(t, date, sizeof(date));
    strftime(hms, sizeof(hms), "%H:%M:%S", &t);
    snprintf(now, sizeof(now), "%s  %s", date, hms);
  } else {
    snprintf(now, sizeof(now), "waiting for NTP");
  }

  JsonDocument doc;
  doc["tz"] = posix;
  doc["label"] = label;
  doc["showDate"] = settingsShowDate();
  doc["synced"] = clockIsSynced();
  doc["infoSeen"] = settingsInfoShown();
  doc["now"] = now;
  sendJson(doc);
}

// Current settings plus the allowed dwell range, which builds the dropdown.
static void handleDisplayGet() {
  char body[200];
  snprintf(body, sizeof(body),
           "{\"mode\":%u,\"screen\":%u,\"dwell\":%u,"
           "\"min\":%u,\"max\":%u,\"step\":%u,\"dark\":%s}",
           (unsigned)settingsDisplayMode(), (unsigned)settingsStaticScreen(), (unsigned)settingsDwellMinutes(),
           (unsigned)DWELL_MIN_MINUTES, (unsigned)DWELL_MAX_MINUTES, (unsigned)DWELL_STEP_MINUTES,
           settingsDarkMode() ? "true" : "false");
  server.send(200, "application/json", body);
}

// Only accepts timezones from our own list, so nothing else can reach NVS.
static const TimeZoneEntry *findTimezone(const char *posix) {
  for (size_t i = 0; i < TIMEZONE_COUNT; i++) {
    if (strcmp(TIMEZONES[i].posix, posix) == 0)
      return &TIMEZONES[i];
  }
  return nullptr;
}

static void handleClockPost() {
  if (!server.hasArg("tz")) {
    server.send(400, "text/plain", "missing tz");
    return;
  }

  const String posix = server.arg("tz");
  if (!findTimezone(posix.c_str())) {
    server.send(400, "text/plain", "unknown timezone");
    return;
  }

  const String label = server.hasArg("label") ? server.arg("label") : posix;
  bool showDate;
  if (!readCheckbox("date", showDate))
    return;

  settingsSetTz(posix.c_str(), label.c_str());
  settingsSetShowDate(showDate);

  // No network needed, it only changes how the time is displayed.
  clockSetTimezone(posix.c_str());

  saved();
}

static void handleDisplayPost() {
  if (!server.hasArg("mode") || !server.hasArg("screen") || !server.hasArg("dwell")) {
    server.send(400, "text/plain", "missing field");
    return;
  }

  long mode, screen, dwell;
  if (!readInteger(server.arg("mode"), mode) || (mode != DISPLAY_CYCLE && mode != DISPLAY_STATIC)) {
    server.send(400, "text/plain", "bad mode");
    return;
  }
  if (!readInteger(server.arg("screen"), screen) || screen < 0 || screen >= SCREEN_ROTATION_COUNT) {
    server.send(400, "text/plain", "bad screen");
    return;
  }
  if (!readInteger(server.arg("dwell"), dwell) || dwell < DWELL_MIN_MINUTES ||
      dwell > DWELL_MAX_MINUTES || dwell % DWELL_STEP_MINUTES != 0) {
    server.send(400, "text/plain", "bad dwell");
    return;
  }

  bool dark;
  if (!readCheckbox("dark", dark))
    return;
  settingsSetDisplayMode((uint8_t)mode);
  settingsSetStaticScreen((uint8_t)screen);
  settingsSetDwellMinutes((uint8_t)dwell);
  settingsSetDarkMode(dark);

  saved();
}

static void handleDismiss() {
  s_dismissRequested = true;
  server.send(200, "text/plain", "ok");
}

// The config page itself, read from the filesystem.
static void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(500, "text/plain", "index.html missing from filesystem");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

// Font served from the filesystem like the page itself.
static void handleFont() {
  File f = LittleFS.open("/Micro5.ttf", "r");
  if (!f) {
    server.send(404, "text/plain", "font missing from filesystem");
    return;
  }

  server.streamFile(f, "font/ttf");
  f.close();
}

void webBegin() {
  if (!s_handlersRegistered) {
    // serveStatic 404s here, so the page is served by handleRoot instead.
    server.on("/", HTTP_GET, handleRoot);
    server.on("/font", HTTP_GET, handleFont); // WebServer can't match /Micro5.ttf directly.
    server.on("/api/timezones", HTTP_GET, handleTimezones);
    server.on("/api/screens", HTTP_GET, handleScreens);
    server.on("/api/clock", HTTP_GET, handleClockGet);
    server.on("/api/clock", HTTP_POST, handleClockPost);
    server.on("/api/display", HTTP_GET, handleDisplayGet);
    server.on("/api/display", HTTP_POST, handleDisplayPost);
    server.on("/api/tasks", HTTP_GET, handleTasksGet);
    server.on("/api/tasks", HTTP_POST, handleTasksPost);
    server.on("/api/weather", HTTP_GET, handleWeatherGet);
    server.on("/api/weather", HTTP_POST, handleWeatherPost);
    server.on("/api/units", HTTP_GET, handleUnitsGet);
    server.on("/api/units", HTTP_POST, handleUnitsPost);
    server.on("/api/markets", HTTP_GET, handleMarketsGet);
    server.on("/api/markets", HTTP_POST, handleMarketsPost);
    server.on("/api/dismiss", HTTP_POST, handleDismiss);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
    s_handlersRegistered = true;
  }

  if (!s_fsReady) {
    s_fsReady = LittleFS.begin();
    if (s_fsReady) {
      Serial.println("[web] LittleFS mounted");
    } else {
      // API routes remain available without the filesystem.
      Serial.println("[web] LittleFS MOUNT FAILED - run: pio run -t uploadfs");
    }
  }

  if (s_running)
    server.stop();
  server.begin();
  s_running = true;
  Serial.println("[web] listening on :80");
}

void webStop() {
  if (!s_running)
    return;
  server.stop();
  s_running = false;
  Serial.println("[web] stopped");
}

void webService() {
  if (s_running)
    server.handleClient();
}

bool webTakeDismissRequest() {
  if (!s_dismissRequested)
    return false;
  s_dismissRequested = false;
  return true;
}

bool webTakeSyncRequest() {
  if (!s_syncRequested)
    return false;
  s_syncRequested = false;
  return true;
}
