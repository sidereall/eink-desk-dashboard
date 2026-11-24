// Provides access to the task list with occupied slots first.

#pragma once
#include "screens.h"
#include <stdint.h>

// Loads the saved list. Call once, in setup().
void tasksBegin();

// Returns the number of occupied slots.
uint8_t tasksCopyInto(Task (&items)[MAX_TASKS]);

// Replaces the whole list. Titles are cleaned up, blanks dropped, the rest compacted, then saved to NVS.
void tasksSetAll(const Task *items, uint8_t count);

// Wipes every task. Used by the 3 second BOOT hold.
void tasksReset();
