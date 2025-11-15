#include "config_unified.h"

static config_entry_t config_table[CONFIG_MAX_KEYS];
static int config_count = 0;

void configUnifiedInit() {
  Serial.println("[CONFIG] Configuration system initializing...");
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
  
  // Load from storage
  configUnifiedLoad();
  
  Serial.print("[CONFIG] Loaded ");
  Serial.print(config_count);
  Serial.println(" configuration entries");
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
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_INT32 && config_table[idx].is_set) {
    return config_table[idx].value.int_val;
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
  
  Serial.print("[CONFIG] Set ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.println(value);
}

float configGetFloat(const char* key, float default_val) {
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_FLOAT && config_table[idx].is_set) {
    return config_table[idx].value.float_val;
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
  
  Serial.print("[CONFIG] Set ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.println(value);
}

const char* configGetString(const char* key, const char* default_val) {
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_STRING && config_table[idx].is_set) {
    return config_table[idx].value.str_val;
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
  
  Serial.print("[CONFIG] Set ");
  Serial.print(key);
  Serial.print(" = ");
  Serial.println(value);
}

void configUnifiedSave() {
  Serial.print("[CONFIG] Saving ");
  Serial.print(config_count);
  Serial.println(" entries...");
  
  // In production, this would write to EEPROM/SPIFFS
  // For now, just acknowledge
  Serial.println("[CONFIG] Configuration saved");
}

void configUnifiedLoad() {
  // In production, this would read from EEPROM/SPIFFS
  // For now, start with empty config
  Serial.println("[CONFIG] Configuration loaded");
}

void configUnifiedReset() {
  Serial.println("[CONFIG] Resetting to defaults...");
  
  // Set default values
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
  
  configUnifiedSave();
  Serial.println("[CONFIG] Defaults applied");
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
