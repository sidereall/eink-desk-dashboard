// screens.cpp - draws each screen from an AppState.
// Never touches GxEPD2, Wi-Fi or SPI: it only writes pixels to a canvas.
#include "screens.h"
#include "icons.h"
#include <Fonts/Org_01.h>

#include <stdio.h>
#include <string.h>

// SHARED HELPERS -------------------------------------------------------------------

// Black rectangle with a white rounded one on top, leaving black corner notches.
static void drawFrame(Adafruit_GFX &g) {
  g.fillRect(0, 0, SCREEN_W, SCREEN_H, COL_BLACK);
  g.fillRoundRect(0, 0, SCREEN_W, SCREEN_H, FRAME_RADIUS, COL_WHITE);
}

static void printAt(Adafruit_GFX &g, const char *s, int16_t x, int16_t y) {
  g.setCursor(x, y);
  g.print(s);
}

// Hollow minute numbers
static void printOutlined(Adafruit_GFX &g, const char *txt, int16_t x, int16_t y) {
  g.setTextColor(COL_BLACK);
  for (int8_t dx = -CLOCK_OUTLINE_PX; dx <= CLOCK_OUTLINE_PX; dx++) {
    for (int8_t dy = -CLOCK_OUTLINE_PX; dy <= CLOCK_OUTLINE_PX; dy++) {
      if (dx == 0 && dy == 0)
        continue;
      g.setCursor(x + dx, y + dy);
      g.print(txt);
    }
  }
  g.setTextColor(COL_WHITE);
  g.setCursor(x, y);
  g.print(txt);
}

// Print text, cutting it short with "..." if it won't fit.
// Returns the width drawn, which the strikethrough needs.
static int16_t printTruncated(Adafruit_GFX &g, const char *text, int16_t x, int16_t y, int16_t maxWidth) {
  char buf[64];
  int16_t bx, by;
  uint16_t bw, bh;

  snprintf(buf, sizeof(buf), "%s", text);

  // Fits, print it as-is
  g.getTextBounds(buf, x, y, &bx, &by, &bw, &bh);
  if ((int16_t)bw <= maxWidth) {
    printAt(g, buf, x, y);
    return (int16_t)bw;
  }

  // Too wide, so shorten it until "text..." fits
  char candidate[68];
  for (int len = (int)strlen(buf) - 1; len > 0; len--) {
    memcpy(candidate, buf, (size_t)len);
    candidate[len] = '\0';
    strcat(candidate, "...");
    g.getTextBounds(candidate, x, y, &bx, &by, &bw, &bh);
    if ((int16_t)bw <= maxWidth) {
      printAt(g, candidate, x, y);
      return (int16_t)bw;
    }
  }

  printAt(g, "...", x, y);
  g.getTextBounds("...", x, y, &bx, &by, &bw, &bh);
  return (int16_t)bw;
}

// Print text centred horizontally
static void printCentered(Adafruit_GFX &g, const char *s, int16_t y) {
  int16_t bx, by;
  uint16_t bw, bh;
  g.getTextBounds(s, 0, y, &bx, &by, &bw, &bh);
  printAt(g, s, (SCREEN_W - (int16_t)bw) / 2, y);
}

// Wi-Fi icon drawn only when connected. No icon means no Wi-Fi.
// Sits outside the clock's partial band, so main.cpp repaints its area
// separately when Wi-Fi changes.
static void drawWifi(Adafruit_GFX &g, int16_t x, int16_t y, bool connected) {
  if (!connected)
    return;
  g.drawBitmap(x, y, icon_wifi, ICON_WIFI_W, ICON_WIFI_H, COL_BLACK);
}

// Title, subtitle, rule and Wi-Fi icon. Shared by TASKS / WEATHER / MARKETS.
static void drawHeader(Adafruit_GFX &g, const char *title, const char *subtitle, bool wifiConnected) {
  g.setTextColor(COL_BLACK);

  g.setTextSize(HDR_TITLE_SIZE);
  printAt(g, title, HDR_TITLE_X, HDR_TITLE_Y);

  g.setTextSize(HDR_SUB_SIZE);
  printTruncated(g, subtitle, HDR_SUB_X, HDR_SUB_Y, HDR_SUB_MAX_W);

  g.drawLine(HDR_RULE_X0, HDR_RULE_Y, HDR_RULE_X1, HDR_RULE_Y, COL_BLACK);

  drawWifi(g, HDR_WIFI_X, HDR_WIFI_Y, wifiConnected);
}

