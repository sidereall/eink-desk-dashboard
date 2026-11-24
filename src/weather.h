// Connects the weather data worker to the dashboard.

#pragma once
#include "screens.h"

void weatherBegin();

// The main loop reads the saved location and units and passes them to the worker.
void weatherApplySettings();
void weatherRequestFetch();

// Consumes the completion flag, including failed requests that may leave a stale forecast.
bool weatherTakeUpdate();
void weatherCopyInto(AppState &s);
