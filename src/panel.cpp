// panel.cpp - drives the e-ink display: full and partial refreshes, and the GxEPD2 setup.
#define ENABLE_GxEPD2_GFX 0 // GxEPD2_BW inherits from Adafruit_GFX directly

#include <GxEPD2_BW.h>
#include <SPI.h>

#include "config.h"
#include "panel.h"

// Buffers the whole screen (400*300/8 = 15KB), so a frame draws in one pass.
static GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
    display(GxEPD2_420_GDEY042T81(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN));

static uint8_t partialsSinceFull = 0;

void panelBegin() {
  SPI.end();
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);

  display.init(115200, /*initial=*/true, /*reset_ms=*/50, /*pulldown_rst=*/false);
  display.setRotation(0);

  partialsSinceFull = 0;
}

// Draw a frame, push it, then power the panel down.
static void renderAndPush(DrawCallback draw) {
  display.firstPage();
  do {
    draw(display);
  } while (display.nextPage());
  display.hibernate();
}

void panelDrawFull(DrawCallback draw) {
  display.setFullWindow();
  renderAndPush(draw);
  partialsSinceFull = 0;
}

// The display controller works in groups of 8 horizontal pixels, so a partial
// window has to start and end on a multiple of 8.
static void alignToBytes(int16_t &x, int16_t &w) {
  int16_t right = x + w;
  x = x & ~7;               // round left edge down
  right = (right + 7) & ~7; // round right edge up
  if (x < 0)
    x = 0;
  if (right > SCREEN_W)
    right = SCREEN_W;
  w = right - x;
}

void panelDrawPartial(int16_t x, int16_t y, int16_t w, int16_t h, DrawCallback draw) {
  // Partial updates leave ghosting behind. Full refresh fixes it.
  if (partialsSinceFull >= MAX_PARTIALS_BEFORE_FULL) {
    Serial.println("[panel] FULL refresh  <- ghost cleanup (partial budget spent)");
    panelDrawFull(draw);
    return;
  }

  alignToBytes(x, w);
  Serial.printf("[panel] partial %dx%d at (%d,%d)   [%u/%u]\n", w, h, x, y, (unsigned)(partialsSinceFull + 1),
                (unsigned)MAX_PARTIALS_BEFORE_FULL);

  // Partial and full refreshes use the same drawing code: draw() always paints the whole screen.
  // GxEPD2 throws away whatever falls outside the window.
  display.setPartialWindow(x, y, w, h);
  renderAndPush(draw);
  partialsSinceFull++;
}
