// config.h - pin map, panel, refresh policy, screen layout, network timings
#pragma once
#include <stddef.h>
#include <stdint.h>

// PIN MAP ------------------------------------------------------------
// E-ink to ESP32-C6 DevKit wiring
#define CS_PIN 10
#define DC_PIN 19
#define RES_PIN 18
#define BUSY_PIN 21
#define SCK_PIN 6
#define MOSI_PIN 7

// BOOT button on the ESP32-C6 DevKit.
// short press - next screen in the rotation (or dismiss INFO)
// 3s hold     - reset: wipes every stored setting
#define BUTTON_PIN 9

// PANEL  --------------------------------------------------------------
constexpr int16_t SCREEN_W = 400;
constexpr int16_t SCREEN_H = 300;

// GxEPD_BLACK / GxEPD_WHITE by value, so screens.cpp doesnt need GxEPD2
constexpr uint16_t COL_BLACK = 0x0000;
constexpr uint16_t COL_WHITE = 0xFFFF;

// Foreground and background for the current theme, set once per frame.
extern uint16_t COL_FG;
extern uint16_t COL_BG;

// REFRESH POLICY ----------------------------------------------------
// Number of partial refreshes, before a full screen refresh
constexpr uint8_t MAX_PARTIALS_BEFORE_FULL = 10;

// Screen rotation. Configurable on the web page.
enum DisplayMode : uint8_t {
  DISPLAY_CYCLE = 0,
  DISPLAY_STATIC = 1,
};

// Minutes a screen stays up before rotating. Every rotation is a full refresh.
constexpr uint8_t DWELL_MIN_MINUTES = 5;
constexpr uint8_t DWELL_MAX_MINUTES = 60;
constexpr uint8_t DWELL_STEP_MINUTES = 5;
constexpr uint8_t DEFAULT_DWELL_MINUTES = 5;

// Defaults used until the user saves something.
constexpr uint8_t DEFAULT_DISPLAY_MODE = DISPLAY_CYCLE;
constexpr uint8_t DEFAULT_STATIC_SCREEN = 0; // clock

// Screens the user is allowed to set (clock, tasks, weather, markets)
constexpr uint8_t SELECTABLE_SCREEN_COUNT = 4;

// How often loop() checks the time.
constexpr uint32_t CLOCK_POLL_MS = 500;

// LAYOUT --------------------------------------------------------------
// Rounded corners on all screens
constexpr int16_t FRAME_RADIUS = 22;

// Shared header — TASKS / WEATHER / MARKETS
constexpr int16_t HDR_TITLE_X = 21;
constexpr int16_t HDR_TITLE_Y = 37;
constexpr uint8_t HDR_TITLE_SIZE = 4;

// Shared header subtitle
constexpr int16_t HDR_SUB_X = 23;
constexpr int16_t HDR_SUB_Y = 55;
constexpr uint8_t HDR_SUB_SIZE = 2;
constexpr int16_t HDR_SUB_MAX_W = 201;

// Horizontal divider line under the header
constexpr int16_t HDR_RULE_Y = 61;
constexpr int16_t HDR_RULE_X0 = 20;
constexpr int16_t HDR_RULE_X1 = 379;

// Wi-Fi icon location
constexpr int16_t HDR_WIFI_X = 364;
constexpr int16_t HDR_WIFI_Y = 21;

// Bitmap dimensions matching the arrays in icons.h
// Wi-Fi icon size
constexpr int16_t ICON_WIFI_W = 16;
constexpr int16_t ICON_WIFI_H = 16;

// Weather 4 day forecast icon size
constexpr int16_t ICON_SMALL_W = 48;
constexpr int16_t ICON_SMALL_H = 48;

// Weather current day icon size
constexpr int16_t ICON_BIG_W = 96;
constexpr int16_t ICON_BIG_H = 96;

// Large degree Celsius icon size
constexpr int16_t ICON_DEG_LARGE_W = 38;
constexpr int16_t ICON_DEG_LARGE_H = 30;

// Small degree Celsius icon size
constexpr int16_t ICON_DEG_SMALL_W = 16;
constexpr int16_t ICON_DEG_SMALL_H = 10;

// Centred "not set up yet" messages, shared by weather and markets
constexpr int16_t MSG_LINE1_Y = 150;
constexpr int16_t MSG_LINE2_Y = 185;
constexpr int16_t MSG_SINGLE_Y = 165;

