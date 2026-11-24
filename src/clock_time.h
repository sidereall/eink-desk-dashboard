// Provides local time, display dates, and clock-based fetch intervals.

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Loads the saved timezone from NVS and applies it. Call once, in setup().
void clockBegin();

// (Re)start the SNTP client. Call when Wi-Fi comes up.
void clockStartSync();

// Switch to a different timezone. The underlying time doesn't change, only how
// it's displayed, so no network is needed and the next redraw shows it.
void clockSetTimezone(const char *posixTz);

// True once NTP has set the clock.
bool clockIsSynced();

// Gets the current local time. Returns false if the clock isn't set yet.
bool clockGetLocal(struct tm &out);

// e.g. "Fri, 6 Mar 2026"
void clockFormatDate(const struct tm &t, char *buf, size_t n);

// e.g. "FRIDAY, 6 MAR" - for the TASKS header
void clockFormatDateLong(const struct tm &t, char *buf, size_t n);

// Align provider refreshes to local time once NTP is available.
uint32_t clockNextFetchDelay(uint32_t intervalMs, uint16_t offsetSeconds);
