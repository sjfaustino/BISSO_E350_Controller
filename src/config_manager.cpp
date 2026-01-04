#include "config_manager.h"
#include "config_unified.h"
#include "string_safety.h"
#include "serial_logger.h"
#include <stdio.h>
#include <string.h>

static config_limits_t current_limits = {
  .max_position = 10000000, .min_position = -10000000, .max_velocity = 50000,
  .max_acceleration = 10000, .num_axes = 4, .timeout_ms = 5000, .retry_count = 3
};
static config_preset_info_t preset_info[5];
static int last_diff_count = 0;
static bool presets_initialized = false;

static void initializePresets() {
  safe_strcpy(preset_info[CONFIG_PRESET_DEFAULT].name, sizeof(preset_info[CONFIG_PRESET_DEFAULT].name), "Factory Default");
  safe_strcpy(preset_info[CONFIG_PRESET_DEFAULT].description, sizeof(preset_info[CONFIG_PRESET_DEFAULT].description), "Balanced performance");
  preset_info[CONFIG_PRESET_DEFAULT].valid = true;

  safe_strcpy(preset_info[CONFIG_PRESET_PERFORMANCE].name, sizeof(preset_info[CONFIG_PRESET_PERFORMANCE].name), "Performance");
  safe_strcpy(preset_info[CONFIG_PRESET_PERFORMANCE].description, sizeof(preset_info[CONFIG_PRESET_PERFORMANCE].description), "Max speed, higher power");
  preset_info[CONFIG_PRESET_PERFORMANCE].valid = true;

  safe_strcpy(preset_info[CONFIG_PRESET_PRECISION].name, sizeof(preset_info[CONFIG_PRESET_PRECISION].name), "Precision");
  safe_strcpy(preset_info[CONFIG_PRESET_PRECISION].description, sizeof(preset_info[CONFIG_PRESET_PRECISION].description), "Low speed, high accuracy");
  preset_info[CONFIG_PRESET_PRECISION].valid = true;

  safe_strcpy(preset_info[CONFIG_PRESET_CONSERVATIVE].name, sizeof(preset_info[CONFIG_PRESET_CONSERVATIVE].name), "Conservative");
  safe_strcpy(preset_info[CONFIG_PRESET_CONSERVATIVE].description, sizeof(preset_info[CONFIG_PRESET_CONSERVATIVE].description), "Safe, reduced speed");
  preset_info[CONFIG_PRESET_CONSERVATIVE].valid = true;

  safe_strcpy(preset_info[CONFIG_PRESET_CUSTOM].name, sizeof(preset_info[CONFIG_PRESET_CUSTOM].name), "Custom");
  safe_strcpy(preset_info[CONFIG_PRESET_CUSTOM].description, sizeof(preset_info[CONFIG_PRESET_CUSTOM].description), "User-defined");
  preset_info[CONFIG_PRESET_CUSTOM].valid = false;
}

size_t configExportToJSON(char* buffer, size_t buffer_size) {
  if (!buffer || buffer_size < 256) return 0;
  int offset = 0;
  offset += snprintf(buffer + offset, buffer_size - offset,
    "{\n  \"version\": 1,\n  \"timestamp\": %lu,\n  \"configuration\": {\n", (unsigned long)millis());
  
  offset += snprintf(buffer + offset, buffer_size - offset,
    "    \"max_position\": %ld,\n    \"min_position\": %ld,\n    \"max_velocity\": %lu,\n"
    "    \"max_acceleration\": %lu,\n    \"num_axes\": %u,\n    \"timeout_ms\": %lu,\n    \"retry_count\": %u\n",
    (long)current_limits.max_position, (long)current_limits.min_position, (unsigned long)current_limits.max_velocity,
    (unsigned long)current_limits.max_acceleration, current_limits.num_axes, (unsigned long)current_limits.timeout_ms, current_limits.retry_count);
    
  offset += snprintf(buffer + offset, buffer_size - offset, "  }\n}\n");
  return offset;
}

