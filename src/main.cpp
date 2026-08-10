// main.cpp - setup, the main loop, and everything that draws to the panel.
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

// Everything the screens draw from.
static AppState g_state;

// The current rotation screen. Never SCREEN_INFO, that's tracked separately.
static ScreenId g_screen = SCREEN_CLOCK;

static bool g_rotationEnabled[SCREEN_ROTATION_COUNT] = {
    /* CLOCK   */ true,
    /* TASKS   */ true,
    /* WEATHER */ true,
    /* MARKETS */ true,
};

// INFO takes over the panel until dismissed, then hands back to g_screen.
static bool g_showingInfo = false;

// Which minute INFO is showing, so it repaints on the boundary.
static uint32_t g_drawnInfoMinute = 0xFFFFFFFF;

static uint32_t g_lastScreenSwitch = 0;
static uint32_t g_lastNetPoll = 0;
static uint32_t g_lastClockPoll = 0;
static bool g_drawnSynced = false;

// Cached from NVS, re-read when the web page pushes a change.
static uint8_t g_displayMode = DEFAULT_DISPLAY_MODE;
static ScreenId g_staticScreen = (ScreenId)DEFAULT_STATIC_SCREEN;
static uint32_t g_dwellMs = (uint32_t)DEFAULT_DWELL_MINUTES * 60000UL;
static uint8_t g_drawnMinute = 255; // 255 = nothing drawn yet
static uint8_t g_drawnHour = 255;

// Whichever screen is on the panel right now, INFO included.
static ScreenId activeScreen() { return g_showingInfo ? SCREEN_INFO : g_screen; }

// Passed to the panel, which calls it to draw a frame.
static void renderActive(Adafruit_GFX &g) { drawScreen(g, activeScreen(), g_state); }

// The frame guard: renders off-screen and hashes it, so a refresh only happens
// when the picture actually changes.
static GFXcanvas1 *g_probe = nullptr;
static uint32_t g_shownHash = 0;
static bool g_haveHash = false;

static uint32_t frameHash() {
  if (!g_probe)
    return 0;

  g_probe->fillScreen(0);
  drawScreen(*g_probe, activeScreen(), g_state);

  const uint8_t *buf = g_probe->getBuffer();
  const size_t n = (size_t)(SCREEN_W / 8) * SCREEN_H;

  uint32_t h = 2166136261u; // FNV-1a
  for (size_t i = 0; i < n; i++) {
    h ^= buf[i];
    h *= 16777619u;
  }
  return h;
}

// Full refresh, skipped if the frame is unchanged. Logs why it fired.
static void redrawFull(const char *reason) {
  // Cheap struct copies, keeps the stale flags current.
  weatherCopyInto(g_state);
  marketsCopyInto(g_state);

  const uint32_t h = frameHash();
  if (g_haveHash && h == g_shownHash) {
    Serial.printf("[panel] SKIPPED (%s) - the frame is already on screen\n", reason);
    g_drawnMinute = g_state.minute;
    g_drawnHour = g_state.hour;
    return;
  }

  Serial.printf("[panel] FULL refresh  <- %s\n", reason);
  panelDrawFull(renderActive);

  g_shownHash = h;
  g_haveHash = true;
  g_drawnMinute = g_state.minute;
  g_drawnHour = g_state.hour;
}

// A partial refresh changes the panel too, so the stored hash has to follow.
static void noteFrameShown() {
  g_shownHash = frameHash();
  g_haveHash = true;
}

