/**
 * @file config_unified.cpp
 * @brief Unified Configuration Manager (NVS) v3.5.20
 * @details Implements Input Validation, Hardened String Pool, and Dump Utility.
 * @author Sergio Faustino
 */

#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include "system_constants.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>
#include <math.h>

// NVS Persistence Object
static Preferences prefs;

// Internal Cache Table
static config_entry_t config_table[CONFIG_MAX_KEYS];
static int config_count = 0;

// State Flags
static bool initialized = false;
static bool config_dirty = false;
static uint32_t last_nvs_save = 0;

// Auto-Save Configuration
#define NVS_CONFIG_SAVE_INTERVAL_MS 5000 
#define NVS_SAVE_ON_CRITICAL true        

// Critical keys trigger immediate write to flash to prevent data loss
static const char* critical_keys[] = {
  KEY_PPM_X, KEY_PPM_Y, KEY_PPM_Z, KEY_PPM_A,
  KEY_X_LIMIT_MIN, KEY_X_LIMIT_MAX,
  KEY_Y_LIMIT_MIN, KEY_Y_LIMIT_MAX,
  KEY_Z_LIMIT_MIN, KEY_Z_LIMIT_MAX,
  KEY_A_LIMIT_MIN, KEY_A_LIMIT_MAX,
  KEY_ALARM_PIN, KEY_STALL_TIMEOUT,
  KEY_MOTION_APPROACH_MODE,
  KEY_MOTION_STRICT_LIMITS, // Safety Critical
  KEY_HOME_PROFILE_FAST,    // Homing
  KEY_HOME_PROFILE_SLOW,
  KEY_DEFAULT_ACCEL,        // Float
  KEY_DEFAULT_SPEED,        // Float
  KEY_WEB_USERNAME,         // Security Critical
  KEY_WEB_PASSWORD,         // Security Critical
  KEY_WEB_PW_CHANGED        // Security Critical
};

static bool isCriticalKey(const char* key) {
  for (uint8_t i = 0; i < sizeof(critical_keys)/sizeof(critical_keys[0]); i++) {
    if (strcmp(key, critical_keys[i]) == 0) return true;
  }
  return false;
}

// ----------------------------------------------------------------------------
// STRING BUFFER POOL (Safety Fix)
// ----------------------------------------------------------------------------
// Increased to 8 to prevent overwrites during complex logging/formatting.
// WARNING: Strings returned by configGetString are valid only until 
// the pool rotates (8 calls later). Do not store pointers long-term.
#define CONFIG_STRING_BUFFER_COUNT 8
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

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static int findConfigEntry(const char* key) {
  for (int i = 0; i < config_count; i++) {
    if (strcmp(config_table[i].key, key) == 0) return i;
  }
  return -1;
}

static void addToCacheInt(const char* key, int32_t val) {
    if (config_count >= CONFIG_MAX_KEYS) return;
    int idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_INT32;
    config_table[idx].value.int_val = val;
    config_table[idx].is_set = true;
}

static void addToCacheFloat(const char* key, float val) {
    if (config_count >= CONFIG_MAX_KEYS) return;
    int idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_FLOAT;
    config_table[idx].value.float_val = val;
    config_table[idx].is_set = true;
}

// ----------------------------------------------------------------------------
// VALIDATION LOGIC (Safety Fix)
// ----------------------------------------------------------------------------

static int32_t validateInt(const char* key, int32_t value) {
    // 1. Pulses Per MM/Degree (Must be positive)
    if (strcmp(key, KEY_PPM_X) == 0 || strcmp(key, KEY_PPM_Y) == 0 ||
        strcmp(key, KEY_PPM_Z) == 0 || strcmp(key, KEY_PPM_A) == 0) {
        if (value <= 0) {
            logError("[CONFIG] Invalid PPM value %d (Must be > 0)", (long)value);
            return 1000; // Default safe value
        }
    }
    
    // 2. Timeout Safety
    if (strcmp(key, KEY_STALL_TIMEOUT) == 0) {
        if (value < 100) return 100; // Minimum 100ms
        if (value > 60000) return 60000; // Max 60s
    }
    
    // 3. Profiles (0-2)
    if (strcmp(key, KEY_HOME_PROFILE_FAST) == 0 || strcmp(key, KEY_HOME_PROFILE_SLOW) == 0) {
        if (value < 0) return 0;
        if (value > 2) return 2;
    }
    
    // 4. Deadband (Positive)
    if (strcmp(key, KEY_MOTION_DEADBAND) == 0) {
        if (value < 0) return 0;
    }

    return value;
}

