// tasks.cpp
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "tasks.h"

// The save format. uint8_t not bool, because NVS needs fixed-size fields.
struct StoredTask {
  char title[TASK_TITLE_MAX];
  uint8_t done;
};

static Preferences prefs;
static const char *NS = "dash_tasks";
static const char *KEY_LIST = "list";

// The live list, so reads don't touch flash.
static StoredTask s_tasks[MAX_TASKS];

static void clearCache() { memset(s_tasks, 0, sizeof(s_tasks)); }

void tasksBegin() {
  prefs.begin(NS, /*readOnly=*/false);
  clearCache();

  // All five slots load as one blob.
  const size_t got = prefs.getBytes(KEY_LIST, s_tasks, sizeof(s_tasks));

  // Wrong size means nothing saved yet, or the format changed. Start empty.
  if (got != sizeof(s_tasks)) {
    clearCache();
  }

  // Force a terminator on every title, in case the saved bytes were corrupt.
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    s_tasks[i].title[TASK_TITLE_MAX - 1] = '\0';
  }

  Serial.printf("[tasks] loaded %u\n", (unsigned)tasksCount());
}

// Counted, not stored, so it can't disagree with the list.
uint8_t tasksCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    if (s_tasks[i].title[0] != '\0')
      n++;
  }
  return n;
}

// Copies the list into AppState for the screens to draw.
void tasksCopyInto(AppState &s) {
  s.taskCount = tasksCount();
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    snprintf(s.tasks[i].title, sizeof(s.tasks[i].title), "%s", s_tasks[i].title);
    s.tasks[i].done = (s_tasks[i].done != 0);
  }
}

// Printable ASCII only, trimmed. Org_01 can't draw anything above 126.
static void sanitize(const char *in, char *out, size_t n) {
  size_t j = 0;

  // Skip leading whitespace
  while (*in == ' ' || *in == '\t')
    in++;

  // Copy what's printable, drop the rest
  for (; *in && j < n - 1; in++) {
    const unsigned char c = (unsigned char)*in;
    if (c >= 32 && c < 127)
      out[j++] = (char)c;
  }
  out[j] = '\0';

  // Back off any trailing spaces
  while (j > 0 && out[j - 1] == ' ')
    out[--j] = '\0';
}

void tasksSetAll(const Task *items, uint8_t count) {
  clearCache();

  if (count > MAX_TASKS)
    count = MAX_TASKS;

  // Copy the non-blank titles into the first free slots, so any gaps close up.
  uint8_t out = 0;
  for (uint8_t i = 0; i < count; i++) {
    char clean[TASK_TITLE_MAX];
    sanitize(items[i].title, clean, sizeof(clean));
    if (clean[0] == '\0')
      continue;

    snprintf(s_tasks[out].title, TASK_TITLE_MAX, "%s", clean);
    s_tasks[out].done = items[i].done ? 1 : 0;
    out++;
  }

  // Writes all five slots, so deleted ones get overwritten with zeros.
  prefs.putBytes(KEY_LIST, s_tasks, sizeof(s_tasks));
  Serial.printf("[tasks] saved %u\n", (unsigned)out);
}

void tasksReset() {
  clearCache();
  prefs.clear();
  Serial.println("[tasks] cleared");
}
