/**
 * @file config_unified.h
 * @brief Unified Configuration Manager API (Gemini v3.5.22)
 * @details Increased key limit to support WCS offsets.
 */

#ifndef CONFIG_UNIFIED_H
#define CONFIG_UNIFIED_H

#include <stdint.h>
#include <stddef.h>

// Configuration Constants
// Increased to 128 to hold G54-G59 offsets (24 keys) + defaults
#define CONFIG_MAX_KEYS 128
#define CONFIG_KEY_LEN 32
#define CONFIG_VALUE_LEN 64

// Storage type enum (internal use in config_unified.cpp)
// Different from config_validator_schema.h's config_type_t (used for validation)
typedef enum {
    CONFIG_INT32,
    CONFIG_FLOAT,
    CONFIG_STRING
} config_storage_type_t;

typedef struct {
    char key[CONFIG_KEY_LEN];
    config_storage_type_t type;  // Storage type (int/float/string)
    union {
        int32_t int_val;
        float float_val;
        char str_val[CONFIG_VALUE_LEN];
    } value;
    bool is_set;
} config_entry_t;

void configUnifiedInit();
void configUnifiedCleanup();  // PHASE 5.10: Resource cleanup
void configSetDefaults();

int32_t configGetInt(const char* key, int32_t default_val);
float configGetFloat(const char* key, float default_val);
const char* configGetString(const char* key, const char* default_val);

void configSetInt(const char* key, int32_t value);
void configSetFloat(const char* key, float value);
void configSetString(const char* key, const char* value);

void configUnifiedFlush();
void configUnifiedSave();
void configUnifiedReset();
void configUnifiedClear();
int configGetKeyCount();

void configUnifiedDiagnostics();
void configUnifiedPrintAll();

// PHASE 5.1: Validated configuration getters with bounds checking
/**
 * Get integer with validation and bounds checking
 * @param key Configuration key
 * @param default_val Default if not found
 * @param min_val Minimum allowed value (or -1 to skip)
 * @param max_val Maximum allowed value (or -1 to skip)
 * @return Validated integer value
 */
int32_t configGetIntValidated(const char* key, int32_t default_val, int32_t min_val, int32_t max_val);

/**
 * Get float with validation and bounds checking
 * @param key Configuration key
 * @param default_val Default if not found
 * @param min_val Minimum allowed value (or negative to skip)
 * @param max_val Maximum allowed value (or negative to skip)
 * @return Validated float value
 */
float configGetFloatValidated(const char* key, float default_val, float min_val, float max_val);

// NVS Space Management
void configLogNvsStats();
bool configEraseNvs();

#endif