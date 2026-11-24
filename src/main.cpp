// Coordinates startup, button input, network updates, and screen refreshes.

#include <Arduino.h>

#include "app_settings.h"
#include "clock_time.h"
#include "config.h"
#include "markets.h"
#include "panel.h"
#include "screens.h"
#include "tasks.h"
#include "weather.h"
#include "web_ui.h"
#include "wifi_net.h"

static AppState g_state;
static ScreenId g_screen = SCREEN_CLOCK;

// INFO takes over the panel until dismissed, then hands back to g_screen.
static bool g_showingInfo = false;

// Which minute INFO is showing, so it repaints on the boundary.
static uint32_t g_drawnInfoMinute = 0xFFFFFFFF;

static uint32_t g_lastScreenSwitch = 0;
static uint32_t g_lastNetPoll = 0;
static uint32_t g_lastClockPoll = 0;
static uint32_t g_lastBattPoll = 0;

// Cached from NVS, re-read when the web page pushes a change.
static uint8_t g_displayMode = DEFAULT_DISPLAY_MODE;
static uint32_t g_dwellMs = (uint32_t)DEFAULT_DWELL_MINUTES * 60000UL;
static bool g_drawnSynced = false;
static char g_drawnDate[sizeof(g_state.tasksDate)];
static uint8_t g_drawnMinute = 255; // 255 = nothing drawn yet
static uint8_t g_drawnHour = 255;

static ScreenId activeScreen() { return g_showingInfo ? SCREEN_INFO : g_screen; }

uint8_t webStatusBattery() { return g_state.batteryPct; }
ScreenId webStatusScreen() { return activeScreen(); }

static void renderActive(Adafruit_GFX &g) { drawScreen(g, activeScreen(), g_state); }

// The off-screen canvas lets identical full frames skip a panel refresh.
static GFXcanvas1 *g_probe = nullptr;
static uint32_t g_shownHash = 0;
static bool g_haveHash = false;

// The canvas and panel both store eight horizontal pixels per byte.
static uint32_t canvasHash(Rect exclude = {0, 0, 0, 0}) {
  const uint8_t *pixels = g_probe->getBuffer();
  const int stride = (SCREEN_W + 7) / 8;
  const int left = exclude.x / 8;
  const int right = (exclude.x + exclude.w + 7) / 8;
  uint32_t hash = 2166136261u; // FNV-1a
  for (int y = 0; y < SCREEN_H; ++y) {
    const bool withinRows = y >= exclude.y && y < exclude.y + exclude.h;
    for (int x = 0; x < stride; ++x) {
      if (!withinRows || x < left || x >= right) {
        hash ^= pixels[y * stride + x];
        hash *= 16777619u;
      }
    }
  }
  return hash;
}

static void noteFullFrame() {
  g_drawnMinute = g_state.minute;
  g_drawnHour = g_state.hour;
  g_drawnSynced = g_state.timeSynced;
  snprintf(g_drawnDate, sizeof(g_drawnDate), "%s", g_state.tasksDate);
}

static void redrawFull(const char *reason) {
  weatherCopyInto(g_state);
  marketsCopyInto(g_state);

  if (g_probe)
    renderActive(*g_probe);
  const uint32_t hash = g_probe ? canvasHash() : 0;
  if (!g_haveHash || hash != g_shownHash) {
    Serial.printf("[panel] full refresh: %s\n", reason);
    panelDrawFull(renderActive);
    g_shownHash = hash;
    g_haveHash = g_probe != nullptr;
  }

  noteFullFrame();
}

static void redrawPartial(Rect area) {
  const bool full = panelDrawPartial(area.x, area.y, area.w, area.h, renderActive);
  const bool wholeFrame = full || (area.w == SCREEN_W && area.h == SCREEN_H);
  if (wholeFrame)
    noteFullFrame();
  if (!g_probe)
    return;

  const uint32_t previousOutside = g_haveHash && !wholeFrame ? canvasHash(area) : 0;
  renderActive(*g_probe);
  // A Wi-Fi update can coincide with a clock tick outside its window.
  g_haveHash = wholeFrame || (g_haveHash && canvasHash(area) == previousOutside);
  if (g_haveHash)
    g_shownHash = canvasHash();
}

static void copyTimeIntoState() {
  struct tm t;
  if (!clockGetLocal(t)) {
    // No RTC, so a cold boot doesn't know the time.
    g_state.timeSynced = false;
    g_state.hour = 0;
    g_state.minute = 0;
    snprintf(g_state.clockDate, sizeof(g_state.clockDate), "%s", "WAITING FOR NTP");
    snprintf(g_state.tasksDate, sizeof(g_state.tasksDate), "%s", "WAITING FOR NTP");
    return;
  }

  g_state.timeSynced = true;
  g_state.hour = (uint8_t)t.tm_hour;
  g_state.minute = (uint8_t)t.tm_min;
  clockFormatDate(t, g_state.clockDate, sizeof(g_state.clockDate));
  clockFormatDateLong(t, g_state.tasksDate, sizeof(g_state.tasksDate));
}

