#include "config_unified.h"
#include "serial_logger.h"
#include "config_keys.h" 
#include "system_constants.h"
#include <Preferences.h>
#include <string.h>
#include <math.h> 

static Preferences prefs;
static config_entry_t config_table[CONFIG_MAX_KEYS];
static int config_count = 0;

#define NVS_CONFIG_SAVE_INTERVAL_MS 5000 
#define NVS_SAVE_ON_CRITICAL true        

static uint32_t last_nvs_save = 0;
static bool config_dirty = false;

// Critical keys that should save immediately
static const char* critical_keys[] = {
  KEY_PPM_X, KEY_PPM_Y, KEY_PPM_Z, KEY_PPM_A,
  KEY_X_LIMIT_MIN, KEY_X_LIMIT_MAX,
  KEY_Y_LIMIT_MIN, KEY_Y_LIMIT_MAX,
  KEY_Z_LIMIT_MIN, KEY_Z_LIMIT_MAX,
  KEY_A_LIMIT_MIN, KEY_A_LIMIT_MAX,
  KEY_STALL_TIMEOUT, KEY_ALARM_PIN,
  KEY_MOTION_APPROACH_MODE,
  KEY_MOTION_STRICT_LIMITS // <-- Added to critical list
};

static bool isCriticalKey(const char* key) {
  for (uint8_t i = 0; i < sizeof(critical_keys)/sizeof(critical_keys[0]); i++) {
    if (strcmp(key, critical_keys[i]) == 0) return true;
  }
  return false;
}

#define CONFIG_STRING_BUFFER_COUNT 4
#define CONFIG_STRING_BUFFER_SIZE 256
static struct {
  char buffers[CONFIG_STRING_BUFFER_COUNT][CONFIG_STRING_BUFFER_SIZE];
  uint8_t current_buffer = 0;
} string_return_pool = {};

static char* configGetStringBuffer() {
  char* buffer = string_return_pool.buffers[string_return_pool.current_buffer];
  string_return_pool.current_buffer = (string_return_pool.current_buffer + 1) % CONFIG_STRING_BUFFER_COUNT;
  return buffer;
}

void configUnifiedInit() {
  logInfo("[CONFIG] Initializing...");
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
  
  if (!prefs.begin("bisso_config", false)) {
    logError("[CONFIG] NVS Init Failed!");
    return;
  }
  
  // Auto-set default strict limits if key doesn't exist yet (Safety First)
  if (!prefs.isKey(KEY_MOTION_STRICT_LIMITS)) {
      prefs.putInt(KEY_MOTION_STRICT_LIMITS, 1);
  }

  configUnifiedLoad();
  logInfo("[CONFIG] Loaded %d entries", config_count);
}

static int findConfigEntry(const char* key) {
  for (int i = 0; i < config_count; i++) {
    if (strcmp(config_table[i].key, key) == 0) return i;
  }
  return -1;
}

int32_t configGetInt(const char* key, int32_t default_val) {
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_INT32 && config_table[idx].is_set) {
    return config_table[idx].value.int_val;
  }
  if (prefs.isKey(key)) return prefs.getInt(key, default_val);
  return default_val;
}

void configSetInt(const char* key, int32_t value) {
  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      logError("[CONFIG] Table full!");
      return;
    }
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_INT32;
  }

  // SMART WRITE
  if (config_table[idx].is_set && config_table[idx].value.int_val == value) return;
  
  config_table[idx].value.int_val = value;
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();
  
  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putInt(key, value);
    config_dirty = false; 
    logInfo("[CONFIG] Set %s = %d (Saved)", key, value);
  } else {
    logInfo("[CONFIG] Set %s = %d (Cached)", key, value);
  }
}

float configGetFloat(const char* key, float default_val) {
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_FLOAT && config_table[idx].is_set) {
    return config_table[idx].value.float_val;
  }
  if (prefs.isKey(key)) return prefs.getFloat(key, default_val);
  return default_val;
}

void configSetFloat(const char* key, float value) {
  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) return;
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_FLOAT;
  }
  
  // SMART WRITE
  if (config_table[idx].is_set && fabsf(config_table[idx].value.float_val - value) < 0.0001f) return;

  config_table[idx].value.float_val = value;
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();
  
  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putFloat(key, value);
    config_dirty = false;
  }
}

const char* configGetString(const char* key, const char* default_val) {
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_STRING && config_table[idx].is_set) {
    return config_table[idx].value.str_val;
  }
  if (prefs.isKey(key)) {
    char* buffer = configGetStringBuffer();
    size_t bytes_read = prefs.getString(key, buffer, CONFIG_STRING_BUFFER_SIZE);
    if (bytes_read > 0) {
      buffer[CONFIG_STRING_BUFFER_SIZE - 1] = '\0';
      if (idx < 0 && config_count < CONFIG_MAX_KEYS) {
        idx = config_count++;
        strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
        config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
      }
      if (idx >= 0) {
        config_table[idx].type = CONFIG_STRING;
        strncpy(config_table[idx].value.str_val, buffer, CONFIG_VALUE_LEN - 1);
        config_table[idx].value.str_val[CONFIG_VALUE_LEN - 1] = '\0';
        config_table[idx].is_set = true;
      }
      return buffer;
    }
  }
  return default_val ? default_val : "";
}