// Reads the clock and the saved timezone into AppState.
static void copyTimeIntoState() {
  char posix[TZ_POSIX_MAX];
  settingsGetTz(posix, sizeof(posix), g_state.tzLabel, sizeof(g_state.tzLabel));
  g_state.showDate = settingsShowDate();

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

// Reads the live radio state into AppState.
static void copyNetworkIntoState() {
  WifiStatus w;
  wifiGetStatus(w);

  g_state.wifiConnected = w.connected;
  g_state.rssi = w.rssi;
  g_state.uptimeSec = millis() / 1000UL;
  snprintf(g_state.ip, sizeof(g_state.ip), "%s", w.ip);
  snprintf(g_state.ssid, sizeof(g_state.ssid), "%s", w.ssid);
  snprintf(g_state.security, sizeof(g_state.security), "%s", w.security);
}

// Re-reads the display settings from NVS into the cached globals.
static void reloadDisplaySettings() {
  g_displayMode = settingsDisplayMode();
  g_staticScreen = (ScreenId)settingsStaticScreen();
  g_dwellMs = (uint32_t)settingsDwellMinutes() * 60000UL;
  g_state.darkMode = settingsDarkMode();

  if (g_displayMode == DISPLAY_STATIC)
    g_screen = g_staticScreen;

  Serial.printf("[display] %s, screen=%s, dwell=%umin\n", (g_displayMode == DISPLAY_STATIC) ? "static" : "cycle",
                screenName(g_staticScreen), (unsigned)(g_dwellMs / 60000UL));
}

// How many screens are in the rotation.
static uint8_t rotationCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < SCREEN_ROTATION_COUNT; i++) {
    if (g_rotationEnabled[i])
      n++;
  }
  return n;
}

// Advances to the next enabled screen and redraws.
static void goToNextRotationScreen() {
  for (uint8_t step = 1; step <= SCREEN_ROTATION_COUNT; step++) {
    const ScreenId candidate = (ScreenId)((g_screen + step) % SCREEN_ROTATION_COUNT);
    if (g_rotationEnabled[candidate]) {
      g_screen = candidate;
      break;
    }
  }
  g_showingInfo = false;
  redrawFull("rotated to a new screen");
  g_lastScreenSwitch = millis();
}

// Before NTP the clock minute is stuck at 0, so fall back to uptime.
static uint32_t infoMinute() { return g_state.timeSynced ? (uint32_t)g_state.minute : (millis() / 60000UL); }

static void showInfoScreen() {
  copyNetworkIntoState();
  g_showingInfo = true;
  g_drawnInfoMinute = infoMinute();
  redrawFull("info screen shown");
}

