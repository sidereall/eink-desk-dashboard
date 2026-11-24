// Connects the market data worker to the dashboard.

#pragma once
#include "screens.h"

void marketsBegin();

// The main loop reads the saved key and symbol and passes them to the worker.
void marketsApplySettings();
void marketsRequestFetch();

// Consumes the completion flag, including failed requests that may leave a stale quote.
bool marketsTakeUpdate();
void marketsCopyInto(AppState &s);
