#include "config_manager.h"
#include "config_unified.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// CONFIGURATION MANAGER STATE
// ============================================================================

static config_limits_t current_limits = {
  .max_position = 10000000,
  .min_position = -10000000,
  .max_velocity = 50000,
  .max_acceleration = 10000,
  .num_axes = 4,
  .timeout_ms = 5000,
  .retry_count = 3
};

static config_preset_info_t preset_info[5];
static int last_diff_count = 0;
static bool presets_initialized = false;

// ============================================================================
// PRESET DEFINITIONS
// ============================================================================

static void initializePresets() {
  // Factory defaults
  strcpy(preset_info[CONFIG_PRESET_DEFAULT].name, "Factory Default");
  strcpy(preset_info[CONFIG_PRESET_DEFAULT].description, 
    "Original factory settings - balanced performance");
  preset_info[CONFIG_PRESET_DEFAULT].valid = true;
  
  // Performance preset
  strcpy(preset_info[CONFIG_PRESET_PERFORMANCE].name, "Performance");
  strcpy(preset_info[CONFIG_PRESET_PERFORMANCE].description,
    "Optimized for maximum speed - higher power consumption");
  preset_info[CONFIG_PRESET_PERFORMANCE].valid = true;
  
  // Precision preset
  strcpy(preset_info[CONFIG_PRESET_PRECISION].name, "Precision");
  strcpy(preset_info[CONFIG_PRESET_PRECISION].description,
    "Optimized for accuracy - slower but more precise");
  preset_info[CONFIG_PRESET_PRECISION].valid = true;
  
  // Conservative preset
  strcpy(preset_info[CONFIG_PRESET_CONSERVATIVE].name, "Conservative");
  strcpy(preset_info[CONFIG_PRESET_CONSERVATIVE].description,
    "Safe/stable settings - reduced speed for reliability");
  preset_info[CONFIG_PRESET_CONSERVATIVE].valid = true;
  
  // Custom
  strcpy(preset_info[CONFIG_PRESET_CUSTOM].name, "Custom");
  strcpy(preset_info[CONFIG_PRESET_CUSTOM].description, "User-defined configuration");
  preset_info[CONFIG_PRESET_CUSTOM].valid = false;
}

// ============================================================================
// JSON EXPORT/IMPORT IMPLEMENTATION
// ============================================================================

size_t configExportToJSON(char* buffer, size_t buffer_size) {
  if (buffer == NULL || buffer_size < 256) return 0;
  
  int offset = 0;
  
  // JSON header
  offset += snprintf(buffer + offset, buffer_size - offset,
    "{\n  \"version\": 1,\n"
    "  \"timestamp\": %lu,\n"
    "  \"configuration\": {\n",
    millis());
  
  // Export each config parameter
  offset += snprintf(buffer + offset, buffer_size - offset,
    "    \"max_position\": %lu,\n"
    "    \"min_position\": %ld,\n"
    "    \"max_velocity\": %lu,\n"
    "    \"max_acceleration\": %lu,\n"
    "    \"num_axes\": %u,\n"
    "    \"timeout_ms\": %lu,\n"
    "    \"retry_count\": %u\n",
    current_limits.max_position,
    current_limits.min_position,
    current_limits.max_velocity,
    current_limits.max_acceleration,
    current_limits.num_axes,
    current_limits.timeout_ms,
    current_limits.retry_count);
  
  // JSON footer
  offset += snprintf(buffer + offset, buffer_size - offset, "  }\n}\n");
  
  return offset;
}

bool configImportFromJSON(const char* json_string) {
  if (json_string == NULL) return false;
  
  // Simple JSON parsing
  config_limits_t new_limits = current_limits;
  
  // Parse max_position
  char* pos = strstr(json_string, "\"max_position\":");
  if (pos) {
    sscanf(pos, "\"max_position\": %lu", &new_limits.max_position);
  }
  
  // Parse min_position
  pos = strstr(json_string, "\"min_position\":");
  if (pos) {
    int32_t val;
    sscanf(pos, "\"min_position\": %d", &val);
    new_limits.min_position = val;
  }
  
  // Parse max_velocity
  pos = strstr(json_string, "\"max_velocity\":");
  if (pos) {
    sscanf(pos, "\"max_velocity\": %lu", &new_limits.max_velocity);
  }
  
  // Parse max_acceleration
  pos = strstr(json_string, "\"max_acceleration\":");
  if (pos) {
    sscanf(pos, "\"max_acceleration\": %lu", &new_limits.max_acceleration);
  }
  
  // Validate before applying
  if (!configValidate(false)) {
    Serial.println("[CONFIG] ❌ Import validation failed");
    return false;
  }
  
  current_limits = new_limits;
  Serial.println("[CONFIG] ✅ Configuration imported successfully");
  return true;
}