// CLOCK SCREEN ---------------------------------------------------------------
// Clock digits
constexpr uint8_t CLOCK_DIGIT_SIZE = 15;
constexpr int16_t CLOCK_DIGIT_Y = 160;
constexpr int16_t CLOCK_DIGIT_GAP = 30;
constexpr int8_t CLOCK_OUTLINE_PX = 2; // Hollow minute stroke width

// Date under the digits. X is computed at draw time so it stays centred.
constexpr uint8_t CLOCK_DATE_SIZE = 4;
constexpr int16_t CLOCK_DATE_Y = 207;

constexpr int16_t CLOCK_WIFI_X = 374;
constexpr int16_t CLOCK_WIFI_Y = 10;

// Replaces the time digits until NTP is fetched. Centered on panel.
constexpr int16_t CLOCK_NOSYNC_Y = 160;

// The rectangle the clock digits sit in. On a minute change only this area is redrawn.
// X and W must be multiples of 8 or the update smears.
constexpr int16_t CLOCK_TIME_BAND_X = 0;
constexpr int16_t CLOCK_TIME_BAND_Y = 60;
constexpr int16_t CLOCK_TIME_BAND_W = 400;
constexpr int16_t CLOCK_TIME_BAND_H = 124;

// TASKS SCREEN -------------------------------------------------------------------
// Max number of tasks displayed on screen
constexpr uint8_t MAX_TASKS = 5;

// Titles longer than the screen fits get truncated with "..."
constexpr size_t TASK_TITLE_MAX = 48;

// One rounded rectangle box per task
constexpr int16_t TASK_BOX_X = 20;
constexpr int16_t TASK_BOX_Y0 = 70;
constexpr int16_t TASK_BOX_W = 360;
constexpr int16_t TASK_BOX_H = 37;
constexpr int16_t TASK_BOX_RADIUS = 18;
constexpr int16_t TASK_ROW_PITCH = 43;

// Task checkbox circle
constexpr int16_t TASK_CHECK_CX = 39;
constexpr int16_t TASK_CHECK_DY = 18;
constexpr int16_t TASK_CHECK_R = 8;

// Task title text
constexpr int16_t TASK_TEXT_X = 56;
constexpr int16_t TASK_TEXT_DY = 23;
constexpr uint8_t TASK_TEXT_SIZE = 3;

// Text max width to fit the rounded rectangle box
constexpr int16_t TASK_TEXT_MAX_W = (TASK_BOX_X + TASK_BOX_W) - TASK_TEXT_X - 10;

// Strikethrough on a done task, positioned to cross the letters
constexpr int16_t TASK_STRIKE_OFFSET = 6;
constexpr int16_t TASK_STRIKE_THICK = 2;

// Tasks done tracking e.g. "0 out of 5 done". Fixed length.
constexpr int16_t TASK_COUNT_X = 261;
constexpr int16_t TASK_COUNT_Y = 55;

// WEATHER SCREEN  -------------------------------------------------------------------
// Last updated time "UPDATED HH:MM" in header
constexpr int16_t WX_UPDATED_X = 234;
constexpr int16_t WX_UPDATED_Y = 55;

// Current temperature, with its degree symbol
constexpr uint8_t WX_TEMP_SIZE = 9;
constexpr int16_t WX_TEMP_X = 24;
constexpr int16_t WX_TEMP_Y = 121;
constexpr int16_t WX_DEG_LARGE_Y = 100;

// Current weather condition text e.g. "PARTLY CLOUDY"
constexpr int16_t WX_STATUS_X = 24;
constexpr int16_t WX_STATUS_Y = 148;

// Todays highest and lowest temperatures
// e.g. "H: 24" and "L: 20" plus a small degree symbol
constexpr int16_t WX_HL_Y = 165;
constexpr int16_t WX_HIGH_X = 25;
constexpr int16_t WX_LOW_X = 109;
constexpr int16_t WX_HL_DEG_Y = 157;

// Current weather condition icon
constexpr int16_t WX_BIG_ICON_X = 234;
constexpr int16_t WX_BIG_ICON_Y = 77;

