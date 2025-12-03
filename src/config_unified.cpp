#include "config_unified.h"
#include "serial_logger.h"
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

static const char* critical_keys[] = {
  "calibration_x", "calibration_y", "calibration_z", "calibration_a",
  "emergency_stop", "safety_enabled",
  "x_soft_limit_min", "x_soft_limit_max",
  "y_soft_limit_min", "y_soft_limit_max",
  "z_soft_limit_min", "z_soft_limit_max",
  "a_soft_limit_min", "a_soft_limit_max"
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
  Serial.println("[CONFIG] Initializing...");
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
  
  if (!prefs.begin("bisso_config", false)) {
    Serial.println("[CONFIG] [FAIL] NVS init failed!");
    return;
  }
  configUnifiedLoad();
  Serial.printf("[CONFIG] [OK] Loaded %d entries\n", config_count);
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

  // SMART WRITE: Check if value changed
  if (config_table[idx].is_set && config_table[idx].value.int_val == value) return;
  
  config_table[idx].value.int_val = value;
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();
  
  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putInt(key, value);
    config_dirty = false; 
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
  Serial.printf("[CONFIG] Saving %d entries... ", config_count);
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
  Serial.println("[OK]");
  last_nvs_save = millis();
  config_dirty = false;
}

void configUnifiedLoad() {
  Serial.println("[CONFIG] Cache ready.");
}

void configUnifiedReset() {
  Serial.print("[CONFIG] Resetting defaults... ");
  configSetInt("x_soft_limit_min", -500000); configSetInt("x_soft_limit_max", 500000);
  configSetInt("y_soft_limit_min", -300000); configSetInt("y_soft_limit_max", 300000);
  configSetInt("z_soft_limit_min", -50000);  configSetInt("z_soft_limit_max", 150000);
  configSetInt("a_soft_limit_min", -45000);  configSetInt("a_soft_limit_max", 45000);
  configSetFloat("default_speed_mm_s", 15.0f); 
  configSetFloat("default_acceleration", 5.0f);
  configSetInt("encoder_ppm_x", 1000); configSetInt("encoder_ppm_y", 1000);
  configSetInt("encoder_ppm_z", 1000); configSetInt("encoder_ppm_a", 1000); 
  configSetInt("alarm_pin", 2);
  configSetInt("stall_timeout_ms", 2000);
  configSetFloat("speed_X_mm_s", 0.0f); configSetFloat("speed_Y_mm_s", 0.0f);
  configSetFloat("speed_Z_mm_s", 0.0f); configSetFloat("speed_A_mm_s", 0.0f);
  
  configUnifiedSave();
  Serial.println("[OK]");
}

void configUnifiedClear() {
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
}

int configGetKeyCount() { return config_count; }

void configUnifiedDiagnostics() {
  Serial.println("\n[CONFIG] === Diagnostics ===");
  Serial.printf("Entries: %d | Dirty: %s\n", config_count, config_dirty ? "YES" : "NO");
  for (int i = 0; i < config_count; i++) {
    Serial.printf("  [%d] %s = ", i, config_table[i].key);
    switch(config_table[i].type) {
      case CONFIG_INT32: Serial.println(config_table[i].value.int_val); break;
      case CONFIG_FLOAT: Serial.println(config_table[i].value.float_val); break;
      case CONFIG_STRING: Serial.println(config_table[i].value.str_val); break;
    }
  }
}