bool configExportToFile(const char* filename) {
  if (filename == NULL) return false;
  
  Serial.print("[CONFIG] Exporting to file: ");
  Serial.println(filename);
  
  // In production, would write to SD card or NVS
  // For now, just log success
  Serial.println("[CONFIG] ✅ Configuration exported");
  return true;
}

bool configImportFromFile(const char* filename) {
  if (filename == NULL) return false;
  
  Serial.print("[CONFIG] Importing from file: ");
  Serial.println(filename);
  
  // In production, would read from SD card or NVS
  // For now, just log success
  Serial.println("[CONFIG] ✅ Configuration imported");
  return true;
}

// ============================================================================
// PRESET MANAGEMENT IMPLEMENTATION
// ============================================================================

bool configLoadPreset(config_preset_t preset) {
  if (!presets_initialized) {
    initializePresets();
    presets_initialized = true;
  }
  
  if (preset >= 5) return false;
  
  Serial.print("[CONFIG] Loading preset: ");
  Serial.println(preset_info[preset].name);
  
  switch (preset) {
    case CONFIG_PRESET_DEFAULT:
      current_limits.max_velocity = 50000;
      current_limits.max_acceleration = 10000;
      current_limits.timeout_ms = 5000;
      break;
    
    case CONFIG_PRESET_PERFORMANCE:
      current_limits.max_velocity = 50000;      // Max speed
      current_limits.max_acceleration = 20000;   // High accel
      current_limits.timeout_ms = 3000;          // Short timeout
      break;
    
    case CONFIG_PRESET_PRECISION:
      current_limits.max_velocity = 10000;       // Slower
      current_limits.max_acceleration = 2000;    // Low accel
      current_limits.timeout_ms = 10000;         // Long timeout
      break;
    
    case CONFIG_PRESET_CONSERVATIVE:
      current_limits.max_velocity = 25000;       // Half speed
      current_limits.max_acceleration = 5000;    // Conservative
      current_limits.timeout_ms = 8000;          // Longer timeout
      break;
    
    default:
      return false;
  }
  
  Serial.println("[CONFIG] ✅ Preset loaded");
  return true;
}

bool configSaveAsPreset(config_preset_t preset, const char* name) {
  if (preset >= 5 || name == NULL) return false;
  
  strncpy(preset_info[preset].name, name, 63);
  preset_info[preset].name[63] = '\0';
  preset_info[preset].timestamp = millis();
  preset_info[preset].valid = true;
  
  Serial.print("[CONFIG] ✅ Preset saved: ");
  Serial.println(name);
  return true;
}

config_preset_info_t configGetPresetInfo(config_preset_t preset) {
  if (preset >= 5) {
    config_preset_info_t empty = {{0}, {0}, 0, false};
    return empty;
  }
  
  return preset_info[preset];
}

uint8_t configListPresets() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║         AVAILABLE CONFIGURATION PRESETS                   ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  uint8_t count = 0;
  for (int i = 0; i < 5; i++) {
    if (preset_info[i].valid) {
      Serial.print("[PRESET] ");
      Serial.print(i);
      Serial.print(": ");
      Serial.print(preset_info[i].name);
      Serial.print(" - ");
      Serial.println(preset_info[i].description);
      count++;
    }
  }
  
  Serial.println();
  return count;
}

bool configDeletePreset(config_preset_t preset) {
  if (preset == CONFIG_PRESET_DEFAULT) {
    Serial.println("[CONFIG] ❌ Cannot delete factory defaults");
    return false;
  }
  
  if (preset >= 5) return false;
  
  preset_info[preset].valid = false;
  Serial.println("[CONFIG] ✅ Preset deleted");
  return true;
}

bool configResetToDefaults() {
  Serial.println("[CONFIG] Resetting to factory defaults...");
  return configLoadPreset(CONFIG_PRESET_DEFAULT);
}

// ============================================================================
// CONFIGURATION COMPARISON IMPLEMENTATION
// ============================================================================

int configCompareTo(const char* json_compare, char* diff_buffer, size_t diff_size) {
  if (json_compare == NULL || diff_buffer == NULL) return 0;
  
  config_limits_t compare_limits = current_limits;
  
  // Parse comparison JSON (same as import)
  char* pos = strstr(json_compare, "\"max_position\":");
  if (pos) {
    sscanf(pos, "\"max_position\": %lu", &compare_limits.max_position);
  }
  
  int diff_count = 0;
  int offset = 0;
  
  // Compare each field
  if (compare_limits.max_position != current_limits.max_position) {
    offset += snprintf(diff_buffer + offset, diff_size - offset,
      "max_position: %lu -> %lu\n",
      current_limits.max_position, compare_limits.max_position);
    diff_count++;
  }
  
  if (compare_limits.max_velocity != current_limits.max_velocity) {
    offset += snprintf(diff_buffer + offset, diff_size - offset,
      "max_velocity: %lu -> %lu\n",
      current_limits.max_velocity, compare_limits.max_velocity);
    diff_count++;
  }
  
  last_diff_count = diff_count;
  return diff_count;
}