// CLOCK  ---------------------------------------------------------------------
void drawClock(Adafruit_GFX &g, const AppState &s) {
  drawFrame(g);

  g.setFont(&Org_01);
  g.setTextWrap(false);
  g.setTextColor(COL_BLACK);

  // No RTC, so a cold boot doesn't know the time until NTP is fetched.
  if (!s.timeSynced) {
    g.setTextSize(3);
    printCentered(g, "WAITING FOR NTP", CLOCK_NOSYNC_Y);
    drawWifi(g, CLOCK_WIFI_X, CLOCK_WIFI_Y, s.wifiConnected);
    return;
  }

  char hh[3], mm[3];
  snprintf(hh, sizeof(hh), "%02u", (unsigned)s.hour);
  snprintf(mm, sizeof(mm), "%02u", (unsigned)s.minute);

  g.setTextSize(CLOCK_DIGIT_SIZE);

  // Hollow minutes, solid hours
  printOutlined(g, mm, CLOCK_MIN_X, CLOCK_MIN_Y);
  g.setTextColor(COL_BLACK);
  printAt(g, hh, CLOCK_HOUR_X, CLOCK_HOUR_Y);

  // Centred, the date's length changes with the day and month
  if (s.showDate) {
    g.setTextSize(CLOCK_DATE_SIZE);
    printCentered(g, s.clockDate, CLOCK_DATE_Y);
  }

  drawWifi(g, CLOCK_WIFI_X, CLOCK_WIFI_Y, s.wifiConnected);
}

// TASKS ---------------------------------------------------------------------
void drawTasks(Adafruit_GFX &g, const AppState &s) {
  drawFrame(g);

  g.setFont(&Org_01);
  g.setTextWrap(false);
  drawHeader(g, "TASKS", s.tasksDate, s.wifiConnected);

  uint8_t done = 0;
  for (uint8_t i = 0; i < s.taskCount && i < MAX_TASKS; i++) {
    if (s.tasks[i].done)
      done++;
  }

  // Counts tasks entered, not slots: "1 of 3 done", not "1 of 5 done".
  char counter[16];
  snprintf(counter, sizeof(counter), "%u of %u done", (unsigned)done, (unsigned)s.taskCount);
  g.setTextColor(COL_BLACK);
  g.setTextSize(HDR_SUB_SIZE);
  printAt(g, counter, TASK_COUNT_X, TASK_COUNT_Y);

  // All five boxes are always drawn. Empty slots get a box and circle, no text.
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    const int16_t top = TASK_BOX_Y0 + i * TASK_ROW_PITCH;

    g.drawRoundRect(TASK_BOX_X, top, TASK_BOX_W, TASK_BOX_H, TASK_BOX_RADIUS, COL_BLACK);

    const bool filled = (i < s.taskCount) && s.tasks[i].done;
    const int16_t cy = top + TASK_CHECK_DY;

    if (filled)
      g.fillCircle(TASK_CHECK_CX, cy, TASK_CHECK_R, COL_BLACK);
    else
      g.drawCircle(TASK_CHECK_CX, cy, TASK_CHECK_R, COL_BLACK);

    if (i >= s.taskCount || s.tasks[i].title[0] == '\0')
      continue;

    g.setTextSize(TASK_TEXT_SIZE);
    g.setTextColor(COL_BLACK);

    const int16_t baseline = top + TASK_TEXT_DY;
    const int16_t drawnW = printTruncated(g, s.tasks[i].title, TASK_TEXT_X, baseline, TASK_TEXT_MAX_W);

    // Strike only as far as the text actually reaches
    if (s.tasks[i].done) {
      const int16_t strikeY = baseline - TASK_STRIKE_OFFSET;
      g.fillRect(TASK_TEXT_X, strikeY, drawnW, TASK_STRIKE_THICK, COL_BLACK);
    }
  }
}

// WEATHER ---------------------------------------------------------------------
// 48x48 weather icons for the forecast cards
static const unsigned char *smallIcon(WxIcon w) {
  switch (w) {
  case WX_CLEAR:
    return icon_clear_48;
  case WX_PARTLY_CLOUDY:
    return icon_partly_cloudy_48;
  case WX_OVERCAST:
    return icon_overcast_48;
  case WX_RAIN:
    return icon_rain_48;
  case WX_THUNDERSTORM:
    return icon_thunderstorm_48;
  case WX_SNOW:
    return icon_snow_48;
  default:
    return icon_overcast_48;
  }
}

