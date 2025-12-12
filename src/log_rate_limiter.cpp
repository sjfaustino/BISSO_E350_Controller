/**
 * @file log_rate_limiter.cpp
 * @brief PHASE 2.5 Log Rate Limiting
 *
 * Prevents duplicate fault messages from flooding the log by tracking
 * recent faults and suppressing repeated instances within a configured
 * time interval. Maintains suppression counts for diagnostics.
 */

#include "log_rate_limiter.h"
#include "serial_logger.h"
#include <string.h>

// ============================================================================
// RATE LIMITER STATE
// ============================================================================

static log_rate_entry_t rate_entries[LOG_RATE_LIMIT_CAPACITY];
static uint8_t entry_count = 0;
static bool limiter_initialized = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

void logRateLimiterInit() {
  if (limiter_initialized) return;

  memset(rate_entries, 0, sizeof(rate_entries));
  entry_count = 0;
  limiter_initialized = true;

  logInfo("[RATE_LIMIT] Log rate limiter initialized");
}

// ============================================================================
// RATE LIMIT CHECKING
// ============================================================================

bool logRateLimiterCheck(uint16_t fault_id, int16_t sub_id) {
  if (!limiter_initialized) return true;  // Allow if not initialized

  uint32_t now = millis();

  // Search for existing entry for this fault
  for (uint8_t i = 0; i < entry_count; i++) {
    if (rate_entries[i].fault_id == fault_id &&
        (rate_entries[i].sub_id == -1 || rate_entries[i].sub_id == sub_id)) {
      // Found existing entry - check if enough time has passed
      uint32_t time_since_last = now - rate_entries[i].last_logged_ms;

      if (time_since_last >= rate_entries[i].limit_interval_ms) {
        // Enough time has passed - allow and update
        rate_entries[i].last_logged_ms = now;
        rate_entries[i].suppress_count = 0;
        return true;
      } else {
        // Still in suppression window - increment counter
        rate_entries[i].suppress_count++;
        return false;
      }
    }
  }

  // New fault type - create entry if space available
  if (entry_count < LOG_RATE_LIMIT_CAPACITY) {
    rate_entries[entry_count].fault_id = fault_id;
    rate_entries[entry_count].sub_id = sub_id;
    rate_entries[entry_count].last_logged_ms = now;
    rate_entries[entry_count].suppress_count = 0;
    rate_entries[entry_count].limit_interval_ms = LOG_RATE_LIMIT_DEFAULT_MS;
    entry_count++;
    return true;
  }

  // Table full - allow anyway to prevent lost faults
  return true;
}

// ============================================================================
// RATE LIMIT CONFIGURATION
// ============================================================================

void logRateLimiterSetInterval(uint16_t fault_id, uint32_t interval_ms) {
  if (!limiter_initialized) return;

  // Search for entry
  for (uint8_t i = 0; i < entry_count; i++) {
    if (rate_entries[i].fault_id == fault_id) {
      rate_entries[i].limit_interval_ms = interval_ms;
      logInfo("[RATE_LIMIT] Set interval for fault %u to %lu ms", fault_id, (unsigned long)interval_ms);
      return;
    }
  }

  // Not found - we can add a new entry if space available
  if (entry_count < LOG_RATE_LIMIT_CAPACITY) {
    rate_entries[entry_count].fault_id = fault_id;
    rate_entries[entry_count].sub_id = -1;  // Broadcast
    rate_entries[entry_count].last_logged_ms = 0;
    rate_entries[entry_count].suppress_count = 0;
    rate_entries[entry_count].limit_interval_ms = interval_ms;
    entry_count++;
    logInfo("[RATE_LIMIT] Registered fault %u with interval %lu ms", fault_id, (unsigned long)interval_ms);
  }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

uint32_t logRateLimiterGetSuppressed(uint16_t fault_id, int16_t sub_id) {
  if (!limiter_initialized) return 0;

  for (uint8_t i = 0; i < entry_count; i++) {
    if (rate_entries[i].fault_id == fault_id &&
        (rate_entries[i].sub_id == -1 || rate_entries[i].sub_id == sub_id)) {
      return rate_entries[i].suppress_count;
    }
  }

  return 0;
}

void logRateLimiterReset() {
  memset(rate_entries, 0, sizeof(rate_entries));
  entry_count = 0;
  logInfo("[RATE_LIMIT] Rate limiter reset");
}

void logRateLimiterShowStats() {
  Serial.println("\n=== LOG RATE LIMITER STATISTICS ===");
  Serial.println("Fault_ID  Sub_ID  Last(ms)  Suppressed  Interval(ms)");
  Serial.println("--------------------------------------------------------");

  uint32_t now = millis();
  for (uint8_t i = 0; i < entry_count; i++) {
    uint32_t age = now - rate_entries[i].last_logged_ms;
    Serial.printf("%-9u %-7d %-9lu %-11lu %-12lu\n",
                  (unsigned)rate_entries[i].fault_id,
                  (int)rate_entries[i].sub_id,
                  (unsigned long)age,
                  (unsigned long)rate_entries[i].suppress_count,
                  (unsigned long)rate_entries[i].limit_interval_ms);
  }
  Serial.println();
}
