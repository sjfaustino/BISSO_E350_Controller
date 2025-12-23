/**
 * @file api_rate_limiter.cpp
 * @brief HTTP API Rate Limiting Implementation (PHASE 5.1)
 */

#include "api_rate_limiter.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>

// Rate limit configuration
#define API_RATE_LIMIT_REQUESTS 50         // Max requests per window
#define API_RATE_LIMIT_WINDOW_MS 60000     // Time window: 60 seconds (50 req/min)
// PHASE 5.10: Increased from 16 to 32 (24 current endpoints + room for growth)
// Issue: 24 unique endpoints exist but only 16 slots, causing endpoints 17-24 to be blocked
#define API_RATE_LIMIT_ENDPOINTS 32        // Track up to 32 different endpoints

// Token bucket state per endpoint
typedef struct {
    uint32_t endpoint_id;
    uint32_t tokens;                       // Current token count
    uint32_t last_refill_ms;               // Last time tokens were refilled
    uint32_t blocked_count;                // Number of blocked requests (diagnostic)
} api_rate_limiter_bucket_t;

static api_rate_limiter_bucket_t buckets[API_RATE_LIMIT_ENDPOINTS];
static uint8_t active_endpoints = 0;

// Helper: Get or create bucket for endpoint
static api_rate_limiter_bucket_t* getRateLimiterBucket(uint32_t endpoint_id) {
    // Search for existing bucket
    for (uint8_t i = 0; i < active_endpoints; i++) {
        if (buckets[i].endpoint_id == endpoint_id) {
            return &buckets[i];
        }
    }

    // Create new bucket if space available
    if (active_endpoints < API_RATE_LIMIT_ENDPOINTS) {
        api_rate_limiter_bucket_t* bucket = &buckets[active_endpoints];
        bucket->endpoint_id = endpoint_id;
        bucket->tokens = API_RATE_LIMIT_REQUESTS;
        bucket->last_refill_ms = millis();
        bucket->blocked_count = 0;
        active_endpoints++;
        return bucket;
    }

    return NULL;  // No space for new endpoint
}

void apiRateLimiterInit() {
    memset(buckets, 0, sizeof(buckets));
    active_endpoints = 0;
    logInfo("[API_RATELIMIT] Initialized (50 req/min per endpoint)");
}

bool apiRateLimiterCheck(uint32_t endpoint_id, uint32_t ip_addr) {
    (void)ip_addr;  // Reserved for per-IP tracking in future

    api_rate_limiter_bucket_t* bucket = getRateLimiterBucket(endpoint_id);
    if (!bucket) {
        logWarning("[API_RATELIMIT] No space for new endpoint (max %d)", API_RATE_LIMIT_ENDPOINTS);
        return false;
    }

    uint32_t now = millis();
    uint32_t elapsed = (uint32_t)(now - bucket->last_refill_ms);

    // Refill tokens based on elapsed time
    // Rate: API_RATE_LIMIT_REQUESTS tokens per API_RATE_LIMIT_WINDOW_MS
    // Tokens per ms = API_RATE_LIMIT_REQUESTS / API_RATE_LIMIT_WINDOW_MS
    if (elapsed > 0) {
        uint32_t new_tokens = (elapsed * API_RATE_LIMIT_REQUESTS) / API_RATE_LIMIT_WINDOW_MS;
        if (new_tokens > 0) {
            bucket->tokens += new_tokens;
            if (bucket->tokens > API_RATE_LIMIT_REQUESTS) {
                bucket->tokens = API_RATE_LIMIT_REQUESTS;  // Cap at max
            }
            bucket->last_refill_ms = now;
        }
    }

    // Check if token available
    if (bucket->tokens > 0) {
        bucket->tokens--;
        return true;  // Request allowed
    }

    // Token bucket empty - request denied
    bucket->blocked_count++;
    return false;
}

uint32_t apiRateLimiterGetBlockedCount(uint32_t endpoint_id) {
    for (uint8_t i = 0; i < active_endpoints; i++) {
        if (buckets[i].endpoint_id == endpoint_id) {
            return buckets[i].blocked_count;
        }
    }
    return 0;
}

void apiRateLimiterReset() {
    for (uint8_t i = 0; i < active_endpoints; i++) {
        buckets[i].tokens = API_RATE_LIMIT_REQUESTS;
        buckets[i].last_refill_ms = millis();
        buckets[i].blocked_count = 0;
    }
    logInfo("[API_RATELIMIT] Reset all endpoints");
}

void apiRateLimiterDiagnostics() {
    Serial.println("\n[API_RATELIMIT] === Rate Limiter Diagnostics ===");
    Serial.printf("Active Endpoints: %d / %d\n", active_endpoints, API_RATE_LIMIT_ENDPOINTS);
    Serial.printf("Limit: %d req / %lu ms (%.1f req/min)\n\n",
                  API_RATE_LIMIT_REQUESTS,
                  (unsigned long)API_RATE_LIMIT_WINDOW_MS,
                  (API_RATE_LIMIT_REQUESTS * 60000.0f) / API_RATE_LIMIT_WINDOW_MS);

    Serial.println("Endpoint ID        | Tokens | Blocked");
    Serial.println("-------------------|--------|--------");

    for (uint8_t i = 0; i < active_endpoints; i++) {
        Serial.printf("0x%08lX | %6lu | %7lu\n",
                     (unsigned long)buckets[i].endpoint_id,
                     (unsigned long)buckets[i].tokens,
                     (unsigned long)buckets[i].blocked_count);
    }

    Serial.println("\nNote: Tokens are refilled at configured rate.");
    Serial.println("      Blocked count resets at rate limiter reset.\n");
}