// 96x96 weather icons for current conditions
static const unsigned char *bigIcon(WxIcon w) {
  switch (w) {
  case WX_CLEAR:
    return icon_clear_96;
  case WX_PARTLY_CLOUDY:
    return icon_partly_cloudy_96;
  case WX_OVERCAST:
    return icon_overcast_96;
  case WX_RAIN:
    return icon_rain_96;
  case WX_THUNDERSTORM:
    return icon_thunderstorm_96;
  case WX_SNOW:
    return icon_snow_96;
  default:
    return icon_overcast_96;
  }
}

void drawWeather(Adafruit_GFX &g, const AppState &s) {
  const WeatherData &w = s.weather;

  drawFrame(g);
  g.setFont(&Org_01);
  g.setTextWrap(false);

  const char *sub = w.configured ? w.location : "NO LOCATION SET";
  drawHeader(g, "WEATHER", sub, s.wifiConnected);
  g.setTextColor(COL_BLACK);

  // Nothing to draw yet
  if (!w.configured) {
    g.setTextSize(3);
    printCentered(g, "SET A LOCATION", MSG_LINE1_Y);
    g.setTextSize(2);
    printCentered(g, "ON THE CONFIG PAGE", MSG_LINE2_Y);
    return;
  }
  if (!w.valid) {
    g.setTextSize(3);
    printCentered(g, "FETCHING...", MSG_SINGLE_Y);
    return;
  }

  // A failed fetch keeps the old data on screen and lets this timestamp age.
  // Past WEATHER_STALE_MS it says STALE, so old data can't look current.
  char updated[20];
  snprintf(updated, sizeof(updated), "%s %s", w.stale ? "STALE" : "UPDATED", w.updated);
  g.setTextSize(HDR_SUB_SIZE);
  printAt(g, updated, WX_UPDATED_X, WX_UPDATED_Y);

  // Current conditions
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", (int)w.tempNow);
  g.setTextSize(WX_TEMP_SIZE);
  printAt(g, buf, WX_TEMP_X, WX_TEMP_Y);

  // Degree celsius symbol
  g.drawBitmap(WX_DEG_LARGE_X, WX_DEG_LARGE_Y, icon_degree_large, ICON_DEG_LARGE_W, ICON_DEG_LARGE_H, COL_BLACK);

  g.setTextSize(HDR_SUB_SIZE);
  printAt(g, w.status, WX_STATUS_X, WX_STATUS_Y);

  snprintf(buf, sizeof(buf), "H: %d", (int)w.tempHigh);
  printAt(g, buf, WX_HIGH_X, WX_HL_Y);
  g.drawBitmap(WX_HIGH_DEG_X, WX_HL_DEG_Y, icon_degree_small, ICON_DEG_SMALL_W, ICON_DEG_SMALL_H, COL_BLACK);

  snprintf(buf, sizeof(buf), "L: %d", (int)w.tempLow);
  printAt(g, buf, WX_LOW_X, WX_HL_Y);
  g.drawBitmap(WX_LOW_DEG_X, WX_HL_DEG_Y, icon_degree_small, ICON_DEG_SMALL_W, ICON_DEG_SMALL_H, COL_BLACK);

  g.drawBitmap(WX_BIG_ICON_X, WX_BIG_ICON_Y, bigIcon((WxIcon)w.iconNow), ICON_BIG_W, ICON_BIG_H, COL_BLACK);

  // Four forecast cards, same offsets each, only the x moves
  for (uint8_t i = 0; i < WX_CARD_COUNT; i++) {
    const int16_t cx = WX_CARD_X0 + i * WX_CARD_PITCH;
    const int16_t cy = WX_CARD_Y;

    g.drawRoundRect(cx, cy, WX_CARD_W, WX_CARD_H, WX_CARD_R, COL_BLACK);

    g.setTextSize(3);
    printAt(g, w.days[i].name, cx + WX_CARD_DAY_DX, cy + WX_CARD_DAY_DY);

    g.drawBitmap(cx + WX_CARD_ICON_DX, cy + WX_CARD_ICON_DY, smallIcon((WxIcon)w.days[i].icon), ICON_SMALL_W,
                 ICON_SMALL_H, COL_BLACK);

    g.setTextSize(2);
    snprintf(buf, sizeof(buf), "%d", (int)w.days[i].temp);
    printAt(g, buf, cx + WX_CARD_TEMP_DX, cy + WX_CARD_TEMP_DY);
    g.drawBitmap(cx + WX_CARD_DEG_DX, cy + WX_CARD_DEG_DY, icon_degree_small, ICON_DEG_SMALL_W, ICON_DEG_SMALL_H,
                 COL_BLACK);
  }
}

