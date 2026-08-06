// panel.h - talks to the e-ink display.
//
// The only file that includes GxEPD2.
// Everything else draws into a plain Adafruit_GFX canvas and never sees the panel.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>

// Draws one complete screen into whatever canvas it's handed.
typedef void (*DrawCallback)(Adafruit_GFX &g);

// SPI and display init. Call once from setup().
void panelBegin();

// Redraw the whole panel.  Used on boot and on a screen change.
void panelDrawFull(DrawCallback draw);

// Redraw only the given rectangle. Fast and silent, but ghosts, so it promotes
// itself to a full refresh every MAX_PARTIALS_BEFORE_FULL calls.
void panelDrawPartial(int16_t x, int16_t y, int16_t w, int16_t h, DrawCallback draw);