// Only this function updates the connection state and starts or stops network services.
static bool pollNetwork() {
  const bool wasConnected = g_state.wifiConnected;
  WifiStatus w;
  wifiGetStatus(w);

  g_state.wifiConnected = w.connected;
  g_state.rssi = w.rssi;
  g_state.uptimeSec = millis() / 1000UL;
  snprintf(g_state.ip, sizeof(g_state.ip), "%s", w.ip);
  snprintf(g_state.ssid, sizeof(g_state.ssid), "%s", w.ssid);
  snprintf(g_state.security, sizeof(g_state.security), "%s", w.security);

  if (wasConnected == w.connected)
    return false;

  if (w.connected) {
    webBegin();
    clockStartSync();
    weatherRequestFetch();
    marketsRequestFetch();
  } else {
    webStop();
  }
  return true;
}

static void reloadDisplaySettings() {
  g_displayMode = settingsDisplayMode();
  const ScreenId pinned = (ScreenId)settingsStaticScreen();
  g_dwellMs = (uint32_t)settingsDwellMinutes() * 60000UL;
  g_state.darkMode = settingsDarkMode();

  if (g_displayMode == DISPLAY_STATIC)
    g_screen = pinned < SCREEN_ROTATION_COUNT ? pinned : SCREEN_CLOCK;

  char posix[TZ_POSIX_MAX];
  settingsGetTz(posix, sizeof(posix), g_state.tzLabel, sizeof(g_state.tzLabel));
  g_state.showDate = settingsShowDate();
}

static void goToNextRotationScreen() {
  g_screen = (ScreenId)((g_screen + 1) % SCREEN_ROTATION_COUNT);
  redrawFull("screen rotation");
  g_lastScreenSwitch = millis();
}

// Before NTP the clock minute is stuck at 0, so fall back to uptime.
static uint32_t infoMinute() { return g_state.timeSynced ? (uint32_t)g_state.minute : (millis() / 60000UL); }

static void showInfoScreen() {
  g_showingInfo = true;
  g_drawnInfoMinute = infoMinute();
  redrawFull("info screen shown");
}

static void leaveInfoScreen() {
  settingsMarkInfoShown();

  g_showingInfo = false;
  redrawFull("left the info screen");
  g_lastScreenSwitch = millis();
}

// Short press = next screen, 3 second hold = reset.
static ButtonEvent pollButton() {
  static bool down = false;
  static bool raw = false;
  static uint32_t changedAt = 0;
  static uint32_t downAt = 0;
  static bool longFired = false;

  const bool now = (digitalRead(BUTTON_PIN) == LOW);
  if (now != raw) {
    raw = now;
    changedAt = millis();
  }

  // Keep the last value until the pin has been stable long enough.
  const bool debounced = (millis() - changedAt > BUTTON_DEBOUNCE_MS) ? raw : down;

  if (debounced && !down) { // pressed
    down = true;
    downAt = millis();
    longFired = false;
  } else if (debounced && down) { // still held
    if (!longFired && millis() - downAt >= BUTTON_LONG_MS) {
      longFired = true; // fire the instant 3s is reached
      return BTN_LONG;
    }
  } else if (!debounced && down) { // released
    down = false;
    if (!longFired)
      return BTN_SHORT; // short press fires on release
  }

  return BTN_NONE;
}

// Averaged, since a single ADC read swings by tens of millivolts. Li-ion
// doesn't fall linearly, so these steps are uneven.
static uint8_t batteryLevel() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < BATTERY_SAMPLES; i++) {
    sum += analogReadMilliVolts(BATTERY_PIN);
  }

  const float v = ((float)sum / BATTERY_SAMPLES / 1000.0f) * BATTERY_DIVIDER;

  if (v >= BATTERY_V_100)
    return 100;
  if (v >= BATTERY_V_75)
    return 75;
  if (v >= BATTERY_V_50)
    return 50;
  if (v >= BATTERY_V_25)
    return 25;
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(SERIAL_BOOT_SETTLE_MS); // let the USB host attach before printing
  Serial.println("\nDesk Dashboard");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  settingsBegin();
  tasksBegin();
  weatherBegin();
  marketsBegin();
  clockBegin(); // applies the saved timezone before anything renders
  reloadDisplaySettings();

  copyTimeIntoState();
  g_state.taskCount = tasksCopyInto(g_state.tasks);

  // 15KB for the frame guard's canvas.
  static GFXcanvas1 canvas(SCREEN_W, SCREEN_H);
  if (canvas.getBuffer())
    g_probe = &canvas;

  panelBegin();

  // Doesn't wait for Wi-Fi. With no connection the clock still draws.
  wifiBegin();
  pollNetwork();

  g_state.batteryPct = BATTERY_UNKNOWN;
  g_lastBattPoll = millis() - BATTERY_POLL_MS + 5000;

  // INFO on the first boot only. A 3 second BOOT hold brings it back.
  if (INFO_ALWAYS_ON_BOOT || !settingsInfoShown()) {
    showInfoScreen();
  } else {
    redrawFull("boot");
  }
  g_lastScreenSwitch = millis();
}

