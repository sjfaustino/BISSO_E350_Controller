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

void configUnifiedInit();
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

#endif