static void leaveInfoScreen() {
  // Dismissing is the acknowledgement, so the flag is only written now.
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

void setup() {
  Serial.begin(115200);
  delay(SERIAL_BOOT_SETTLE_MS); // let the USB host attach before printing
  Serial.println("\nDesk Dashboard ");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  settingsBegin();
  tasksBegin();
  weatherBegin();
  marketsBegin();
  clockBegin(); // applies the saved timezone before anything renders
  reloadDisplaySettings();

  copyTimeIntoState();
  tasksCopyInto(g_state);
  weatherCopyInto(g_state);

  // 15KB for the frame guard's canvas.
  g_probe = new GFXcanvas1(SCREEN_W, SCREEN_H);
  if (!g_probe || !g_probe->getBuffer()) {
    Serial.println("[panel] no memory for the frame guard - every redraw will run");
    g_probe = nullptr;
  }

  panelBegin();

  // Doesn't wait for Wi-Fi. With no connection the clock still draws.
  wifiBegin();
  copyNetworkIntoState();

  if (!g_rotationEnabled[g_screen]) {
    for (uint8_t i = 0; i < SCREEN_ROTATION_COUNT; i++) {
      if (g_rotationEnabled[i]) {
        g_screen = (ScreenId)i;
        break;
      }
    }
  }

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

  // Wi-Fi came up or dropped
  if (millis() - g_lastNetPoll >= NET_POLL_MS) {
    g_lastNetPoll = millis();

    const bool was = g_state.wifiConnected;
    copyNetworkIntoState();

    if (g_state.wifiConnected != was) {
      if (g_state.wifiConnected) {
        Serial.printf("[net] up: %s  %s  %d dBm\n", g_state.ip, g_state.security, (int)g_state.rssi);
        webBegin();
        clockStartSync();      // NTP needs a network
        weatherRequestFetch(); // Weather needs network
        marketsRequestFetch(); // Quote needs network
      } else {
        Serial.println("[net] down");
        webStop();
      }

      // Only the icon changed, so repaint just its area. On INFO that area is
      // the whole screen, since every field there is network state.
      const Rect r = wifiDirtyRect(activeScreen());
      panelDrawPartial(r.x, r.y, r.w, r.h, renderActive);
      noteFrameShown();
      g_drawnInfoMinute = infoMinute();
      return;
    }
  }

  // A Save button on the web page. Already saved, so just reload and redraw.
  if (webTakeSyncRequest()) {
    Serial.println("[web] settings synced to the panel");
    reloadDisplaySettings();
    copyTimeIntoState();
    tasksCopyInto(g_state);
    redrawFull("web page pushed new settings");
    g_lastScreenSwitch = millis();
    return;
  }

  // Fresh data. Only repaint if that screen is the one showing.
  if (weatherTakeUpdate()) {
    weatherCopyInto(g_state);
    if (activeScreen() == SCREEN_WEATHER) {
      redrawFull("fresh weather data landed");
      return;
    }
  }

  if (marketsTakeUpdate()) {
    marketsCopyInto(g_state);
    if (activeScreen() == SCREEN_MARKETS) {
      redrawFull("fresh market data landed");
      return;
    }
  }

  if (webTakeDismissRequest() && g_showingInfo) {
    Serial.println("[web] info screen dismissed");
    leaveInfoScreen();
    return;
  }

  switch (pollButton()) {
  case BTN_LONG:
    // Wipes everything and shows INFO again, as if it were new.
    Serial.println("[btn] long press - factory reset");
    settingsFactoryReset();  // timezone, date toggle, display mode, weather, location, info flag
    tasksReset();            // task list
    clockBegin();            // re-reads the (now default) timezone: UTC
    weatherApplySettings();  // location is gone -> back to "SET A LOCATION"
    marketsApplySettings();  // key is gone -> back to "SET API KEY"
    reloadDisplaySettings(); // back to cycling, 5 minutes
    copyTimeIntoState();
    tasksCopyInto(g_state);
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

  // INFO doesn't time out. It repaints once a minute to keep TIME and UPTIME
  // live, full screen but partial, so there's no flash.
  if (g_showingInfo) {
    if (infoMinute() != g_drawnInfoMinute) {
      g_drawnInfoMinute = infoMinute();
      copyNetworkIntoState(); // signal, uptime
      panelDrawPartial(0, 0, SCREEN_W, SCREEN_H, renderActive);
      noteFrameShown();
    }
    delay(20);
    return;
  }

  // Static mode never auto-advances, though a short press still steps through.
  if (g_displayMode == DISPLAY_CYCLE && rotationCount() > 1 && millis() - g_lastScreenSwitch >= g_dwellMs) {
    goToNextRotationScreen();
    return;
  }

  // NTP landed, so the whole clock screen changes at once.
  if (g_state.timeSynced != g_drawnSynced) {
    g_drawnSynced = g_state.timeSynced;
    Serial.printf("[clock] %s\n", g_state.timeSynced ? "synced" : "lost sync");
    redrawFull("NTP sync state changed");
    return;
  }

  if (g_screen == SCREEN_CLOCK) {
    if (g_state.hour != g_drawnHour) {
      // The date is outside the digit band, so this needs the whole screen.
      redrawFull("hour rolled over");
    } else if (g_state.minute != g_drawnMinute) {
      panelDrawPartial(CLOCK_TIME_BAND_X, CLOCK_TIME_BAND_Y, CLOCK_TIME_BAND_W, CLOCK_TIME_BAND_H,
                       renderActive); // digits only, no flash
      noteFrameShown();
      g_drawnMinute = g_state.minute;
    }
  }

  delay(20);
}