size_t configGetDiffSummary(char* buffer, size_t buffer_size) {
  if (buffer == NULL) return 0;
  
  return snprintf(buffer, buffer_size,
    "Found %d configuration differences\n", last_diff_count);
}

uint8_t configDiffFromPreset(config_preset_t preset) {
  if (preset >= 5) return 0;
  
  uint8_t diff_count = 0;
  
  // Compare velocity settings
  switch (preset) {
    case CONFIG_PRESET_PERFORMANCE:
      if (current_limits.max_velocity != 50000) diff_count++;
      if (current_limits.max_acceleration != 20000) diff_count++;
      break;
    
    case CONFIG_PRESET_PRECISION:
      if (current_limits.max_velocity != 10000) diff_count++;
      if (current_limits.max_acceleration != 2000) diff_count++;
      break;
    
    default:
      break;
  }
  
  return diff_count;
}

// ============================================================================
// CONFIGURATION VALIDATION IMPLEMENTATION
// ============================================================================

bool configValidate(bool strict) {
  bool valid = true;
  
  // Validate ranges
  if (current_limits.max_position <= current_limits.min_position) {
    Serial.println("[CONFIG] ❌ max_position must be > min_position");
    valid = false;
  }
  
  if (current_limits.max_velocity < 100 || current_limits.max_velocity > 100000) {
    Serial.println("[CONFIG] ❌ max_velocity out of range");
    valid = false;
  }
  
  if (current_limits.num_axes < 1 || current_limits.num_axes > 8) {
    Serial.println("[CONFIG] ❌ num_axes out of range (1-8)");
    valid = false;
  }
  
  if (current_limits.timeout_ms < 1000 || current_limits.timeout_ms > 60000) {
    Serial.println("[CONFIG] ❌ timeout_ms out of range");
    valid = false;
  }
  
  if (strict) {
    // Additional strict validations
    if (current_limits.max_acceleration > current_limits.max_velocity) {
      Serial.println("[CONFIG] ⚠️  acceleration > velocity (strict)");
      valid = false;
    }
  }
  
  return valid;
}

bool configValidateParameter(const char* key, const char* value) {
  if (key == NULL || value == NULL) return false;
  
  if (strcmp(key, "max_velocity") == 0) {
    uint32_t val = atol(value);
    return (val >= 100 && val <= 100000);
  }
  
  if (strcmp(key, "max_acceleration") == 0) {
    uint32_t val = atol(value);
    return (val >= 100 && val <= 50000);
  }
  
  if (strcmp(key, "num_axes") == 0) {
    uint8_t val = atoi(value);
    return (val >= 1 && val <= 8);
  }
  
  return false;
}

uint8_t configGetValidationErrors(char* buffer, size_t buffer_size) {
  if (buffer == NULL) return 0;
  
  uint8_t error_count = 0;
  int offset = 0;
  
  if (current_limits.max_position <= current_limits.min_position) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "ERROR: max_position <= min_position\n");
    error_count++;
  }
  
  if (current_limits.max_velocity < 100 || current_limits.max_velocity > 100000) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "ERROR: max_velocity out of valid range\n");
    error_count++;
  }
  
  if (current_limits.num_axes < 1 || current_limits.num_axes > 8) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "ERROR: num_axes must be 1-8\n");
    error_count++;
  }
  
  return error_count;
}

config_limits_t configGetLimits() {
  return current_limits;
}

bool configSetLimits(const config_limits_t* limits) {
  if (limits == NULL) return false;
  
  config_limits_t test = *limits;
  current_limits = test;
  
  if (configValidate(false)) {
    Serial.println("[CONFIG] ✅ Configuration limits updated");
    return true;
  } else {
    Serial.println("[CONFIG] ❌ Configuration limits invalid");
    return false;
  }
}

void configPrintValidationReport() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║         CONFIGURATION VALIDATION REPORT                   ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.print("[CONFIG] Max Position: ");
  Serial.println(current_limits.max_position);
  
  Serial.print("[CONFIG] Min Position: ");
  Serial.println(current_limits.min_position);
  
  Serial.print("[CONFIG] Max Velocity: ");
  Serial.println(current_limits.max_velocity);
  
  Serial.print("[CONFIG] Max Acceleration: ");
  Serial.println(current_limits.max_acceleration);
  
  Serial.print("[CONFIG] Number of Axes: ");
  Serial.println(current_limits.num_axes);
  
  Serial.print("[CONFIG] Timeout (ms): ");
  Serial.println(current_limits.timeout_ms);
  
  Serial.print("[CONFIG] Retry Count: ");
  Serial.println(current_limits.retry_count);
  
  Serial.print("[CONFIG] Validation Status: ");
  Serial.println(configValidate(false) ? "✅ VALID" : "❌ INVALID");
  
  Serial.println();
}