// Four day forecast cards along the bottom
constexpr int16_t WX_CARD_X0 = 20;
constexpr int16_t WX_CARD_Y = 186;
constexpr int16_t WX_CARD_W = 85;
constexpr int16_t WX_CARD_H = 94;
constexpr int16_t WX_CARD_R = 20;
constexpr int16_t WX_CARD_PITCH = 92;
constexpr uint8_t WX_CARD_COUNT = 4;

// Contents of each forecast card, offset from that card's top-left corner
constexpr int16_t WX_CARD_DAY_DX = 17;
constexpr int16_t WX_CARD_DAY_DY = 22;
constexpr int16_t WX_CARD_ICON_DX = 19;
constexpr int16_t WX_CARD_ICON_DY = 25;
constexpr int16_t WX_CARD_TEMP_DX = 22;
constexpr int16_t WX_CARD_TEMP_DY = 80;
constexpr int16_t WX_CARD_DEG_DY = 72;

// Gap between a temperature and its degree glyph.
constexpr int16_t WX_DEG_GAP = 4;

// MARKETS SCREEN  ---------------------------------------------------------------
// Last updated time "UPDATED HH:MM" in header
constexpr int16_t MK_UPDATED_X = 234;
constexpr int16_t MK_UPDATED_Y = 55;

// Market rounded rect, contains: name, price, change arrow
constexpr int16_t MK_BOX_X = 20;
constexpr int16_t MK_BOX_Y = 70;
constexpr int16_t MK_BOX_W = 360;
constexpr int16_t MK_BOX_H = 48;
constexpr int16_t MK_BOX_R = 20;

// Symbol name and price
constexpr uint8_t MK_NAME_SIZE = 3;
constexpr int16_t MK_NAME_X = 37;
constexpr int16_t MK_NAME_Y = 98;
constexpr int16_t MK_PRICE_X = 172;
constexpr int16_t MK_PRICE_Y = 98;

// Up/down change arrow
constexpr int16_t MK_TRI_CX = 308;
constexpr int16_t MK_TRI_TOP_Y = 88;
constexpr int16_t MK_TRI_BOT_Y = 98;
constexpr int16_t MK_TRI_HALF_W = 5;

// Percent change, e.g. "0.22%"
constexpr int16_t MK_PCT_X = 317;
constexpr int16_t MK_PCT_Y = 97;

// Sparkline box
constexpr int16_t MK_CHART_X = 20;
constexpr int16_t MK_CHART_Y = 123;
constexpr int16_t MK_CHART_W = 360;
constexpr int16_t MK_CHART_H = 158;
constexpr int16_t MK_CHART_R = 20;

// Gap from the chart box edges to the sparkline
constexpr int16_t MK_SPARK_PAD_X = 14;
constexpr int16_t MK_SPARK_PAD_TOP = 16;
constexpr int16_t MK_SPARK_PAD_BOT = 16;

// Centred fallback message in the chart box
constexpr int16_t MK_MSG_Y = 210;

// DEVICE IDENTITY ------------------------------------------------------------------
#define DEVICE_HOSTNAME "dashboard" // router DHCP name

// TIME -----------------------------------------------------------------------------
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "time.google.com"

// Default time zone
#define DEFAULT_TZ_POSIX "UTC0"
#define DEFAULT_TZ_LABEL "UTC"

// Max lengths for the timezone's POSIX rule string and its on-screen label
constexpr size_t TZ_POSIX_MAX = 48;
constexpr size_t TZ_LABEL_MAX = 40;

// WEATHER -------------------------------------------------------------------------
constexpr size_t WX_NAME_MAX = 24; // city name

// Fetch on the clock (10:30, 11:00...) so the update time doesn't drift.
constexpr uint8_t WEATHER_SLOT_MINUTES = 30;

// Offset the fetch a few seconds so it doesn't land on :00/:30, where the API gets overloaded.
constexpr uint16_t WEATHER_SLOT_OFFSET_SEC = 35;

// Ignore the next-fetch time if its close.
constexpr int32_t FETCH_SLOT_MIN_GAP_SEC = 5;

// Fallback only, for when the clock isn't synced and slots can't be computed.
constexpr uint32_t WEATHER_FETCH_MS = 30UL * 60 * 1000;

// After a failed fetch, retry this soon instead of waiting for the next slot.
constexpr uint32_t WEATHER_RETRY_MS = 5UL * 60 * 1000;
constexpr uint32_t WEATHER_STALE_MS = 3UL * 60 * 60 * 1000; // if older than 3h, mark it