// MARKETS -------------------------------------------------------------------
void drawMarkets(Adafruit_GFX &g, const AppState &s) {
  const MarketData &m = s.markets;

  drawFrame(g);
  g.setFont(&Org_01);
  g.setTextWrap(false);

  // Subtitle shows the market state
  const char *state = !m.configured  ? "NO API KEY"
                      : !m.valid     ? "CONNECTING"
                      : m.marketOpen ? "NYSE OPEN"
                                     : "NYSE CLOSED";
  drawHeader(g, "MARKETS", state, s.wifiConnected);
  g.setTextColor(COL_BLACK);

  // Nothing to draw yet
  if (!m.configured) {
    g.setTextSize(3);
    printCentered(g, "SET API KEY", MSG_LINE1_Y);
    g.setTextSize(2);
    printCentered(g, "ON THE CONFIG PAGE", MSG_LINE2_Y);
    return;
  }
  if (!m.valid) {
    g.setTextSize(3);
    printCentered(g, "FETCHING...", MSG_SINGLE_Y);
    return;
  }

  // Shows STALE instead of UPDATED once the data is older than MARKET_STALE_MS
  char updated[20];
  snprintf(updated, sizeof(updated), "%s %s", m.stale ? "STALE" : "UPDATED", m.updated);
  g.setTextSize(HDR_SUB_SIZE);
  printAt(g, updated, MK_UPDATED_X, MK_UPDATED_Y);

  // Quote box: name, price, change
  g.drawRoundRect(MK_BOX_X, MK_BOX_Y, MK_BOX_W, MK_BOX_H, MK_BOX_R, COL_BLACK);

  g.setTextSize(MK_NAME_SIZE);
  printAt(g, m.name, MK_NAME_X, MK_NAME_Y);
  printAt(g, m.price, MK_PRICE_X, MK_PRICE_Y);

  if (m.rising) {
    // Market up since last close: hollow triangle up
    g.drawTriangle(MK_TRI_CX, MK_TRI_TOP_Y, MK_TRI_CX + MK_TRI_HALF_W, MK_TRI_BOT_Y, MK_TRI_CX - MK_TRI_HALF_W,
                   MK_TRI_BOT_Y, COL_BLACK);
  } else {
    // Market down since last close: filled triangle down
    g.fillTriangle(MK_TRI_CX, MK_TRI_BOT_Y, MK_TRI_CX + MK_TRI_HALF_W, MK_TRI_TOP_Y, MK_TRI_CX - MK_TRI_HALF_W,
                   MK_TRI_TOP_Y, COL_BLACK);
  }

  g.setTextSize(HDR_SUB_SIZE);
  printAt(g, m.changePct, MK_PCT_X, MK_PCT_Y);

  // Chart box
  g.drawRoundRect(MK_CHART_X, MK_CHART_Y, MK_CHART_W, MK_CHART_H, MK_CHART_R, COL_BLACK);

  // Need at least two points to draw a line, and a price range to spread it over.
  const float range = m.sparkMax - m.sparkMin;
  if (m.sparkCount < 2 || range <= 0.0f) {
    g.setTextSize(2);
    printCentered(g, m.marketOpen ? "NO DATA YET" : "NYSE CLOSED", MK_MSG_Y);
    return;
  }

  const int16_t x0 = MK_CHART_X + MK_SPARK_PAD_X;
  const int16_t x1 = MK_CHART_X + MK_CHART_W - MK_SPARK_PAD_X;
  const int16_t yTop = MK_CHART_Y + MK_SPARK_PAD_TOP;
  const int16_t yBot = MK_CHART_Y + MK_CHART_H - MK_SPARK_PAD_BOT;
  const int16_t plotW = x1 - x0;
  const int16_t plotH = yBot - yTop;

  // Scale the prices to fit the chart box.
  // px spreads points across the width, py maps each price between the day's min and max.
  auto px = [&](uint8_t i) -> int16_t { return x0 + (int16_t)((int32_t)i * plotW / (m.sparkCount - 1)); };
  auto py = [&](float v) -> int16_t { return yBot - (int16_t)((v - m.sparkMin) / range * plotH); };

  int16_t prevX = px(0), prevY = py(m.spark[0]);
  for (uint8_t i = 1; i < m.sparkCount; i++) {
    const int16_t cx = px(i), cy = py(m.spark[i]);
    g.drawLine(prevX, prevY, cx, cy, COL_BLACK);
    prevX = cx;
    prevY = cy;
  }

  // Dot on the newest point
  g.fillCircle(prevX, prevY, 2, COL_BLACK);
}

