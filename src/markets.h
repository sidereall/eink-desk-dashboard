// markets.h - Twelve Data over HTTPS, on its own FreeRTOS task.
//
// Same shape as weather.h. The fetch task passes results back through a mutex,
// and never touches the panel, the web server, or NVS.
#pragma once
#include "screens.h"

void marketsBegin();               // create mutex, read the key, start the task
void marketsApplySettings();       // re-read the API key from NVS, loop task only
void marketsRequestFetch();        // fetch now, after a key change or Wi-Fi coming up
bool marketsTakeUpdate();          // true once, after fresh data arrives
void marketsCopyInto(AppState &s); // copy the current data into AppState
