/**
 * @file openapi.cpp
 * @brief OpenAPI 3.0 Specification Generator Implementation (Phase 6)
 *
 * ARCHITECTURE NOTE (Gemini Audit):
 * This file generates OpenAPI spec at runtime (~10KB flash, 5-10ms per request).
 *
 * OPTIMIZATION OPPORTUNITY:
 * - Static .json.gz file in SPIFFS would save ~5KB flash
 * - Trade-off: Requires build-time generation, breaks auto-sync with endpoints
 * - Current approach: Acceptable for production (simple, auto-syncs)
 * - Future optimization: Available if flash space becomes constrained
 *
 * See: docs/GEMINI_FINAL_AUDIT.md for full analysis and optimization path
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#include "openapi.h"
#include "api_endpoints.h"
#include "serial_logger.h"
#include <string.h>
#include <stdio.h>

#define OPENAPI_VERSION "3.0.0"
#define CONTROLLER_API_VERSION "1.0"

/**
 * Helper to safely append string to buffer
 */
static size_t appendToBuffer(char* buffer, size_t offset, size_t buffer_size, const char* text) {
    if (!buffer || !text) return offset;

    size_t available = (offset < buffer_size) ? (buffer_size - offset) : 0;
    size_t len = strlen(text);

    if (len > available) len = available;
    if (len > 0) {
        memcpy(buffer + offset, text, len);
    }

    return offset + len;
}

/**
 * Escape JSON string - handle quotes and special chars
 */
static size_t escapeJsonString(char* buffer, size_t buffer_size, const char* str) {
    if (!str) return 0;

    size_t offset = 0;
    size_t src_len = strlen(str);

    for (size_t i = 0; i < src_len && offset < buffer_size - 1; i++) {
        char c = str[i];

        switch (c) {
            case '\"':
                if (offset + 1 < buffer_size) {
                    buffer[offset++] = '\\';
                    buffer[offset++] = '\"';
                }
                break;
            case '\\':
                if (offset + 1 < buffer_size) {
                    buffer[offset++] = '\\';
                    buffer[offset++] = '\\';
                }
                break;
            case '\n':
                if (offset + 1 < buffer_size) {
                    buffer[offset++] = '\\';
                    buffer[offset++] = 'n';
                }
                break;
            case '\r':
                if (offset + 1 < buffer_size) {
                    buffer[offset++] = '\\';
                    buffer[offset++] = 'r';
                }
                break;
            case '\t':
                if (offset + 1 < buffer_size) {
                    buffer[offset++] = '\\';
                    buffer[offset++] = 't';
                }
                break;
            default:
                buffer[offset++] = c;
                break;
        }
    }

    return offset;
}

/**
 * Convert HTTP methods to OpenAPI format
 */
static void appendMethodsJson(char* buffer, size_t* offset, size_t buffer_size, http_method_t methods) {
    bool first = true;

    if (methods & HTTP_GET) {
        *offset += snprintf(buffer + *offset, buffer_size - *offset, "\"get\"");
        first = false;
    }
    if (methods & HTTP_POST) {
        if (!first) *offset += snprintf(buffer + *offset, buffer_size - *offset, ",");
        *offset += snprintf(buffer + *offset, buffer_size - *offset, "\"post\"");
        first = false;
    }
    if (methods & HTTP_PUT) {
        if (!first) *offset += snprintf(buffer + *offset, buffer_size - *offset, ",");
        *offset += snprintf(buffer + *offset, buffer_size - *offset, "\"put\"");
        first = false;
    }
    if (methods & HTTP_DELETE) {
        if (!first) *offset += snprintf(buffer + *offset, buffer_size - *offset, ",");
        *offset += snprintf(buffer + *offset, buffer_size - *offset, "\"delete\"");
    }
}

/**
 * Generate method details for OpenAPI (single method)
 */