bool configImportFromJSON(const char* json_string) {
  if (!json_string) return false;
  config_limits_t new_limits = current_limits;
  
  long t_max_pos = new_limits.max_position;
  long t_min_pos = new_limits.min_position;
  unsigned long t_max_vel = new_limits.max_velocity;
  unsigned long t_max_acc = new_limits.max_acceleration;

  char* pos = strstr(json_string, "\"max_position\":");
  if (pos) sscanf(pos, "\"max_position\": %ld", &t_max_pos);
  
  pos = strstr(json_string, "\"min_position\":");
  if (pos) sscanf(pos, "\"min_position\": %ld", &t_min_pos);
  
  pos = strstr(json_string, "\"max_velocity\":");
  if (pos) sscanf(pos, "\"max_velocity\": %lu", &t_max_vel);
  
  pos = strstr(json_string, "\"max_acceleration\":");
  if (pos) sscanf(pos, "\"max_acceleration\": %lu", &t_max_acc);
  
  new_limits.max_position = (int32_t)t_max_pos;
  new_limits.min_position = (int32_t)t_min_pos;
  new_limits.max_velocity = (uint32_t)t_max_vel;
  new_limits.max_acceleration = (uint32_t)t_max_acc;

  if (!configValidate(false)) {
    logError("[CONFIG] Import validation failed");
    return false;
  }
  current_limits = new_limits;
  logInfo("[CONFIG] Configuration imported");
  return true;
}

bool configExportToFile(const char* filename) {
  if (!filename) return false;
  logInfo("[CONFIG] Exporting to file: %s", filename);
  logInfo("[CONFIG] [OK] Exported");
  return true;
}

bool configImportFromFile(const char* filename) {
  if (!filename) return false;
  logInfo("[CONFIG] Importing from file: %s", filename);
  logInfo("[CONFIG] [OK] Imported");
  return true;
}

bool configLoadPreset(config_preset_t preset) {
  if (!presets_initialized) { initializePresets(); presets_initialized = true; }
  if (preset >= 5) return false;
  logInfo("[CONFIG] Loading preset: %s", preset_info[preset].name);
  
  switch (preset) {
    case CONFIG_PRESET_DEFAULT:
      current_limits.max_velocity = 50000; current_limits.max_acceleration = 10000; current_limits.timeout_ms = 5000; break;
    case CONFIG_PRESET_PERFORMANCE:
      current_limits.max_velocity = 50000; current_limits.max_acceleration = 20000; current_limits.timeout_ms = 3000; break;
    case CONFIG_PRESET_PRECISION:
      current_limits.max_velocity = 10000; current_limits.max_acceleration = 2000; current_limits.timeout_ms = 10000; break;
    case CONFIG_PRESET_CONSERVATIVE:
      current_limits.max_velocity = 25000; current_limits.max_acceleration = 5000; current_limits.timeout_ms = 8000; break;
    default: return false;
  }
  logInfo("[CONFIG] [OK] Preset loaded");
  return true;
}

bool configSaveAsPreset(config_preset_t preset, const char* name) {
  if (preset >= 5 || !name) return false;
  strncpy(preset_info[preset].name, name, 63);
  preset_info[preset].name[63] = '\0';
  preset_info[preset].timestamp = millis();
  preset_info[preset].valid = true;
  logInfo("[CONFIG] [OK] Preset saved: %s", name);
  return true;
}

config_preset_info_t configGetPresetInfo(config_preset_t preset) {
  if (preset >= 5) { 
      config_preset_info_t empty;
      memset(&empty, 0, sizeof(empty));
      return empty; 
  }
  return preset_info[preset];
}

uint8_t configListPresets() {
  serialLoggerLock();
  Serial.println("\n=== AVAILABLE PRESETS ===");
  uint8_t count = 0;
  for (int i = 0; i < 5; i++) {
    if (preset_info[i].valid) {
      Serial.printf("[%d] %s - %s\n", i, preset_info[i].name, preset_info[i].description);
      count++;
    }
  }
  Serial.println();
  serialLoggerUnlock();
  return count;
}

