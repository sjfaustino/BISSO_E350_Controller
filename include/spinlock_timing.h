/**
 * @file spinlock_timing.h
 * @brief Spinlock Performance Monitoring (Debug Only)
 * @details Provides instrumented spinlock macros to measure critical section durations.
 *          Enabled only in DEBUG builds to avoid production overhead.
 */

#ifndef SPINLOCK_TIMING_H
#define SPINLOCK_TIMING_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Spinlock timing is only enabled in DEBUG builds
#ifdef DEBUG
  #define ENABLE_SPINLOCK_TIMING 1
#else
  #define ENABLE_SPINLOCK_TIMING 0
#endif

#if ENABLE_SPINLOCK_TIMING

// Stats structure for each instrumented location
typedef struct {
  const char* location;      // Source code location identifier
  uint32_t max_duration_us;  // Maximum duration observed (microseconds)
  uint32_t total_count;      // Number of times executed
  uint32_t over_10us_count;  // Count of executions >10μs (should use mutex)
} spinlock_timing_stats_t;

#define MAX_SPINLOCK_LOCATIONS 32

// Get or create stats entry for a location
spinlock_timing_stats_t* spinlockTimingGetStats(const char* location);

// Print all spinlock timing statistics
void spinlockTimingPrintStats();

// Reset all timing statistics
void spinlockTimingResetStats();

// Macros for timed critical sections
#define SPINLOCK_ENTER(spinlock, location) \
  uint32_t _spinlock_start_##location = micros(); \
  portENTER_CRITICAL(&spinlock);

#define SPINLOCK_EXIT(spinlock, location) \
  portEXIT_CRITICAL(&spinlock); \
  { \
    uint32_t _spinlock_duration_##location = micros() - _spinlock_start_##location; \
    spinlock_timing_stats_t* _stats_##location = spinlockTimingGetStats(#location); \
    if (_stats_##location) { \
      _stats_##location->total_count++; \
      if (_spinlock_duration_##location > _stats_##location->max_duration_us) { \
        _stats_##location->max_duration_us = _spinlock_duration_##location; \
      } \
      if (_spinlock_duration_##location > 10) { \
        _stats_##location->over_10us_count++; \
      } \
    } \
  }

#else

// Timing disabled - use standard spinlock macros (no overhead)
#define SPINLOCK_ENTER(spinlock, location) portENTER_CRITICAL(&spinlock);
#define SPINLOCK_EXIT(spinlock, location) portEXIT_CRITICAL(&spinlock);

// Stubs for non-debug builds
#define spinlockTimingPrintStats() do {} while(0)
#define spinlockTimingResetStats() do {} while(0)

#endif // ENABLE_SPINLOCK_TIMING

#endif // SPINLOCK_TIMING_H