static float validateFloat(const char* key, float value) {
    // 1. Acceleration / Speed (Must be positive)
    if (strcmp(key, KEY_DEFAULT_ACCEL) == 0 || strcmp(key, KEY_DEFAULT_SPEED) == 0) {
        if (value < 0.1f) return 0.1f;
    }
    return value;
}

static void validateString(const char* key, char* value, size_t len) {
    // 1. Web credentials (Non-empty, reasonable length)
    if (strcmp(key, KEY_WEB_USERNAME) == 0 || strcmp(key, KEY_WEB_PASSWORD) == 0) {
        if (value[0] == '\0') {
            // Empty string - use default
            if (strcmp(key, KEY_WEB_USERNAME) == 0) {
                strncpy(value, "admin", len - 1);
            } else {
                strncpy(value, "password", len - 1);
            }
            value[len - 1] = '\0';
            logWarning("[CONFIG] %s was empty, using default", key);
        }
        // Enforce minimum length of 4 characters for security
        if (strlen(value) < 4) {
            logWarning("[CONFIG] %s too short (min 4 chars), padding", key);
            if (strcmp(key, KEY_WEB_USERNAME) == 0) {
                strncpy(value, "admin", len - 1);
            } else {
                strncpy(value, "password", len - 1);
            }
            value[len - 1] = '\0';
        }
    }
}

// ============================================================================
// INITIALIZATION & DEFAULTS
// ============================================================================

void configSetDefaults() {
    if (!initialized) return;

    logInfo("[CONFIG] Applying Factory Defaults...");

    // SAFETY: Default to Strict Limits (1 = E-Stop on any drift)
    if (!prefs.isKey(KEY_MOTION_STRICT_LIMITS)) prefs.putInt(KEY_MOTION_STRICT_LIMITS, 1);

    // HOMING
    if (!prefs.isKey(KEY_HOME_PROFILE_FAST)) prefs.putInt(KEY_HOME_PROFILE_FAST, 2);
    if (!prefs.isKey(KEY_HOME_PROFILE_SLOW)) prefs.putInt(KEY_HOME_PROFILE_SLOW, 0);

    // MOTION
    if (!prefs.isKey(KEY_MOTION_DEADBAND))      prefs.putInt(KEY_MOTION_DEADBAND, 10);
    if (!prefs.isKey(KEY_MOTION_BUFFER_ENABLE)) prefs.putInt(KEY_MOTION_BUFFER_ENABLE, 1);
    if (!prefs.isKey(KEY_MOTION_APPROACH_MODE)) prefs.putInt(KEY_MOTION_APPROACH_MODE, 0);

    // AXIS
    if (!prefs.isKey(KEY_X_APPROACH))    prefs.putInt(KEY_X_APPROACH, 50);
    if (!prefs.isKey(KEY_DEFAULT_ACCEL)) prefs.putFloat(KEY_DEFAULT_ACCEL, 100.0f);

    // HARDWARE
    if (!prefs.isKey(KEY_ALARM_PIN))     prefs.putInt(KEY_ALARM_PIN, 2);
    if (!prefs.isKey(KEY_STALL_TIMEOUT)) prefs.putInt(KEY_STALL_TIMEOUT, 2000);

    // WEB SERVER CREDENTIALS
    if (!prefs.isKey(KEY_WEB_USERNAME)) prefs.putString(KEY_WEB_USERNAME, "admin");
    if (!prefs.isKey(KEY_WEB_PASSWORD)) prefs.putString(KEY_WEB_PASSWORD, "password");
    if (!prefs.isKey(KEY_WEB_PW_CHANGED)) prefs.putInt(KEY_WEB_PW_CHANGED, 0);  // 0 = default, needs change
}

void configUnifiedLoad() {
    logInfo("[CONFIG] Pre-loading Cache...");
    
    for (uint8_t i = 0; i < sizeof(critical_keys)/sizeof(critical_keys[0]); i++) {
        const char* key = critical_keys[i];
        
        if (prefs.isKey(key)) {
            // Heuristic for types based on key name content
            if (strstr(key, "accel") || strstr(key, "speed")) {
                float val = prefs.getFloat(key, 0.0f);
                addToCacheFloat(key, val);
            } else {
                int32_t val = prefs.getInt(key, 0);
                addToCacheInt(key, val);
            }
        }
    }
}

