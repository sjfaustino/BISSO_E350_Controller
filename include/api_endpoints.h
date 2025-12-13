/**
 * @file api_endpoints.h
 * @brief API Endpoint Registry and Discovery (PHASE 5.2)
 * @details Centralized registry of all available API endpoints for auto-discovery
 * @project BISSO E350 Controller
 */

#ifndef API_ENDPOINTS_H
#define API_ENDPOINTS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HTTP methods
 */
typedef enum {
    HTTP_GET = 0x01,
    HTTP_POST = 0x02,
    HTTP_PUT = 0x04,
    HTTP_DELETE = 0x08
} http_method_t;

/**
 * API endpoint descriptor
 */
typedef struct {
    const char* path;              // REST endpoint path (e.g., "/api/status")
    http_method_t methods;         // Bitmask of supported HTTP methods
    const char* description;       // Human-readable description
    bool requires_auth;            // True if HTTP Basic Auth required
    bool rate_limited;             // True if rate limiting applied
    const char* rate_limit_info;   // e.g., "50 requests/min"
    const char* response_type;     // e.g., "application/json"
} api_endpoint_t;

/**
 * Initialize API endpoint registry
 */
void apiEndpointsInit();

/**
 * Get all registered endpoints
 * @param count Output: number of endpoints
 * @return Array of endpoint descriptors
 */
const api_endpoint_t* apiEndpointsGetAll(int* count);

/**
 * Find endpoint by path
 * @param path Endpoint path
 * @return Descriptor or NULL if not found
 */
const api_endpoint_t* apiEndpointsFind(const char* path);

/**
 * Export endpoints as JSON for discovery
 * @param buffer Output buffer
 * @param buffer_size Maximum size
 * @return Bytes written
 */
size_t apiEndpointsExportJSON(char* buffer, size_t buffer_size);

/**
 * Print endpoint registry to serial
 */
void apiEndpointsPrint();

#ifdef __cplusplus
}
#endif

#endif // API_ENDPOINTS_H
