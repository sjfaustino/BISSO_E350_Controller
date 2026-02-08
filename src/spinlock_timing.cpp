/**
 * @file spinlock_timing.cpp
 * @brief Spinlock Performance Monitoring Implementation
 * @details Tracks critical section durations to identify candidates for mutex migration.
 */

#include "spinlock_timing.h"
#include "serial_logger.h"
#include <string.h>

#if ENABLE_SPINLOCK_TIMING

static spinlock_timing_stats_t spinlock_stats[MAX_SPINLOCK_LOCATIONS];
static uint8_t spinlock_stats_count = 0;
static SemaphoreHandle_t spinlock_stats_mutex = nullptr;

spinlock_timing_stats_t* spinlockTimingGetStats(const char* location) {
  if (!spinlock_stats_mutex) {
    spinlock_stats_mutex = xSemaphoreCreateMutex();
  }

  xSemaphoreTake(spinlock_stats_mutex, portMAX_DELAY);

  // Find existing entry
  for (uint8_t i = 0; i < spinlock_stats_count; i++) {
    if (strcmp(spinlock_stats[i].location, location) == 0) {
      xSemaphoreGive(spinlock_stats_mutex);
      return &spinlock_stats[i];
    }
  }

  // Create new entry
  if (spinlock_stats_count < MAX_SPINLOCK_LOCATIONS) {
    spinlock_timing_stats_t* entry = &spinlock_stats[spinlock_stats_count];
    entry->location = location;
    entry->max_duration_us = 0;
    entry->total_count = 0;
    entry->over_10us_count = 0;
    spinlock_stats_count++;
    xSemaphoreGive(spinlock_stats_mutex);
    return entry;
  }

  xSemaphoreGive(spinlock_stats_mutex);
  return nullptr;
}

void spinlockTimingPrintStats() {
  if (!spinlock_stats_mutex) return;
  
  xSemaphoreTake(spinlock_stats_mutex, portMAX_DELAY);
  
  logPrintln("\n[SPINLOCK] === Critical Section Timing Stats ===");
  logPrintln("Location                         Count    Max(us)  >10us");
  logPrintln("------------------------------------------------------");
  
  for (uint8_t i = 0; i < spinlock_stats_count; i++) {
    logPrintf("%-32s %7lu  %7lu  %5lu\n",
              spinlock_stats[i].location,
              (unsigned long)spinlock_stats[i].total_count,
              (unsigned long)spinlock_stats[i].max_duration_us,
              (unsigned long)spinlock_stats[i].over_10us_count);
  }
  
  xSemaphoreGive(spinlock_stats_mutex);
}

void spinlockTimingResetStats() {
  if (!spinlock_stats_mutex) return;
  
  xSemaphoreTake(spinlock_stats_mutex, portMAX_DELAY);
  
  for (uint8_t i = 0; i < spinlock_stats_count; i++) {
    spinlock_stats[i].max_duration_us = 0;
    spinlock_stats[i].total_count = 0;
    spinlock_stats[i].over_10us_count = 0;
  }
  
  logInfo("[SPINLOCK] Timing stats reset");
  xSemaphoreGive(spinlock_stats_mutex);
}

#endif // ENABLE_SPINLOCK_TIMING
