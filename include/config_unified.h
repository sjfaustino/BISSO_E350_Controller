#ifndef CONFIG_UNIFIED_H
#define CONFIG_UNIFIED_H

#include <Arduino.h>

#define CONFIG_MAX_KEYS 64
#define CONFIG_KEY_LEN 32
#define CONFIG_VALUE_LEN 256
#define CONFIG_STORAGE_SIZE 4096

typedef enum {
  CONFIG_INT32 = 0,
  CONFIG_FLOAT = 1,
  CONFIG_STRING = 2
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
int32_t configGetInt(const char* key, int32_t default_val);
void configSetInt(const char* key, int32_t value);
float configGetFloat(const char* key, float default_val);
void configSetFloat(const char* key, float value);
const char* configGetString(const char* key, const char* default_val);
void configSetString(const char* key, const char* value);
void configUnifiedSave();
void configUnifiedLoad();
void configUnifiedReset();
void configUnifiedClear();
int configGetKeyCount();
void configUnifiedDiagnostics();
void configUnifiedFlush(); // <-- NEW: Batch write handler

#endif