void configUnifiedInit() {
    logInfo("[CONFIG] Initializing NVS...");
    memset(config_table, 0, sizeof(config_table));
    config_count = 0;
    
    if (!prefs.begin("gemini_cfg", false)) { 
        logError("[CONFIG] NVS Mount Failed!");
        return;
    }
    
    initialized = true;
    configSetDefaults(); 
    configUnifiedLoad();
    logInfo("[CONFIG] Ready. Loaded %d entries.", config_count);
}

// ============================================================================
// GETTERS
// ============================================================================

int32_t configGetInt(const char* key, int32_t default_val) {
  if (!initialized) return default_val;
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_INT32 && config_table[idx].is_set) {
    return config_table[idx].value.int_val;
  }
  // CRITICAL FIX: Check if key exists before calling getInt()
  // Prevents ESP32 Preferences library from logging ERROR messages for missing keys
  if (prefs.isKey(key)) {
    return prefs.getInt(key, default_val);
  }
  return default_val;
}

float configGetFloat(const char* key, float default_val) {
  if (!initialized) return default_val;
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_FLOAT && config_table[idx].is_set) {
    return config_table[idx].value.float_val;
  }
  // CRITICAL FIX: Check if key exists before calling getFloat()
  // This prevents ESP32 Preferences library from logging ERROR messages
  // when loading WCS offsets (g54_x, g54_y, etc.) that don't exist yet
  if (prefs.isKey(key)) {
    return prefs.getFloat(key, default_val);
  }
  return default_val;
}

const char* configGetString(const char* key, const char* default_val) {
  if (!initialized) return default_val ? default_val : "";
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_STRING && config_table[idx].is_set) {
    return config_table[idx].value.str_val;
  }
  
  if (prefs.isKey(key)) {
    char* buffer = configGetStringBuffer();
    if (prefs.getString(key, buffer, CONFIG_STRING_BUFFER_SIZE) > 0) {
        return buffer;
    }
  }
  return default_val ? default_val : "";
}

// ============================================================================
// SETTERS (With Validation)
// ============================================================================

void configSetInt(const char* key, int32_t value) {
  if (!initialized) return;
  
  // VALIDATION STEP
  value = validateInt(key, value);

  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) return;
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_INT32;
  }

  if (config_table[idx].is_set && config_table[idx].value.int_val == value) return;
  
  config_table[idx].value.int_val = value;
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();
  
  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putInt(key, value);
    config_dirty = false; 
    logInfo("[CONFIG] Set %s = %ld (Saved)", key, (long)value);
  } else {
    logInfo("[CONFIG] Set %s = %ld (Cached)", key, (long)value);
  }
}

void configSetFloat(const char* key, float value) {
  if (!initialized) return;
  
  // VALIDATION STEP
  value = validateFloat(key, value);

  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) return;
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_FLOAT;
  }
  
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

void configSetString(const char* key, const char* value) {
  if (!initialized) return;

  // Create a mutable copy for validation
  char validated_value[CONFIG_VALUE_LEN];
  strncpy(validated_value, value ? value : "", CONFIG_VALUE_LEN - 1);
  validated_value[CONFIG_VALUE_LEN - 1] = '\0';

  // VALIDATION STEP
  validateString(key, validated_value, CONFIG_VALUE_LEN);

  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) return;
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_STRING;
  }

  if (config_table[idx].is_set && strncmp(config_table[idx].value.str_val, validated_value, CONFIG_VALUE_LEN) == 0) return;

  strncpy(config_table[idx].value.str_val, validated_value, CONFIG_VALUE_LEN - 1);
  config_table[idx].value.str_val[CONFIG_VALUE_LEN - 1] = '\0';
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();

  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putString(key, validated_value);
    config_dirty = false;
    logInfo("[CONFIG] Set %s (Saved)", key);
  } else {
    logInfo("[CONFIG] Set %s (Cached)", key);
  }
}

// ============================================================================
// UTILITIES
// ============================================================================

