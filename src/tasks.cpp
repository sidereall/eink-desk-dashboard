// Stores the five task slots together in ESP32 nonvolatile storage.

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
}

uint8_t tasksCopyInto(Task (&items)[MAX_TASKS]) {
  uint8_t count = 0;
  for (const StoredTask &stored : s_tasks) {
    if (stored.title[0] == '\0')
      continue;
    Task &task = items[count++];
    snprintf(task.title, sizeof(task.title), "%s", stored.title);
    task.done = stored.done != 0;
  }
  for (uint8_t i = count; i < MAX_TASKS; i++)
    items[i] = {};
  return count;
}

// Printable ASCII only, trimmed. Org_01 can't draw anything above 126.
static void sanitize(const char *in, char *out, size_t n) {
  size_t j = 0;

  while (*in == ' ' || *in == '\t')
    in++;

  for (; *in && j < n - 1; in++) {
    const unsigned char c = (unsigned char)*in;
    if (c >= 32 && c < 127)
      out[j++] = (char)c;
  }
  out[j] = '\0';

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
    StoredTask &slot = s_tasks[out];
    sanitize(items[i].title, slot.title, sizeof(slot.title));
    if (slot.title[0] == '\0')
      continue;

    slot.done = items[i].done ? 1 : 0;
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
