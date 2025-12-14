/**
 * @file api_endpoints.cpp
 * @brief API Endpoint Registry Implementation (PHASE 5.2)
 */

#include "api_endpoints.h"
#include "serial_logger.h"
#include <string.h>
#include <stdio.h>

// Centralized API endpoint registry (PHASE 5.2)
static const api_endpoint_t api_endpoints[] = {
    {
        .path = "/api/status",
        .methods = HTTP_GET,
        .description = "System status and axis positions",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/jog",
        .methods = HTTP_POST,
        .description = "Manual axis jogging commands",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/spindle",
        .methods = HTTP_GET,
        .description = "Spindle current monitoring and telemetry",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/metrics",
        .methods = HTTP_GET,
        .description = "Task performance metrics and CPU usage",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/telemetry",
        .methods = HTTP_GET,
        .description = "Comprehensive system telemetry and health status",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/telemetry/compact",
        .methods = HTTP_GET,
        .description = "Lightweight telemetry for high-frequency polling",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/health",
        .methods = HTTP_GET,
        .description = "System health check with component status",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/endpoints",
        .methods = HTTP_GET,
        .description = "API endpoint discovery and documentation",
        .requires_auth = false,
        .rate_limited = false,
        .rate_limit_info = "unlimited",
        .response_type = "application/json"
    },
    {
        .path = "/api/update",
        .methods = HTTP_POST,
        .description = "OTA firmware update endpoint (binary upload)",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "1 request/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/update/status",
        .methods = HTTP_GET,
        .description = "OTA firmware update progress and status",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/files",
        .methods = HTTP_GET | HTTP_DELETE,
        .description = "File management (list and delete)",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/upload",
        .methods = HTTP_POST,
        .description = "File upload endpoint",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "10 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/config/get",
        .methods = HTTP_GET,
        .description = "Get configuration for category (motion, VFD, encoder, safety, thermal)",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/config/set",
        .methods = HTTP_POST,
        .description = "Set configuration value with validation",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "30 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/config/validate",
        .methods = HTTP_POST,
        .description = "Validate configuration change (pre-flight check)",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/config/schema",
        .methods = HTTP_GET,
        .description = "Get configuration schema for client-side validation",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/encoder/calibrate",
        .methods = HTTP_POST,
        .description = "Calibrate encoder for specified axis",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "20 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/encoder/diagnostics",
        .methods = HTTP_GET,
        .description = "Get encoder diagnostics (jitter, deviation, calibration)",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/dashboard/metrics",
        .methods = HTTP_GET,
        .description = "Get dashboard metrics (optimized for web interface)",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
    {
        .path = "/api/load",
        .methods = HTTP_GET,
        .description = "Get system load information (CPU, memory, heap)",
        .requires_auth = true,
        .rate_limited = true,
        .rate_limit_info = "50 requests/min",
        .response_type = "application/json"
    },
};

static const int endpoint_count = sizeof(api_endpoints) / sizeof(api_endpoint_t);

void apiEndpointsInit() {
    logInfo("[API_ENDPOINTS] Registry initialized with %d endpoints", endpoint_count);
}

const api_endpoint_t* apiEndpointsGetAll(int* count) {
    if (count) *count = endpoint_count;
    return api_endpoints;
}

const api_endpoint_t* apiEndpointsFind(const char* path) {
    if (!path) return NULL;

    for (int i = 0; i < endpoint_count; i++) {
        if (strcmp(api_endpoints[i].path, path) == 0) {
            return &api_endpoints[i];
        }
    }
    return NULL;
}

static const char* methodToString(http_method_t method) {
    static char result[64];
    result[0] = '\0';

    if (method & HTTP_GET) strcat(result, "GET ");
    if (method & HTTP_POST) strcat(result, "POST ");
    if (method & HTTP_PUT) strcat(result, "PUT ");
    if (method & HTTP_DELETE) strcat(result, "DELETE");

    return result;
}

size_t apiEndpointsExportJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 256) return 0;

    size_t offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\"api_version\":\"1.0\",\"endpoints\":[");

    for (int i = 0; i < endpoint_count; i++) {
        const api_endpoint_t* ep = &api_endpoints[i];

        if (i > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset, ",");
        }

        offset += snprintf(buffer + offset, buffer_size - offset,
            "{\"path\":\"%s\",\"methods\":[", ep->path);

        // Add methods as array
        bool first = true;
        if (ep->methods & HTTP_GET) {
            offset += snprintf(buffer + offset, buffer_size - offset, "\"GET\"");
            first = false;
        }
        if (ep->methods & HTTP_POST) {
            offset += snprintf(buffer + offset, buffer_size - offset, "%s\"POST\"", first ? "" : ",");
            first = false;
        }
        if (ep->methods & HTTP_PUT) {
            offset += snprintf(buffer + offset, buffer_size - offset, "%s\"PUT\"", first ? "" : ",");
            first = false;
        }
        if (ep->methods & HTTP_DELETE) {
            offset += snprintf(buffer + offset, buffer_size - offset, "%s\"DELETE\"", first ? "" : ",");
        }

        offset += snprintf(buffer + offset, buffer_size - offset,
            "],\"description\":\"%s\",\"auth\":%s,\"rate_limit\":\"%s\"}",
            ep->description,
            ep->requires_auth ? "true" : "false",
            ep->rate_limit_info);

        // Prevent buffer overrun
        if (offset >= buffer_size - 100) {
            offset = buffer_size - 100;
            break;
        }
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "]}");
    return offset;
}

void apiEndpointsPrint() {
    Serial.println("\n[API ENDPOINTS] === Registered Endpoints ===");
    Serial.println("Path                        | Methods            | Auth | Rate Limit");
    Serial.println("----------------------------|-------------------|------|---------------------");

    for (int i = 0; i < endpoint_count; i++) {
        const api_endpoint_t* ep = &api_endpoints[i];

        Serial.printf("%-27s | %-18s | %-4s | %s\n",
            ep->path,
            methodToString(ep->methods),
            ep->requires_auth ? "Yes" : "No",
            ep->rate_limit_info);
    }

    Serial.printf("\nTotal endpoints: %d\n\n", endpoint_count);
}