void configUnifiedFlush() {
    if (!config_dirty) return;
    // PHASE 5.1: Wraparound-safe timeout comparison
    if ((uint32_t)(millis() - last_nvs_save) > NVS_CONFIG_SAVE_INTERVAL_MS) {
        configUnifiedSave();
        config_dirty = false;
    }
}

void configUnifiedSave() {
  logInfo("[CONFIG] Flushing NVS...");
  for (int i = 0; i < config_count; i++) {
    if (!config_table[i].is_set) continue;
    // Skip if critical key (already write-through)
    if (config_table[i].type == CONFIG_INT32 && isCriticalKey(config_table[i].key)) continue;

    switch(config_table[i].type) {
      case CONFIG_INT32: prefs.putInt(config_table[i].key, config_table[i].value.int_val); break;
      case CONFIG_FLOAT: prefs.putFloat(config_table[i].key, config_table[i].value.float_val); break;
      case CONFIG_STRING: prefs.putString(config_table[i].key, config_table[i].value.str_val); break;
    }
  }
  config_dirty = false;
  logInfo("[CONFIG] Flush Complete.");
}

void configUnifiedReset() {
  logWarning("[CONFIG] Resetting to Factory Defaults...");
  prefs.clear(); 
  configSetDefaults();
  
  // Safe defaults
  configSetInt(KEY_X_LIMIT_MIN, -500000); 
  configSetInt(KEY_X_LIMIT_MAX, 500000);
  
  logInfo("[CONFIG] Reset Complete. Reboot recommended.");
}

void configUnifiedClear() {
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
}

int configGetKeyCount() { return config_count; }

void configUnifiedDiagnostics() {
  Serial.println("\n=== CONFIG DIAGNOSTICS ===");
  Serial.printf("Strict Limits: %s\n", configGetInt(KEY_MOTION_STRICT_LIMITS, 1) ? "ON (Safe)" : "OFF (Recovery)");
  Serial.printf("Home Fast Profile: %ld\n", (long)configGetInt(KEY_HOME_PROFILE_FAST, 2));
  Serial.printf("Buffer Enable: %s\n", configGetInt(KEY_MOTION_BUFFER_ENABLE, 1) ? "YES" : "NO");
  Serial.printf("Total Keys: %d\n", config_count);
  Serial.println("==========================\n");
}

// PHASE 5.1: Validated getters with bounds checking
int32_t configGetIntValidated(const char* key, int32_t default_val, int32_t min_val, int32_t max_val) {
    if (!key) return default_val;

    int32_t value = configGetInt(key, default_val);

    // Apply validation bounds
    if (min_val >= 0 && value < min_val) {
        logWarning("[CONFIG] Value %ld below minimum %ld for key '%s', using minimum",
                  (long)value, (long)min_val, key);
        return min_val;
    }

    if (max_val >= 0 && value > max_val) {
        logWarning("[CONFIG] Value %ld exceeds maximum %ld for key '%s', using maximum",
                  (long)value, (long)max_val, key);
        return max_val;
    }

    return value;
}

float configGetFloatValidated(const char* key, float default_val, float min_val, float max_val) {
    if (!key) return default_val;

    float value = configGetFloat(key, default_val);

    // Apply validation bounds
    if (min_val >= 0.0f && value < min_val) {
        logWarning("[CONFIG] Value %.2f below minimum %.2f for key '%s', using minimum",
                  (double)value, (double)min_val, key);
        return min_val;
    }

    if (max_val >= 0.0f && value > max_val) {
        logWarning("[CONFIG] Value %.2f exceeds maximum %.2f for key '%s', using maximum",
                  (double)value, (double)max_val, key);
        return max_val;
    }

    return value;
}

// Added for CLI 'config dump' command
void configUnifiedPrintAll() {
    for (int i = 0; i < config_count; i++) {
        if (!config_table[i].is_set) continue;

        // Pad key for alignment
        Serial.printf("%-30s | ", config_table[i].key);
        
        switch(config_table[i].type) {
            case CONFIG_INT32: 
                Serial.printf("%ld", (long)config_table[i].value.int_val); 
                break;
            case CONFIG_FLOAT: 
                Serial.printf("%.3f", config_table[i].value.float_val); 
                break;
            case CONFIG_STRING: 
                Serial.printf("\"%s\"", config_table[i].value.str_val); 
                break;
        }
        Serial.println();
    }
}