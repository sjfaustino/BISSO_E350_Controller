/**
 * @file config_unified.h
 * @brief Unified Configuration Manager API (Gemini v3.5.20)
 * @details Central interface for NVS storage with caching and validation.
 */

#ifndef CONFIG_UNIFIED_H
#define CONFIG_UNIFIED_H

#include <stdint.h>
#include <stddef.h>

// Configuration Constants
#define CONFIG_MAX_KEYS 64
#define CONFIG_KEY_LEN 32
#define CONFIG_VALUE_LEN 64

// Data Types
typedef enum {
    CONFIG_INT32,
    CONFIG_FLOAT,
    CONFIG_STRING
} config_type_t;

typedef struct {
    char key[CONFIG_KEY_LEN];
    config_type_t type;
    union {
        int32_t int_val;
        float float_val;
        char str_val[CONFIG_VALUE_LEN];
    } value;
    bool is_set;
} config_entry_t;

// --- INITIALIZATION ---
void configUnifiedInit();
void configSetDefaults();

// --- GETTERS ---
// These return the cached value (fast) or default if not set
int32_t configGetInt(const char* key, int32_t default_val);
float configGetFloat(const char* key, float default_val);
const char* configGetString(const char* key, const char* default_val);

// --- SETTERS ---
// These update the cache immediately and mark NVS as dirty
void configSetInt(const char* key, int32_t value);
void configSetFloat(const char* key, float value);
void configSetString(const char* key, const char* value);

// --- MANAGEMENT ---
void configUnifiedFlush();  // Commit dirty changes to NVS (Call in loop)
void configUnifiedSave();   // Force immediate save
void configUnifiedReset();  // Factory Reset
void configUnifiedClear();  // Clear RAM cache
int configGetKeyCount();

// --- DIAGNOSTICS & TOOLS ---
void configUnifiedDiagnostics();
void configUnifiedPrintAll(); // <-- Added for CLI 'config dump'

#endif // CONFIG_UNIFIED_H