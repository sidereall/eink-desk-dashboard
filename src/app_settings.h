// app_settings.h - the settings that survive a power cut, stored in NVS.
//
// Named app_settings and not settings, because ESP-IDF has its own settings.h
// on the include path and ours got shadowed by it.
//
// NVS survives a reflash. To wipe it, hold BOOT for 3 seconds, or run
// `pio run -t erase`.
#pragma once
#include <stddef.h>

void settingsBegin();

// Wipes every stored setting. Bound to the 3 second BOOT hold.
void settingsFactoryReset();

// INFO SCREEN
bool settingsInfoShown();
void settingsMarkInfoShown();

// CLOCK
// Defaults to UTC on a fresh device.
void settingsGetTz(char *posix, size_t nPosix, char *label, size_t nLabel);
void settingsSetTz(const char *posix, const char *label);

bool settingsShowDate();
void settingsSetShowDate(bool show);

// DISPLAY
uint8_t settingsDisplayMode(); // DISPLAY_CYCLE or DISPLAY_STATIC
void settingsSetDisplayMode(uint8_t mode);

uint8_t settingsStaticScreen(); // ScreenId
void settingsSetStaticScreen(uint8_t screen);

uint8_t settingsDwellMinutes();
void settingsSetDwellMinutes(uint8_t minutes);

// WEATHER
// The browser does the city lookup, the device just stores the result.
bool settingsWeatherConfigured();
void settingsGetWeather(float *lat, float *lon, char *name, size_t nName);
void settingsSetWeather(float lat, float lon, const char *name);

// MARKETS
// Just the API key, the symbol is fixed in config.h.
bool settingsMarketConfigured();
void settingsGetMarketKey(char *key, size_t nKey);
void settingsSetMarketKey(const char *key);
