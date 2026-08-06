// tasks.h - the task list, in RAM and saved to NVS.
//
// Five slots. The list is kept compact: deleting slot 2 shifts 3 and 4 up and
// empties slot 5. That happens here rather than in the browser, so a
// hand-crafted POST can't leave a gap in the middle.
#pragma once
#include "screens.h"
#include <stdint.h>

// Loads the saved list. Call once, in setup().
void tasksBegin();

// How many slots actually have a title in them (0..MAX_TASKS).
uint8_t tasksCount();

// Fills s.tasks and s.taskCount from the store.
void tasksCopyInto(AppState &s);

// Replaces the whole list. Titles are cleaned up, blanks dropped, the rest compacted, then saved to NVS.
void tasksSetAll(const Task *items, uint8_t count);

// Wipes every task. Used by the 3 second BOOT hold.
void tasksReset();