void loop() {
  wifiService();
  webService(); // same thread as the draws

  if (millis() - g_lastClockPoll >= CLOCK_POLL_MS) {
    g_lastClockPoll = millis();
    copyTimeIntoState();
  }

  if (millis() - g_lastBattPoll >= BATTERY_POLL_MS) {
    g_lastBattPoll = millis();
    const uint8_t was = g_state.batteryPct;
    g_state.batteryPct = batteryLevel();

    if (g_state.batteryPct != was) {
      redrawFull("battery level changed");
      return;
    }
  }

  if (millis() - g_lastNetPoll >= NET_POLL_MS) {
    g_lastNetPoll = millis();
    if (pollNetwork()) {
      redrawPartial(wifiDirtyRect(activeScreen()));
      g_drawnInfoMinute = infoMinute();
      return;
    }
  }

  if (webTakeSyncRequest()) {
    Serial.println("[web] settings synced to the panel");
    reloadDisplaySettings();
    copyTimeIntoState();
    g_state.taskCount = tasksCopyInto(g_state.tasks);
    redrawFull("web page pushed new settings");
    g_lastScreenSwitch = millis();
    return;
  }

  if (weatherTakeUpdate() && activeScreen() == SCREEN_WEATHER) {
    redrawFull("weather updated");
    return;
  }
  if (marketsTakeUpdate() && activeScreen() == SCREEN_MARKETS) {
    redrawFull("markets updated");
    return;
  }

  if (webTakeDismissRequest() && g_showingInfo) {
    Serial.println("[web] info screen dismissed");
    leaveInfoScreen();
    return;
  }

  switch (pollButton()) {
  case BTN_LONG:
    settingsFactoryReset();
    tasksReset();
    clockBegin();
    weatherApplySettings();
    marketsApplySettings();
    g_screen = SCREEN_CLOCK;
    reloadDisplaySettings();
    copyTimeIntoState();
    g_state.taskCount = tasksCopyInto(g_state.tasks);
    showInfoScreen();
    return;
  case BTN_SHORT:
    if (g_showingInfo)
      leaveInfoScreen();
    else
      goToNextRotationScreen();
    return;
  default:
    break;
  }

  // Info stays visible until dismissed and updates its time once a minute.
  if (g_showingInfo) {
    if (infoMinute() != g_drawnInfoMinute) {
      g_drawnInfoMinute = infoMinute();
      redrawPartial({0, 0, SCREEN_W, SCREEN_H});
    }
    delay(20);
    return;
  }

  // Static mode never auto-advances, though a short press still steps through.
  if (g_displayMode == DISPLAY_CYCLE && millis() - g_lastScreenSwitch >= g_dwellMs) {
    goToNextRotationScreen();
    return;
  }

  // NTP landed, so the whole clock screen changes at once.
  if (g_state.timeSynced != g_drawnSynced) {
    Serial.printf("[clock] %s\n", g_state.timeSynced ? "synced" : "lost sync");
    redrawFull("NTP sync state changed");
    return;
  }

  if (g_screen == SCREEN_TASKS && strcmp(g_drawnDate, g_state.tasksDate) != 0) {
    redrawFull("task date changed");
  }

  if (g_screen == SCREEN_CLOCK) {
    if (g_state.hour != g_drawnHour || strcmp(g_drawnDate, g_state.tasksDate) != 0) {
      // The date is outside the digit band, so this needs the whole screen.
      redrawFull("hour rolled over");
    } else if (g_state.minute != g_drawnMinute) {
      redrawPartial({CLOCK_TIME_BAND_X, CLOCK_TIME_BAND_Y, CLOCK_TIME_BAND_W, CLOCK_TIME_BAND_H});
      g_drawnMinute = g_state.minute;
    }
  }

  delay(20);
}
