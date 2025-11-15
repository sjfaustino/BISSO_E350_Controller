#include "config_unified.h"
#include <Preferences.h>

// NVS-backed persistent storage
static Preferences prefs;

static config_entry_t config_table[CONFIG_MAX_KEYS];
static int config_count = 0;

void configUnifiedInit() {
  Serial.println("[CONFIG] Configuration system initializing...");
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
  
  // Initialize NVS (ESP32 Preferences)
  if (!prefs.begin("bisso_config", false)) {
    Serial.println("[CONFIG] ERROR: Failed to initialize NVS storage!");
    return;
  }
  Serial.println("[CONFIG] NVS storage initialized (wear leveling enabled)");
  
  // Load from persistent storage
  configUnifiedLoad();
  
  Serial.print("[CONFIG] Loaded ");
  Serial.print(config_count);
  Serial.println(" configuration entries from NVS");
}

static int findConfigEntry(const char* key) {
  for (int i = 0; i < config_count; i++) {
    if (strcmp(config_table[i].key, key) == 0) {
      return i;
    }
  }
  return -1;
}

int32_t configGetInt(const char* key, int32_t default_val) {
  // Check in-memory cache first
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_INT32 && config_table[idx].is_set) {
    return config_table[idx].value.int_val;
  }
  
  // Check NVS for persistent value
  if (prefs.isKey(key)) {
    return prefs.getInt(key, default_val);
  }
  
  return default_val;
}

void configSetInt(const char* key, int32_t value) {
  int idx = findConfigEntry(key);
  
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      Serial.println("[CONFIG] ERROR: Config table full");
      return;
    }
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].type = CONFIG_INT32;
  }
  
  config_table[idx].value.int_val = value;
  config_table[idx].is_set = true;
  
  // Also save to NVS immediately for persistence
  prefs.putInt(key, value);
  
  Serial.print("[CONFIG] Set ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.print(value);
  Serial.println(" (saved to NVS)");
}

float configGetFloat(const char* key, float default_val) {
  // Check in-memory cache first
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_FLOAT && config_table[idx].is_set) {
    return config_table[idx].value.float_val;
  }
  
  // Check NVS for persistent value
  if (prefs.isKey(key)) {
    return prefs.getFloat(key, default_val);
  }
  
  return default_val;
}

void configSetFloat(const char* key, float value) {
  int idx = findConfigEntry(key);
  
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      Serial.println("[CONFIG] ERROR: Config table full");
      return;
    }
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].type = CONFIG_FLOAT;
  }
  
  config_table[idx].value.float_val = value;
  config_table[idx].is_set = true;
  
  // Also save to NVS immediately for persistence
  prefs.putFloat(key, value);
  
  Serial.print("[CONFIG] Set ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.print(value);
  Serial.println(" (saved to NVS)");
}

const char* configGetString(const char* key, const char* default_val) {
  // Check in-memory cache first
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_STRING && config_table[idx].is_set) {
    return config_table[idx].value.str_val;
  }
  
  // Check NVS for persistent value
  if (prefs.isKey(key)) {
    static char nvs_buffer[CONFIG_VALUE_LEN];
    prefs.getString(key, nvs_buffer, CONFIG_VALUE_LEN);
    return nvs_buffer;
  }
  
  return default_val;
}

void configSetString(const char* key, const char* value) {
  int idx = findConfigEntry(key);
  
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      Serial.println("[CONFIG] ERROR: Config table full");
      return;
    }
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].type = CONFIG_STRING;
  }
  
  strncpy(config_table[idx].value.str_val, value, CONFIG_VALUE_LEN - 1);
  config_table[idx].is_set = true;
  
  // Also save to NVS immediately for persistence
  prefs.putString(key, value);
  
  Serial.print("[CONFIG] Set ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.print(value);
  Serial.println(" (saved to NVS)");
}

