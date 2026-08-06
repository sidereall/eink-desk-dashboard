// weather.h - Open-Meteo over HTTPS, on its own FreeRTOS task.
//
// A TLS handshake takes seconds when it works and the full timeout when it
// doesn't, so it runs off the loop task. Otherwise every refresh would freeze
// the clock and the config page.
//
// The fetch task touches nothing the loop task owns: not the panel, not the web
// server, and not NVS. Settings are pushed in and results come back through a
// mutex-guarded copy.
#pragma once
#include "screens.h"

// Creates the mutex, reads the location, starts the task. Call in setup().
void weatherBegin();

// Re-read the location from NVS and hand it to the task. Loop task only, since it touches NVS.
void weatherApplySettings();

// Fetch as soon as possible, after a location change or Wi-Fi coming up. Held until there's a network.
void weatherRequestFetch();

// True exactly once, after fresh data arrives.
bool weatherTakeUpdate();

// Copies the current data into AppState, under the mutex.
void weatherCopyInto(AppState &s);
