/**
 * @file api_rate_limiter.h
 * @brief HTTP API Rate Limiting (PHASE 5.1: Prevent DoS attacks)
 * @project BISSO E350 Controller
 */

#ifndef API_RATE_LIMITER_H
#define API_RATE_LIMITER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize rate limiter
 */
void apiRateLimiterInit();

/**
 * Check if request should be allowed (token bucket algorithm)
 * @param endpoint_id Unique endpoint identifier (hash of path)
 * @param ip_addr IP address as 32-bit integer (for future per-IP tracking)
 * @return true if request allowed, false if rate limit exceeded
 *
 * Token bucket: Each endpoint gets refilled with tokens at configured rate.
 * Each request consumes 1 token. When bucket empty, requests denied.
 */
bool apiRateLimiterCheck(uint32_t endpoint_id, uint32_t ip_addr);

/**
 * Get rate limit stats for diagnostics
 * @param endpoint_id Endpoint to query
 * @return Number of requests blocked in current window
 */
uint32_t apiRateLimiterGetBlockedCount(uint32_t endpoint_id);

/**
 * Reset rate limiter for all endpoints
 */
void apiRateLimiterReset();

/**
 * Print rate limiter diagnostics
 */
void apiRateLimiterDiagnostics();

/**
 * Endpoint IDs (hash of common API paths)
 * Calculate: endpoint_id = hash("/api/path")
 */
#define API_ENDPOINT_STATUS 0x2A5B1234   // /api/status
#define API_ENDPOINT_JOG 0x3C7D5678       // /api/jog
#define API_ENDPOINT_SPINDLE 0x4E9FA9BC  // /api/spindle
#define API_ENDPOINT_CONFIG 0x7D2E6FA0   // /api/config/*
#define API_ENDPOINT_FILES 0x5F0B2DEF     // /api/files
#define API_ENDPOINT_UPDATE 0x6B1C4521    // /api/update (OTA)

#ifdef __cplusplus
}
#endif

#endif // API_RATE_LIMITER_H