void configUnifiedSave() {
  Serial.print("[CONFIG] Saving ");
  Serial.print(config_count);
  Serial.println(" entries to NVS...");
  
  // Write each entry to NVS
  for (int i = 0; i < config_count; i++) {
    if (!config_table[i].is_set) continue;
    
    switch(config_table[i].type) {
      case CONFIG_INT32:
        prefs.putInt(config_table[i].key, config_table[i].value.int_val);
        break;
      case CONFIG_FLOAT:
        prefs.putFloat(config_table[i].key, config_table[i].value.float_val);
        break;
      case CONFIG_STRING:
        prefs.putString(config_table[i].key, config_table[i].value.str_val);
        break;
    }
  }
  
  Serial.print("[CONFIG] ✅ Saved to NVS (");
  Serial.print(prefs.freeEntries());
  Serial.println(" free entries remaining)");
}

void configUnifiedLoad() {
  Serial.println("[CONFIG] Loading configuration from NVS...");
  
  // Configuration is loaded on-demand (keys checked individually)
  // Preferences API doesn't provide entry count, so we verify on access
  Serial.println("[CONFIG] Configuration system ready for on-demand loading");
  
  // For this implementation, we'll load on-demand
  // Full enumeration of NVS keys would require additional Preferences methods
  // This is acceptable since we access values by key in the get/set functions
  
  Serial.println("[CONFIG] ✅ NVS storage ready for on-demand access");
}

void configUnifiedReset() {
  Serial.println("[CONFIG] Resetting to defaults and saving to NVS...");
  
  // Set default values - ALL will auto-save to NVS
  configSetInt("x_soft_limit_min", -50000);
  configSetInt("x_soft_limit_max", 50000);
  configSetInt("y_soft_limit_min", -50000);
  configSetInt("y_soft_limit_max", 50000);
  configSetInt("z_soft_limit_min", -50000);
  configSetInt("z_soft_limit_max", 50000);
  configSetInt("a_soft_limit_min", 0);
  configSetInt("a_soft_limit_max", 360000);
  
  configSetFloat("default_speed_mm_s", 50.0f);
  configSetFloat("default_acceleration", 5.0f);
  
  configSetInt("encoder_ppm_x", 0);
  configSetInt("encoder_ppm_y", 0);
  configSetInt("encoder_ppm_z", 0);
  configSetInt("encoder_ppm_a", 0);
  
  configSetInt("alarm_pin", 2);
  configSetInt("stall_timeout_ms", 2000);
  
  // Speed calibration defaults (will be overwritten by actual calibration)
  configSetFloat("speed_X_mm_s", 0.0f);
  configSetFloat("speed_Y_mm_s", 0.0f);
  configSetFloat("speed_Z_mm_s", 0.0f);
  configSetFloat("speed_A_mm_s", 0.0f);
  
  configUnifiedSave();
  Serial.println("[CONFIG] ✅ All defaults set and saved to NVS");
}

void configUnifiedClear() {
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
  Serial.println("[CONFIG] Configuration cleared");
}

int configGetKeyCount() {
  return config_count;
}

void configUnifiedDiagnostics() {
  Serial.println("\n[CONFIG] === Configuration Diagnostics ===");
  Serial.print("Total Entries: ");
  Serial.println(config_count);
  Serial.print("Max Entries: ");
  Serial.println(CONFIG_MAX_KEYS);
  Serial.print("Usage: ");
  Serial.print((config_count * 100) / CONFIG_MAX_KEYS);
  Serial.println("%");
  
  Serial.println("\nConfiguration Table:");
  for (int i = 0; i < config_count; i++) {
    Serial.print("  [");
    Serial.print(i);
    Serial.print("] ");
    Serial.print(config_table[i].key);
    Serial.print(" (");
    
    switch(config_table[i].type) {
      case CONFIG_INT32:
        Serial.print("INT32) = ");
        Serial.println(config_table[i].value.int_val);
        break;
      case CONFIG_FLOAT:
        Serial.print("FLOAT) = ");
        Serial.println(config_table[i].value.float_val);
        break;
      case CONFIG_STRING:
        Serial.print("STRING) = ");
        Serial.println(config_table[i].value.str_val);
        break;
    }
  }
}