// INFO ----------------------------------------------------------------------------
void drawInfo(Adafruit_GFX &g, const AppState &s) {
  drawFrame(g);
  g.setFont(&Org_01);
  g.setTextWrap(false);

  drawHeader(g, "INFO", s.wifiConnected ? "CONNECTED" : "CONNECTING...", s.wifiConnected);

  g.setTextColor(COL_BLACK);

  // Device IP box
  g.drawRoundRect(INFO_IP_BOX_X, INFO_IP_BOX_Y, INFO_IP_BOX_W, INFO_IP_BOX_H, INFO_IP_BOX_R, COL_BLACK);

  const char *ipText = s.wifiConnected ? s.ip : "NO CONNECTION";
  g.setTextSize(INFO_IP_SIZE);

  // Device IP centred inside the IP box
  int16_t bx, by;
  uint16_t bw, bh;
  g.getTextBounds(ipText, 0, INFO_IP_Y, &bx, &by, &bw, &bh);
  const int16_t ipX = INFO_IP_BOX_X + (INFO_IP_BOX_W - (int16_t)bw) / 2;
  printAt(g, ipText, ipX, INFO_IP_Y);

  // Diagnostic rows
  char uptime[16];
  snprintf(uptime, sizeof(uptime), "%luH %02luM", (unsigned long)(s.uptimeSec / 3600UL),
           (unsigned long)((s.uptimeSec % 3600UL) / 60UL));

  char signal[12];
  if (s.wifiConnected)
    snprintf(signal, sizeof(signal), "%d DBM", (int)s.rssi);
  else
    snprintf(signal, sizeof(signal), "-");

  char clockNow[16];
  if (s.timeSynced)
    snprintf(clockNow, sizeof(clockNow), "%02u:%02u", (unsigned)s.hour, (unsigned)s.minute);
  else
    snprintf(clockNow, sizeof(clockNow), "NO NTP YET");

  // Labels and values kept in matching order, drawn as two columns
  const char *labels[INFO_ROW_COUNT] = {"SSID", "SECURITY", "SIGNAL", "TIMEZONE", "TIME", "UPTIME"};
  const char *values[INFO_ROW_COUNT] = {(s.ssid[0] != '\0') ? s.ssid : "-",       s.security, signal,
                                        (s.tzLabel[0] != '\0') ? s.tzLabel : "-", clockNow,   uptime};

  g.setTextSize(INFO_ROW_SIZE);
  for (uint8_t i = 0; i < INFO_ROW_COUNT; i++) {
    const int16_t y = INFO_ROW_Y0 + i * INFO_ROW_PITCH;
    printAt(g, labels[i], INFO_LABEL_X, y);
    printTruncated(g, values[i], INFO_VALUE_X, y, INFO_VALUE_MAX_W);
  }
}

// Screen names to choose from on the web page
const char *screenName(ScreenId id) {
  switch (id) {
  case SCREEN_CLOCK:
    return "Clock";
  case SCREEN_TASKS:
    return "Tasks";
  case SCREEN_WEATHER:
    return "Weather";
  case SCREEN_MARKETS:
    return "Markets";
  case SCREEN_INFO:
    return "Info";
  default:
    return "?";
  }
}

// The area to repaint when Wi-Fi connects or drops
Rect wifiDirtyRect(ScreenId id) {
  // INFO is all network state, so repaint the lot
  if (id == SCREEN_INFO) {
    return Rect{0, 0, SCREEN_W, SCREEN_H};
  }

  // Clock, tasks, weather, markets: the Wi-Fi icon's area, plus a small margin
  const int16_t x = (id == SCREEN_CLOCK) ? CLOCK_WIFI_X : HDR_WIFI_X;
  const int16_t y = (id == SCREEN_CLOCK) ? CLOCK_WIFI_Y : HDR_WIFI_Y;
  return Rect{(int16_t)(x - 2), (int16_t)(y - 2), (int16_t)(ICON_WIFI_W + 4), (int16_t)(ICON_WIFI_H + 4)};
}

// Dispatch to the right renderer
void drawScreen(Adafruit_GFX &g, ScreenId id, const AppState &s) {
  switch (id) {
  case SCREEN_CLOCK:
    drawClock(g, s);
    break;
  case SCREEN_TASKS:
    drawTasks(g, s);
    break;
  case SCREEN_WEATHER:
    drawWeather(g, s);
    break;
  case SCREEN_MARKETS:
    drawMarkets(g, s);
    break;
  case SCREEN_INFO:
    drawInfo(g, s);
    break;
  default:
    drawClock(g, s);
    break;
  }
}
