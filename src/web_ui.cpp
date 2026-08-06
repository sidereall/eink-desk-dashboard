// web_ui.cpp - the config page and its API.
//
// Handlers never draw, it would stall the response. They save and set a flag.
// No authentication, by design. Inputs are validated, secrets are never sent back.
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

static WebServer server(80);
static bool s_running = false;
static bool s_handlersRegistered = false;
static bool s_fsReady = false;
static volatile bool s_dismissRequested = false;
static volatile bool s_syncRequested = false;

// The timezone list, for the dropdown.
static void handleTimezones() {
  String out;
  out.reserve(2600);
  out += '[';
  for (size_t i = 0; i < TIMEZONE_COUNT; i++) {
    if (i)
      out += ',';
    out += "{\"label\":\"";
    out += TIMEZONES[i].label;
    out += "\",\"posix\":\"";
    out += TIMEZONES[i].posix;
    out += "\"}";
  }
  out += ']';
  server.send(200, "application/json", out);
}

// Screen names, so the page's picker matches the ScreenId enum.
static void handleScreens() {
  String out;
  out.reserve(160);
  out += '[';
  for (uint8_t i = 0; i < SELECTABLE_SCREEN_COUNT; i++) {
    if (i)
      out += ',';
    out += "{\"id\":";
    out += i;
    out += ",\"name\":\"";
    out += screenName((ScreenId)i);
    out += "\"}";
  }
  out += ']';
  server.send(200, "application/json", out);
}

// Escapes quotes and backslashes, which would otherwise break the JSON.
static void appendJsonString(String &out, const char *s) {
  out += '"';
  for (const char *p = s; *p; p++) {
    if (*p == '"' || *p == '\\')
      out += '\\';
    out += *p;
  }
  out += '"';
}

static void handleTasksGet() {
  AppState tmp;
  tasksCopyInto(tmp);

  String out;
  out.reserve(420);
  out += '[';
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    if (i)
      out += ',';
    out += "{\"title\":";
    appendJsonString(out, tmp.tasks[i].title);
    out += ",\"done\":";
    out += tmp.tasks[i].done ? "true" : "false";
    out += '}';
  }
  out += ']';
  server.send(200, "application/json", out);
}

static void handleTasksPost() {
  Task incoming[MAX_TASKS];
  memset(incoming, 0, sizeof(incoming));

  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    char key[4];

    snprintf(key, sizeof(key), "t%u", (unsigned)i);
    const String title = server.hasArg(key) ? server.arg(key) : String("");
    snprintf(incoming[i].title, TASK_TITLE_MAX, "%s", title.c_str());

    snprintf(key, sizeof(key), "d%u", (unsigned)i);
    incoming[i].done = server.hasArg(key) && server.arg(key) == "1";
  }

  // tasksSetAll cleans the titles, drops blanks and closes any gaps.
  tasksSetAll(incoming, MAX_TASKS);

  s_syncRequested = true;
  server.send(200, "text/plain", "ok");
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

  String out;
  out.reserve(160);
  out += "{\"lat\":";
  out += String(lat, 4);
  out += ",\"lon\":";
  out += String(lon, 4);
  out += ",\"name\":";
  appendJsonString(out, name);
  out += '}';
  server.send(200, "application/json", out);
}

static void handleWeatherPost() {
  if (!server.hasArg("lat") || !server.hasArg("lon") || !server.hasArg("name")) {
    server.send(400, "text/plain", "missing field");
    return;
  }

  const float lat = server.arg("lat").toFloat();
  const float lon = server.arg("lon").toFloat();
  if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
    server.send(400, "text/plain", "bad coordinates");
    return;
  }

  // The browser strips accents too, but this is what guarantees it.
  char name[WX_NAME_MAX];
  asciiOnly(server.arg("name").c_str(), name, sizeof(name));
  if (name[0] == '\0') {
    server.send(400, "text/plain", "bad name");
    return;
  }

  settingsSetWeather(lat, lon, name);

  weatherApplySettings(); // push the new location to the fetch task
  weatherRequestFetch();  // don't wait for the next slot to try it

  s_syncRequested = true;
  server.send(200, "text/plain", "ok");
}

