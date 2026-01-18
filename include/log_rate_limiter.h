#ifndef LOG_RATE_LIMITER_H
#define LOG_RATE_LIMITER_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// PHASE 2.5: LOG RATE LIMITING
// ============================================================================

// Maximum number of fault types to track
#define LOG_RATE_LIMIT_CAPACITY 20

// Default rate limit: don't log the same fault within this interval (ms)
#define LOG_RATE_LIMIT_DEFAULT_MS 5000  // 5 seconds

// Aggressive rate limit for high-frequency faults
#define LOG_RATE_LIMIT_AGGRESSIVE_MS 10000  // 10 seconds

// ============================================================================
// RATE LIMIT TRACKING
// ============================================================================

typedef struct {
  uint16_t fault_id;
  int16_t sub_id;           // -1 for any/broadcast
  uint32_t last_logged_ms;
  uint32_t suppress_count;
  uint32_t limit_interval_ms;
} log_rate_entry_t;

// ============================================================================
// RATE LIMITER API
// ============================================================================

// Initialize rate limiter
void logRateLimiterInit();

// Check if a fault should be logged (rate limited)
// Returns true if fault should be logged, false if suppressed
bool logRateLimiterCheck(uint16_t fault_id, int16_t sub_id);

// Enable or disable the rate limiter (useful for stress tests)
void logRateLimiterSetEnabled(bool enabled);

// Check if rate limiter is currently enabled
bool logRateLimiterIsEnabled();

// Update rate limit for a specific fault type (default is 5s)
// Use for high-frequency faults that need stricter limits
void logRateLimiterSetInterval(uint16_t fault_id, uint32_t interval_ms);

// Get suppression count for a fault (diagnostics)
uint32_t logRateLimiterGetSuppressed(uint16_t fault_id, int16_t sub_id);

// Reset rate limiter (testing)
void logRateLimiterReset();

// Print rate limiter statistics
void logRateLimiterShowStats();

#endif
