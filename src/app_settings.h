// Exposes the settings that persist across restarts and firmware updates.

#pragma once
#include <stddef.h>
#include <stdint.h>

void settingsBegin();

// Wipes every stored setting. Bound to the 3 second BOOT hold.
void settingsFactoryReset();

// Info screen
bool settingsInfoShown();
void settingsMarkInfoShown();

// Clock
// Defaults to UTC on a fresh device.
void settingsGetTz(char *posix, size_t nPosix, char *label, size_t nLabel);
void settingsSetTz(const char *posix, const char *label);

bool settingsShowDate();
void settingsSetShowDate(bool show);

// Display
uint8_t settingsDisplayMode(); // DISPLAY_CYCLE or DISPLAY_STATIC
void settingsSetDisplayMode(uint8_t mode);

uint8_t settingsStaticScreen(); // ScreenId
void settingsSetStaticScreen(uint8_t screen);

uint8_t settingsDwellMinutes();
void settingsSetDwellMinutes(uint8_t minutes);

bool settingsDarkMode();
void settingsSetDarkMode(bool dark);

// Weather
// The browser does the city lookup, the device just stores the result.
void settingsGetWeather(float *lat, float *lon, char *name, size_t nName);
void settingsSetWeather(float lat, float lon, const char *name);

bool settingsFahrenheit();
void settingsSetFahrenheit(bool f);

// Markets
bool settingsMarketConfigured();
void settingsGetMarketKey(char *key, size_t nKey);
void settingsSetMarketKey(const char *key);
void settingsGetMarketSymbol(char *symbol, size_t nSymbol);
void settingsSetMarketSymbol(const char *symbol);