void configSetString(const char* key, const char* value) {
  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) return;
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_STRING;
  }
  
  // SMART WRITE
  if (config_table[idx].is_set && strncmp(config_table[idx].value.str_val, value, CONFIG_VALUE_LEN) == 0) return;

  strncpy(config_table[idx].value.str_val, value, CONFIG_VALUE_LEN - 1);
  config_table[idx].value.str_val[CONFIG_VALUE_LEN - 1] = '\0';
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();
}

void configUnifiedFlush() {
    if (!config_dirty) return;
    if (millis() - last_nvs_save > NVS_CONFIG_SAVE_INTERVAL_MS) {
        configUnifiedSave();
        config_dirty = false;
    }
}

void configUnifiedSave() {
  logInfo("[CONFIG] Saving to NVS...");
  for (int i = 0; i < config_count; i++) {
    if (!config_table[i].is_set) continue;
    if (config_table[i].type != CONFIG_INT32 || !isCriticalKey(config_table[i].key)) {
        switch(config_table[i].type) {
        case CONFIG_INT32: prefs.putInt(config_table[i].key, config_table[i].value.int_val); break;
        case CONFIG_FLOAT: prefs.putFloat(config_table[i].key, config_table[i].value.float_val); break;
        case CONFIG_STRING: prefs.putString(config_table[i].key, config_table[i].value.str_val); break;
        }
    }
  }
  prefs.end(); 
  prefs.begin("bisso_config", false); 
  logInfo("[CONFIG] Save complete.");
  last_nvs_save = millis();
  config_dirty = false;
}

void configUnifiedLoad() {
  Serial.println("[CONFIG] Cache ready.");
}

void configUnifiedReset() {
  logInfo("[CONFIG] Resetting defaults...");
  configSetInt(KEY_X_LIMIT_MIN, -500000); configSetInt(KEY_X_LIMIT_MAX, 500000);
  configSetInt(KEY_Y_LIMIT_MIN, -300000); configSetInt(KEY_Y_LIMIT_MAX, 300000);
  configSetInt(KEY_Z_LIMIT_MIN, -50000);  configSetInt(KEY_Z_LIMIT_MAX, 150000);
  configSetInt(KEY_A_LIMIT_MIN, -45000);  configSetInt(KEY_A_LIMIT_MAX, 45000);
  configSetFloat(KEY_DEFAULT_SPEED, 15.0f); 
  configSetFloat(KEY_DEFAULT_ACCEL, 5.0f);
  configSetInt(KEY_PPM_X, 1000); configSetInt(KEY_PPM_Y, 1000);
  configSetInt(KEY_PPM_Z, 1000); configSetInt(KEY_PPM_A, 1000); 
  configSetInt(KEY_ALARM_PIN, 2);
  configSetInt(KEY_STALL_TIMEOUT, 2000);
  configSetInt(KEY_X_APPROACH, 50);
  configSetInt(KEY_MOTION_DEADBAND, 10);
  
  // Initialize new mode to FIXED (0)
  configSetInt(KEY_MOTION_APPROACH_MODE, APPROACH_MODE_FIXED);
  
  // NEW: Initialize Safety Logic (1=Strict, 0=Recovery)
  configSetInt(KEY_MOTION_STRICT_LIMITS, 1);

  configSetFloat(KEY_SPEED_CAL_X, 0.0f); configSetFloat(KEY_SPEED_CAL_Y, 0.0f);
  configSetFloat(KEY_SPEED_CAL_Z, 0.0f); configSetFloat(KEY_SPEED_CAL_A, 0.0f);
  
  configUnifiedSave();
  logInfo("[CONFIG] Defaults saved.");
}

void configUnifiedClear() {
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
}

int configGetKeyCount() { return config_count; }

void configUnifiedDiagnostics() {
  Serial.println("\n=== CONFIG CACHE ===");
  Serial.printf("Entries: %d | Dirty: %s\n", config_count, config_dirty ? "YES" : "NO");
  Serial.printf("Strict Limits: %s\n", configGetInt(KEY_MOTION_STRICT_LIMITS, 1) ? "ON" : "OFF");
  
  for (int i = 0; i < config_count; i++) {
    Serial.printf("  [%d] %s = ", i, config_table[i].key);
    switch(config_table[i].type) {
      case CONFIG_INT32: Serial.println(config_table[i].value.int_val); break;
      case CONFIG_FLOAT: Serial.println(config_table[i].value.float_val); break;
      case CONFIG_STRING: Serial.println(config_table[i].value.str_val); break;
    }
  }
}