/**
 * @file openapi.h
 * @brief OpenAPI 3.0 Specification Generator (Phase 6)
 * @details Generates OpenAPI specification from API endpoint registry for Swagger UI integration
 * @project BISSO E350 Controller
 */

#ifndef OPENAPI_H
#define OPENAPI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generate OpenAPI 3.0 specification as JSON
 * @param buffer Output buffer for JSON spec
 * @param buffer_size Maximum buffer size
 * @return Number of bytes written to buffer
 *
 * Generates complete OpenAPI specification based on registered endpoints.
 * Includes:
 * - API info (title, version, description)
 * - Server information
 * - All endpoint paths with methods, parameters, responses
 * - Security schemes (HTTP Basic Auth)
 * - Common response schemas
 */
size_t openAPIGenerateJSON(char* buffer, size_t buffer_size);

/**
 * Get OpenAPI specification version
 * @return Version string (e.g., \"3.0.0\")
 */
const char* openAPIGetVersion(void);

/**
 * Get OpenAPI info object
 * @return Info description for specification header
 */
const char* openAPIGetInfoJson(void);

/**
 * Get security scheme definition for HTTP Basic Auth
 * @return Security scheme JSON definition
 */
const char* openAPIGetSecurityScheme(void);

/**
 * Validate OpenAPI specification syntax
 * @param spec_json Specification JSON string
 * @return true if valid, false if malformed
 */
bool openAPIValidate(const char* spec_json);

#ifdef __cplusplus
}
#endif

#endif // OPENAPI_H