// Give up on a stuck connection so the retry timer keeps moving.
constexpr uint32_t WEATHER_HTTP_TIMEOUT_MS = 10000;

// Fetch task memory. Measured ~4KB, allocated 12KB.
constexpr uint32_t WEATHER_TASK_STACK = 12288;

// SHARED BY WEATHER + MARKETS FETCH -------------------------------------------------
// Wait after startup before the first fetch, so Wi-Fi and the clock are ready.
constexpr uint32_t FETCH_FIRST_DELAY_MS = 3000;
constexpr uint32_t FETCH_FIRST_STAGGER_MS = 1000; // markets after weather

// Don't fetch until the clock is set - but after 45s fetch anyway.
constexpr uint32_t FETCH_NTP_GRACE_MS = 45000;

// Short pause after startup so the first serial messages aren't lost.
constexpr uint32_t SERIAL_BOOT_SETTLE_MS = 300;

// MARKETS ---------------------------------------------------------------------------
// Displays SPY, part of the free API tier
// It follows the S&P 500 index at 1/10th of the price
#define MARKET_SYMBOL "SPY"
#define MARKET_LABEL "SPY"

constexpr size_t MARKET_KEY_MAX = 40;

// Same timing as weather
constexpr uint32_t MARKET_FETCH_MS = 30UL * 60 * 1000;
constexpr uint32_t MARKET_RETRY_MS = 5UL * 60 * 1000;
constexpr uint32_t MARKET_STALE_MS = 6UL * 60 * 60 * 1000; // if older than 6h, mark it

// Give up on a stuck connection so the retry timer keeps moving.
constexpr uint32_t MARKET_HTTP_TIMEOUT_MS = 10000;

// Fetch task memory. Measured ~4KB, allocated 12KB.
constexpr uint32_t MARKET_TASK_STACK = 12288;

// Fetch a bit later than weather.
constexpr uint16_t MARKET_SLOT_OFFSET_SEC = 50;

// WI-FI -----------------------------------------------------------------------------
// After a drop, wait 5s, then keep doubling up to 60s. Resets on reconnect.
constexpr uint32_t WIFI_RETRY_MIN_MS = 5000;
constexpr uint32_t WIFI_RETRY_MAX_MS = 60000;

// Still offline after this long -> restart the radio from scratch.
constexpr uint32_t WIFI_STACK_RESET_MS = 300000; // 5 minutes

// How often loop() checks the Wi-Fi state.
constexpr uint32_t NET_POLL_MS = 1000;

// BUTTON + INFO SCREEN ---------------------------------------------------------------
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint32_t BUTTON_LONG_MS = 3000; // 3s hold -> reset

// What pollButton() in main.cpp returns
enum ButtonEvent : uint8_t { BTN_NONE = 0, BTN_SHORT, BTN_LONG };

// INFO stays up until dismissed (web page or short press), and redraws once a
// minute to keep the time and uptime current. Shows on first boot only, then a
// saved flag hides it; a reset brings it back.
constexpr bool INFO_ALWAYS_ON_BOOT = false;

// INFO SCREEN LAYOUT ------------------------------------------------------------------
// IP address box at the top center
constexpr int16_t INFO_IP_BOX_X = 20;
constexpr int16_t INFO_IP_BOX_Y = 70;
constexpr int16_t INFO_IP_BOX_W = 360;
constexpr int16_t INFO_IP_BOX_H = 48;
constexpr int16_t INFO_IP_BOX_R = 20;
// IP address text
constexpr int16_t INFO_IP_Y = 100;
constexpr uint8_t INFO_IP_SIZE = 3;

// Rows of label + value below the box
constexpr uint8_t INFO_ROW_COUNT = 6;  // ssid, security, signal, timezone, time, uptime
constexpr int16_t INFO_ROW_Y0 = 150;   // first row baseline
constexpr int16_t INFO_ROW_PITCH = 22; // row spacing
constexpr int16_t INFO_LABEL_X = 30;   // label column
constexpr int16_t INFO_VALUE_X = 150;  // value column
constexpr uint8_t INFO_ROW_SIZE = 2;
constexpr int16_t INFO_VALUE_MAX_W = (SCREEN_W - 20) - INFO_VALUE_X - 10; // clip before the frame