static void appendMethodDetail(char* buffer, size_t* offset, size_t buffer_size,
                               const api_endpoint_t* ep, const char* method) {
    *offset += snprintf(buffer + *offset, buffer_size - *offset,
        "\"%s\":{\"summary\":\"%s\",\"description\":\"%s\",",
        method, ep->description, ep->description);

    // Tags
    const char* tag = "API";
    if (strstr(ep->path, "/config")) tag = "Configuration";
    else if (strstr(ep->path, "/encoder")) tag = "Encoder";
    else if (strstr(ep->path, "/telemetry")) tag = "Telemetry";
    else if (strstr(ep->path, "/status")) tag = "Status";
    else if (strstr(ep->path, "/update")) tag = "Firmware";
    else if (strstr(ep->path, "/files")) tag = "Files";
    else if (strstr(ep->path, "/health")) tag = "Health";
    else if (strstr(ep->path, "/metrics")) tag = "Metrics";
    else if (strstr(ep->path, "/jog")) tag = "Motion";

    *offset += snprintf(buffer + *offset, buffer_size - *offset, "\"tags\":[\"%s\"],", tag);

    // Security
    if (ep->requires_auth) {
        *offset += snprintf(buffer + *offset, buffer_size - *offset,
            "\"security\":[{\"basicAuth\":[]}],");
    }

    // Parameters
    if (strcmp(method, "get") == 0 && strstr(ep->path, "?") == NULL) {
        *offset += snprintf(buffer + *offset, buffer_size - *offset, "\"parameters\":[");

        if (strstr(ep->path, "/config/get") != NULL) {
            *offset += snprintf(buffer + *offset, buffer_size - *offset,
                "{\"name\":\"category\",\"in\":\"query\",\"required\":true,\"description\":\"Config category (0=motion, 1=vfd, 2=encoder, 3=safety, 4=thermal)\",\"schema\":{\"type\":\"integer\"}}");
        } else if (strstr(ep->path, "/config/schema") != NULL) {
            *offset += snprintf(buffer + *offset, buffer_size - *offset,
                "{\"name\":\"category\",\"in\":\"query\",\"required\":true,\"description\":\"Config category\",\"schema\":{\"type\":\"integer\"}}");
        }

        *offset += snprintf(buffer + *offset, buffer_size - *offset, "],");
    }

    // Request body for POST/PUT
    if (strcmp(method, "post") == 0 || strcmp(method, "put") == 0) {
        *offset += snprintf(buffer + *offset, buffer_size - *offset,
            "\"requestBody\":{\"required\":true,\"content\":{\"application/json\":{\"schema\":{\"type\":\"object\"}}}},");
    }

    // Response
    *offset += snprintf(buffer + *offset, buffer_size - *offset,
        "\"responses\":{\"200\":{\"description\":\"Success\",\"content\":{\"application/json\":{\"schema\":{\"type\":\"object\"}}}},\"401\":{\"description\":\"Unauthorized\"},\"429\":{\"description\":\"Rate limit exceeded\"},\"400\":{\"description\":\"Bad request\"}}}");

    *offset += snprintf(buffer + *offset, buffer_size - *offset, "}");
}

const char* openAPIGetVersion(void) {
    return OPENAPI_VERSION;
}

const char* openAPIGetInfoJson(void) {
    return "{\"title\":\"BISSO E350 Controller API\",\"version\":\"1.0\",\"description\":\"CNC Controller REST API with endpoint discovery and configuration management\",\"contact\":{\"name\":\"BISSO E350\"},\"license\":{\"name\":\"Proprietary\"}}";
}

const char* openAPIGetSecurityScheme(void) {
    return "{\"basicAuth\":{\"type\":\"http\",\"scheme\":\"basic\",\"description\":\"HTTP Basic Authentication\"}}";
}

size_t openAPIGenerateJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 512) return 0;

    size_t offset = 0;

    // Header
    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\"openapi\":\"%s\",\"info\":%s,",
        OPENAPI_VERSION, openAPIGetInfoJson());

    // Servers
    offset += snprintf(buffer + offset, buffer_size - offset,
        "\"servers\":[{\"url\":\"http://localhost\",\"description\":\"Local device\"},{\"url\":\"https://device.local\",\"description\":\"HTTPS endpoint\"}],");

    // Paths
    offset += snprintf(buffer + offset, buffer_size - offset, "\"paths\":{");

    int endpoint_count = 0;
    const api_endpoint_t* endpoints = apiEndpointsGetAll(&endpoint_count);

    for (int i = 0; i < endpoint_count; i++) {
        const api_endpoint_t* ep = &endpoints[i];

        if (i > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset, ",");
        }

        offset += snprintf(buffer + offset, buffer_size - offset, "\"%s\":{", ep->path);

        bool first_method = true;

        if (ep->methods & HTTP_GET) {
            if (!first_method) offset += snprintf(buffer + offset, buffer_size - offset, ",");
            appendMethodDetail(buffer, &offset, buffer_size, ep, "get");
            first_method = false;
        }
        if (ep->methods & HTTP_POST) {
            if (!first_method) offset += snprintf(buffer + offset, buffer_size - offset, ",");
            appendMethodDetail(buffer, &offset, buffer_size, ep, "post");
            first_method = false;
        }
        if (ep->methods & HTTP_PUT) {
            if (!first_method) offset += snprintf(buffer + offset, buffer_size - offset, ",");
            appendMethodDetail(buffer, &offset, buffer_size, ep, "put");
            first_method = false;
        }
        if (ep->methods & HTTP_DELETE) {
            if (!first_method) offset += snprintf(buffer + offset, buffer_size - offset, ",");
            appendMethodDetail(buffer, &offset, buffer_size, ep, "delete");
        }

        offset += snprintf(buffer + offset, buffer_size - offset, "}");

        // Prevent buffer overrun
        if (offset >= buffer_size - 200) {
            offset = buffer_size - 100;
            break;
        }
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "}");

    // Components (security schemes)
    offset += snprintf(buffer + offset, buffer_size - offset,
        ",\"components\":{\"securitySchemes\":%s}}",
        openAPIGetSecurityScheme());

    return offset;
}

bool openAPIValidate(const char* spec_json) {
    if (!spec_json) return false;

    // Basic validation - check for required fields
    if (strstr(spec_json, "\"openapi\"") == NULL) return false;
    if (strstr(spec_json, "\"info\"") == NULL) return false;
    if (strstr(spec_json, "\"paths\"") == NULL) return false;

    return true;
}

#pragma GCC diagnostic pop
