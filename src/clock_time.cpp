// clock_time.cpp
#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "app_settings.h"
#include "clock_time.h"
#include "config.h"

// An unset clock reads 1970. Only a time later than this counts as synced.
static const time_t SANE_EPOCH = 1700000000; // Nov 2023, any past date works

void clockSetTimezone(const char *posixTz) {
  setenv("TZ", posixTz, 1);
  tzset();
  Serial.printf("[clock] timezone = %s\n", posixTz);
}

void clockBegin() {
  char posix[TZ_POSIX_MAX];
  char label[TZ_LABEL_MAX];
  settingsGetTz(posix, sizeof(posix), label, sizeof(label));

  // Set the timezone now, so the time is right as soon as NTP arrives.
  clockSetTimezone(posix);
}

void clockStartSync() {
  char posix[TZ_POSIX_MAX];
  char label[TZ_LABEL_MAX];
  settingsGetTz(posix, sizeof(posix), label, sizeof(label));

  // Sets the timezone and starts NTP. Safe to call again on a reconnect.
  configTzTime(posix, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  Serial.println("[clock] ntp sync started");
}

bool clockIsSynced() { return time(nullptr) > SANE_EPOCH; }

bool clockGetLocal(struct tm &out) {
  const time_t now = time(nullptr);
  if (now <= SANE_EPOCH)
    return false;
  localtime_r(&now, &out);
  return true;
}

// Formatting e.g. "Fri, 6 Mar 2026"
void clockFormatDate(const struct tm &t, char *buf, size_t n) {
  char wd[8], mo[8];
  strftime(wd, sizeof(wd), "%a", &t); // Fri
  strftime(mo, sizeof(mo), "%b", &t); // Mar
  snprintf(buf, n, "%s, %d %s %d", wd, t.tm_mday, mo, t.tm_year + 1900);
}

// Formatting e.g. "FRIDAY, 6 MAR" for the TASKS header
void clockFormatDateLong(const struct tm &t, char *buf, size_t n) {
  char wd[12], mo[8];
  strftime(wd, sizeof(wd), "%A", &t); // FRIDAY
  strftime(mo, sizeof(mo), "%b", &t); // MAR

  for (char *p = wd; *p; p++)
    *p = (char)toupper((unsigned char)*p);
  for (char *p = mo; *p; p++)
    *p = (char)toupper((unsigned char)*p);

  // Abbreviated month, full month name would run into the task counter
  snprintf(buf, n, "%s, %d %s", wd, t.tm_mday, mo);
}