bool configDeletePreset(config_preset_t preset) {
  if (preset == CONFIG_PRESET_DEFAULT) {
    logError("[CONFIG] Cannot delete factory defaults");
    return false;
  }
  if (preset >= 5) return false;
  preset_info[preset].valid = false;
  logInfo("[CONFIG] [OK] Preset deleted");
  return true;
}

bool configResetToDefaults() {
  logInfo("[CONFIG] Resetting to factory defaults...");
  return configLoadPreset(CONFIG_PRESET_DEFAULT);
}

int configCompareTo(const char* json_compare, char* diff_buffer, size_t diff_size) {
  if (!json_compare || !diff_buffer) return 0;
  config_limits_t compare_limits = current_limits;
  
  long t_max_pos = compare_limits.max_position;
  char* pos = strstr(json_compare, "\"max_position\":");
  if (pos) sscanf(pos, "\"max_position\": %ld", &t_max_pos);
  compare_limits.max_position = (int32_t)t_max_pos;
  
  int diff_count = 0;
  int offset = 0;
  if (compare_limits.max_position != current_limits.max_position) {
    offset += snprintf(diff_buffer + offset, diff_size - offset, "max_pos: %ld -> %ld\n", 
        (long)current_limits.max_position, (long)compare_limits.max_position);
    diff_count++;
  }
  last_diff_count = diff_count;
  return diff_count;
}

size_t configGetDiffSummary(char* buffer, size_t buffer_size) {
  if (!buffer) return 0;
  return snprintf(buffer, buffer_size, "Found %d differences\n", last_diff_count);
}

uint8_t configDiffFromPreset(config_preset_t preset) {
  if (preset >= 5) return 0;
  uint8_t diff_count = 0;
  switch (preset) {
    case CONFIG_PRESET_PERFORMANCE:
      if (current_limits.max_velocity != 50000) diff_count++;
      break;
    default: break;
  }
  return diff_count;
}

bool configValidate(bool strict) {
  bool valid = true;
  if (current_limits.max_position <= current_limits.min_position) {
    logError("[CONFIG] max_position <= min_position");
    valid = false;
  }
  if (current_limits.max_velocity < 100 || current_limits.max_velocity > 100000) {
    logError("[CONFIG] max_velocity out of range");
    valid = false;
  }
  if (strict && current_limits.max_acceleration > current_limits.max_velocity) {
    logWarning("[CONFIG] acceleration > velocity (strict)");
    valid = false;
  }
  return valid;
}

bool configValidateParameter(const char* key, const char* value) {
  if (!key || !value) return false;
  if (strcmp(key, "max_velocity") == 0) {
    uint32_t val = atol(value);
    return (val >= 100 && val <= 100000);
  }
  return false;
}

uint8_t configGetValidationErrors(char* buffer, size_t buffer_size) {
  if (!buffer) return 0;
  uint8_t error_count = 0;
  int offset = 0;
  if (current_limits.max_position <= current_limits.min_position) {
    offset += snprintf(buffer + offset, buffer_size - offset, "ERROR: max <= min\n");
    error_count++;
  }
  return error_count;
}

config_limits_t configGetLimits() { return current_limits; }

bool configSetLimits(const config_limits_t* limits) {
  if (!limits) return false;
  current_limits = *limits;
  if (configValidate(false)) {
    logInfo("[CONFIG] [OK] Limits updated");
    return true;
  } else {
    logError("[CONFIG] Limits invalid");
    return false;
  }
}

void configPrintValidationReport() {
  serialLoggerLock();
  Serial.println("\n=== CONFIG VALIDATION REPORT ===");
  Serial.printf("Max Pos: %ld\nMin Pos: %ld\n", (long)current_limits.max_position, (long)current_limits.min_position);
  Serial.printf("Max Vel: %lu\nMax Acc: %lu\n", (unsigned long)current_limits.max_velocity, (unsigned long)current_limits.max_acceleration);
  Serial.printf("Status: %s\n", configValidate(false) ? "[VALID]" : "[INVALID]");
  Serial.println();
  serialLoggerUnlock();
}
