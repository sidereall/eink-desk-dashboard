// web_ui.h - the config page served on the LAN.
#pragma once

void webBegin();
void webStop();

// Call every loop(). Requests are handled on the same thread as the draws.
void webService();

bool webIsRunning();

// True once, after the matching button is pressed. Handlers don't draw, they
// save and raise a flag. loop() does the drawing.
bool webTakeDismissRequest();
bool webTakeSyncRequest(); // any of the Save buttons