// Reports whether a key is set, never the key itself.
static void handleMarketsGet() {
  char body[96];
  snprintf(body, sizeof(body), "{\"configured\":%s,\"label\":\"%s\"}", settingsMarketConfigured() ? "true" : "false",
           MARKET_LABEL);
  server.send(200, "application/json", body);
}

static void handleMarketsPost() {
  if (!server.hasArg("key")) {
    server.send(400, "text/plain", "missing key");
    return;
  }

  const String key = server.arg("key");
  // Twelve Data keys are 32 characters. Accept a range, reject obvious junk.
  if (key.length() < 8 || key.length() >= MARKET_KEY_MAX) {
    server.send(400, "text/plain", "bad key length");
    return;
  }

  settingsSetMarketKey(key.c_str());
  marketsApplySettings(); // push the new key to the fetch task
  marketsRequestFetch();  // don't wait for the next slot to try it

  s_syncRequested = true;
  server.send(200, "text/plain", "ok");
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

  char body[320];
  snprintf(body, sizeof(body),
           "{\"tz\":\"%s\",\"label\":\"%s\",\"showDate\":%s,\"synced\":%s,"
           "\"infoSeen\":%s,\"now\":\"%s\"}",
           posix, label, settingsShowDate() ? "true" : "false", clockIsSynced() ? "true" : "false",
           settingsInfoShown() ? "true" : "false", now);
  server.send(200, "application/json", body);
}

// Current settings plus the allowed dwell range, which builds the dropdown.
static void handleDisplayGet() {
  char body[160];
  snprintf(body, sizeof(body),
           "{\"mode\":%u,\"screen\":%u,\"dwell\":%u,"
           "\"min\":%u,\"max\":%u,\"step\":%u}",
           (unsigned)settingsDisplayMode(), (unsigned)settingsStaticScreen(), (unsigned)settingsDwellMinutes(),
           (unsigned)DWELL_MIN_MINUTES, (unsigned)DWELL_MAX_MINUTES, (unsigned)DWELL_STEP_MINUTES);
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
  const bool showDate = server.hasArg("date") && server.arg("date") == "1";

  settingsSetTz(posix.c_str(), label.c_str());
  settingsSetShowDate(showDate);

  // No network needed, it only changes how the time is displayed.
  clockSetTimezone(posix.c_str());

  s_syncRequested = true;
  server.send(200, "text/plain", "ok");
}

static void handleDisplayPost() {
  if (!server.hasArg("mode") || !server.hasArg("screen") || !server.hasArg("dwell")) {
    server.send(400, "text/plain", "missing field");
    return;
  }

  const long mode = server.arg("mode").toInt();
  const long screen = server.arg("screen").toInt();
  const long dwell = server.arg("dwell").toInt();

  if (mode != DISPLAY_CYCLE && mode != DISPLAY_STATIC) {
    server.send(400, "text/plain", "bad mode");
    return;
  }
  if (screen < 0 || screen >= SELECTABLE_SCREEN_COUNT) {
    server.send(400, "text/plain", "bad screen");
    return;
  }
  if (dwell < DWELL_MIN_MINUTES || dwell > DWELL_MAX_MINUTES || (dwell % DWELL_STEP_MINUTES) != 0) {
    server.send(400, "text/plain", "bad dwell");
    return;
  }

  settingsSetDisplayMode((uint8_t)mode);
  settingsSetStaticScreen((uint8_t)screen);
  settingsSetDwellMinutes((uint8_t)dwell);

  s_syncRequested = true;
  server.send(200, "text/plain", "ok");
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

void webBegin() {
  if (!s_handlersRegistered) {
    // serveStatic 404s here, so the page is served by handleRoot instead.
    server.on("/", HTTP_GET, handleRoot);
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
    server.on("/api/markets", HTTP_GET, handleMarketsGet);
    server.on("/api/markets", HTTP_POST, handleMarketsPost);
    server.on("/api/dismiss", HTTP_POST, handleDismiss);
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
    s_handlersRegistered = true;
  }

  if (!s_fsReady) {
    s_fsReady = LittleFS.begin();
    if (s_fsReady) {
      Serial.println("[web] LittleFS mounted");
    } else {
      // The API still works, but the page will 404 until the files are uploaded.
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

bool webIsRunning() { return s_running; }

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
