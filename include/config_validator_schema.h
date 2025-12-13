/**
 * @file config_validator_schema.h
 * @brief Centralized Configuration Schema and Validation (PHASE 5.2)
 * @details Single source of truth for all configuration parameters
 * @project BISSO E350 Controller
 */

#ifndef CONFIG_VALIDATOR_SCHEMA_H
#define CONFIG_VALIDATOR_SCHEMA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration parameter type
 */
typedef enum {
    CONFIG_TYPE_INT,
    CONFIG_TYPE_FLOAT,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_BOOL
} config_type_t;

/**
 * Configuration parameter descriptor
 * Defines valid ranges, types, and help text for each config key
 */
typedef struct {
    const char* key;           // NVS key name
    config_type_t type;        // Data type
    int32_t int_min;           // For INT types
    int32_t int_max;
    float float_min;           // For FLOAT types
    float float_max;
    size_t string_max_len;     // For STRING types
    const char* default_value; // Default as string
    const char* description;   // Human-readable help text
    const char* unit;          // Unit of measurement (e.g., "mm", "A", "ms")
    bool critical;             // If true, invalid values trigger fault
} config_descriptor_t;

/**
 * Validation result
 */
typedef struct {
    bool is_valid;
    const char* error_message;
    bool was_clamped;      // True if value was adjusted to fit range
    const char* clamped_to;
} config_validation_result_t;

/**
 * Initialize schema (call once at startup)
 */
void configSchemaInit();

/**
 * Validate a single configuration value
 * @param key Configuration key
 * @param value Value to validate
 * @return Validation result with error details
 */
config_validation_result_t configValidateValue(const char* key, const char* value);

/**
 * Get configuration descriptor
 * @param key Configuration key
 * @return Descriptor, or NULL if key not found
 */
const config_descriptor_t* configGetDescriptor(const char* key);

/**
 * Get all descriptors
 * @param count Output: number of descriptors
 * @return Array of descriptors
 */
const config_descriptor_t* configGetAllDescriptors(int* count);

/**
 * Export schema as JSON for web UI help
 * @param buffer Output buffer
 * @param buffer_size Maximum size
 * @return Bytes written
 */
size_t configExportSchemaJSON(char* buffer, size_t buffer_size);

/**
 * Print schema to serial (for debugging/documentation)
 */
void configPrintSchema();

/**
 * Validate entire configuration in NVS
 * @return true if all values are within acceptable ranges
 */
bool configValidateAll();

#ifdef __cplusplus
}
#endif

#endif // CONFIG_VALIDATOR_SCHEMA_H
