// Exposes the settings server and its requests to the main loop.

#pragma once
#include "screens.h"

void webBegin();
void webStop();

// Call every loop(). Requests are handled on the same thread as the draws.
void webService();

// True once, after the matching button is pressed. Handlers don't draw, they
// save and raise a flag. loop() does the drawing.
bool webTakeDismissRequest();
bool webTakeSyncRequest(); // any of the Save buttons

// Supplied by main.cpp, so /api/status can report them.
uint8_t webStatusBattery();
ScreenId webStatusScreen();