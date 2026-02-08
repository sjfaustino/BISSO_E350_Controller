/**
 * @file config_unified.cpp
 * @brief Unified Configuration Manager (NVS) v3.5.20
 * @details Implements Input Validation, Hardened String Pool, and Dump Utility.
 * @author Sergio Faustino
 */

#include "config_unified.h"
#include "cli.h" // PHASE 4.1: Support table-driven dump
#include "config_keys.h"
#include "config_cache.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>
#include <nvs_flash.h>
#include "system_utils.h"       // Safe reboot helper
#include "string_safety.h"


// NVS Persistence Object
static Preferences prefs;

// Internal Cache Table
static config_entry_t config_table[CONFIG_MAX_KEYS];
static int config_count = 0;

// Thread Safety: Mutex for config_table access
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static SemaphoreHandle_t config_cache_mutex = NULL;
#define CONFIG_MUTEX_TIMEOUT_MS 100

// State Flags
static bool initialized = false;
static bool config_dirty = false;
static uint32_t last_nvs_save = 0;

// Auto-Save Configuration
#define NVS_CONFIG_SAVE_INTERVAL_MS 5000
#define NVS_SAVE_ON_CRITICAL true

// Critical keys trigger immediate write to flash to prevent data loss
static const char *critical_keys[] = {
    KEY_PPM_X,
    KEY_PPM_Y,
    KEY_PPM_Z,
    KEY_PPM_A,
    KEY_X_LIMIT_MIN,
    KEY_X_LIMIT_MAX,
    KEY_Y_LIMIT_MIN,
    KEY_Y_LIMIT_MAX,
    KEY_Z_LIMIT_MIN,
    KEY_Z_LIMIT_MAX,
    KEY_A_LIMIT_MIN,
    KEY_A_LIMIT_MAX,
    KEY_ALARM_PIN,
    KEY_STALL_TIMEOUT,
    KEY_MOTION_STRICT_LIMITS, // Safety Critical
    KEY_HOME_PROFILE_FAST,    // Homing
    KEY_HOME_PROFILE_SLOW,
    KEY_DEFAULT_ACCEL, // Float
    KEY_DEFAULT_SPEED, // Float
    KEY_WEB_USERNAME,  // Security Critical
    KEY_WEB_PASSWORD,  // Security Critical
    KEY_WEB_PW_CHANGED, // Security Critical
    KEY_WIFI_AP_EN,    // WiFi AP Mode
    KEY_WIFI_AP_SSID,
    KEY_WIFI_AP_PASS,
    KEY_STATUS_LIGHT_EN, // Status Indication
    KEY_STATUS_LIGHT_GREEN,
    KEY_STATUS_LIGHT_YELLOW,
    KEY_STATUS_LIGHT_RED,
    KEY_BUZZER_EN,       // Audible Alarm
    KEY_BUZZER_PIN,
    KEY_LCD_EN,        // Display Enable
    KEY_YHTC05_ENABLED // Tachometer Enable
};

static bool isCriticalKey(const char *key) {
  for (uint8_t i = 0; i < sizeof(critical_keys) / sizeof(critical_keys[0]);
       i++) {
    if (strcmp(key, critical_keys[i]) == 0)
      return true;
  }
  return false;
}

// ----------------------------------------------------------------------------
// STRING BUFFER POOL (Safety Fix) - PHASE 5.7: Cursor AI Enhanced Documentation
// ----------------------------------------------------------------------------
// Increased to 8 to prevent overwrites during complex logging/formatting.
//
// ⚠️ **CRITICAL POINTER LIFETIME WARNING** ⚠️
//
// Strings returned by configGetString() use a ROTATING BUFFER POOL.
// Pointers become INVALID after 8 subsequent calls to configGetString().
//
// SAFE USAGE:
//   const char* name = configGetString(KEY_WEB_USERNAME, "admin");
//   printf("Username: %s\n", name);  // ✅ OK - immediate use
//
// UNSAFE USAGE (USE-AFTER-FREE):
//   const char* name = configGetString(KEY_WEB_USERNAME, "admin");
//   // ... 8 more configGetString() calls ...
//   printf("Username: %s\n", name);  // ❌ DANGER - pointer now invalid!
//
// SAFE ALTERNATIVES:
//   1. Use immediately after retrieval
//   2. Copy to local buffer: char local[256]; safe_strcpy(local, sizeof(local),
//   name);
//   3. Use configGetStringSafe() which copies to your buffer
//
// BUFFER LIFETIME: Valid for 8 configGetString() calls or until function return
#define CONFIG_STRING_BUFFER_COUNT 8
#define CONFIG_STRING_BUFFER_SIZE 256

static struct {
  char buffers[CONFIG_STRING_BUFFER_COUNT][CONFIG_STRING_BUFFER_SIZE];
  uint8_t current_buffer = 0;
} string_return_pool = {};

/**
 * @brief Gets next buffer from rotating pool
 * @warning Returned pointer valid for only 8 more configGetString() calls!
 * @return Pointer to buffer (will be overwritten after pool rotates)
 */
static char *configGetStringBuffer() {
  char *buffer = string_return_pool.buffers[string_return_pool.current_buffer];
  string_return_pool.current_buffer =
      (string_return_pool.current_buffer + 1) % CONFIG_STRING_BUFFER_COUNT;
  return buffer; // ⚠️ WARNING: Pointer lifetime limited to 8 calls
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

// Thread-safe config entry lookup with mutex protection
static int findConfigEntry(const char *key) {
  if (!key)
    return -1;

  int result = -1;

  // Acquire mutex for safe cache access
  if (config_cache_mutex != NULL) {
    if (xSemaphoreTakeRecursive(config_cache_mutex,
                       pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) == pdTRUE) {
      for (int i = 0; i < config_count; i++) {
        if (strcmp(config_table[i].key, key) == 0) {
          result = i;
          break;
        }
      }
      xSemaphoreGiveRecursive(config_cache_mutex);
    } else {
      // Mutex timeout - log warning but still try unprotected for robustness
      logWarning("[CONFIG] Cache mutex timeout - accessing without lock");
      for (int i = 0; i < config_count; i++) {
        if (strcmp(config_table[i].key, key) == 0) {
          result = i;
          break;
        }
      }
    }
  } else {
    // Mutex not initialized yet (early boot) - access directly
    for (int i = 0; i < config_count; i++) {
      if (strcmp(config_table[i].key, key) == 0) {
        result = i;
        break;
      }
    }
  }

  return result;
}

static void addToCacheInt(const char *key, int32_t val) {
  if (config_count >= CONFIG_MAX_KEYS)
    return;

  // Acquire mutex for safe cache modification
  bool mutex_held = false;
  if (config_cache_mutex != NULL) {
    mutex_held =
        (xSemaphoreTakeRecursive(config_cache_mutex,
                        pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) == pdTRUE);
  }

  int idx = config_count++;
  SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
  config_table[idx].type = CONFIG_INT32;
  config_table[idx].value.int_val = val;
  config_table[idx].is_set = true;

  if (mutex_held)
    xSemaphoreGiveRecursive(config_cache_mutex);
}

static void addToCacheFloat(const char *key, float val) {
  if (config_count >= CONFIG_MAX_KEYS)
    return;

  // Acquire mutex for safe cache modification
  bool mutex_held = false;
  if (config_cache_mutex != NULL) {
    mutex_held =
        (xSemaphoreTakeRecursive(config_cache_mutex,
                        pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) == pdTRUE);
  }

  int idx = config_count++;
  SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
  config_table[idx].type = CONFIG_FLOAT;
  config_table[idx].value.float_val = val;
  config_table[idx].is_set = true;

  if (mutex_held)
    xSemaphoreGiveRecursive(config_cache_mutex);
}

// ----------------------------------------------------------------------------
// VALIDATION LOGIC (Safety Fix)
// ----------------------------------------------------------------------------

static int32_t validateInt(const char *key, int32_t value) {
  // 1. Pulses Per MM/Degree (Must be positive)
  if (strstr(key, "ppm_") != NULL) {
    if (value <= 0) {
      logError("[CONFIG] Invalid PPM value %ld (Must be > 0)", (long)value);
      return 1000; // Default safe value
    }
  }

  // 2. Timeout Safety
  if (strcmp(key, KEY_STALL_TIMEOUT) == 0) {
    return (value < 100) ? 100 : (value > 60000 ? 60000 : value);
  }

  // 3. Profiles (0-2)
  if (strstr(key, "home_prof_") != NULL) {
    return (value < 0) ? 0 : (value > 2 ? 2 : value);
  }

  return value;
}

static float validateFloat(const char *key, float value) {
  // 1. Acceleration / Speed (Must be positive)
  if (strstr(key, "default_") != NULL && (strstr(key, "accel") || strstr(key, "speed"))) {
    if (value < 0.1f) return 0.1f;
  }
  return value;
}

static void validateString(const char *key, char *value, size_t len) {
  // Minimum password length for security
  const size_t MIN_PASSWORD_LENGTH = 8;

  // 1. Web credentials (Non-empty, reasonable length)
  if (strcmp(key, KEY_WEB_USERNAME) == 0 ||
      strcmp(key, KEY_WEB_PASSWORD) == 0) {
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
    // Enforce minimum length for password (not username)
    if (strcmp(key, KEY_WEB_PASSWORD) == 0 &&
        strlen(value) < MIN_PASSWORD_LENGTH) {
      logWarning("[CONFIG] Password too short (min %d chars), using default",
                 MIN_PASSWORD_LENGTH);
      strncpy(value, "password", len - 1); // Default password is 8 chars
      value[len - 1] = '\0';
    }
    // Username minimum 4 chars (less strict than password)
    if (strcmp(key, KEY_WEB_USERNAME) == 0 && strlen(value) < 4) {
      logWarning("[CONFIG] Username too short (min 4 chars), using default");
      strncpy(value, "admin", len - 1);
      value[len - 1] = '\0';
    }
  }
}

// ============================================================================
// INITIALIZATION & DEFAULTS
// ============================================================================

void configSetDefaults() {
  if (!initialized)
    return;

  logInfo("[CONFIG] Applying Factory Defaults...");

  // SAFETY: Default to Strict Limits (1 = E-Stop on any drift)
  if (!prefs.isKey(KEY_MOTION_STRICT_LIMITS))
    prefs.putInt(KEY_MOTION_STRICT_LIMITS, 1);

  // HOMING
  if (!prefs.isKey(KEY_HOME_PROFILE_FAST))
    prefs.putInt(KEY_HOME_PROFILE_FAST, 2);
  if (!prefs.isKey(KEY_HOME_PROFILE_SLOW))
    prefs.putInt(KEY_HOME_PROFILE_SLOW, 0);

  // MOTION
  if (!prefs.isKey(KEY_MOTION_BUFFER_ENABLE))
    prefs.putInt(KEY_MOTION_BUFFER_ENABLE, 1);

  // AXIS
  if (!prefs.isKey(KEY_X_APPROACH))
    prefs.putInt(KEY_X_APPROACH, 5);         // Final approach (SLOW) at 5mm
  if (!prefs.isKey(KEY_X_APPROACH_MED))
    prefs.putInt(KEY_X_APPROACH_MED, 20);    // Medium approach at 20mm
  if (!prefs.isKey(KEY_TARGET_MARGIN))
    prefs.putFloat(KEY_TARGET_MARGIN, 0.1f); // Target position margin 0.1mm
  if (!prefs.isKey(KEY_DEFAULT_ACCEL))
    prefs.putFloat(KEY_DEFAULT_ACCEL, 100.0f);

  // HARDWARE
  if (!prefs.isKey(KEY_ALARM_PIN))
    prefs.putInt(KEY_ALARM_PIN, 2);
  if (!prefs.isKey(KEY_STALL_TIMEOUT))
    prefs.putInt(KEY_STALL_TIMEOUT, 2000);
  if (!prefs.isKey(KEY_SPINDL_TOOLBREAK_THR))
    prefs.putFloat(KEY_SPINDL_TOOLBREAK_THR, 5.0f);
  if (!prefs.isKey(KEY_SPINDL_PAUSE_THR))
    prefs.putInt(KEY_SPINDL_PAUSE_THR, 25);
  // PHASE 5.0: Consolidated Spindle/JXK10 Defaults
  if (!prefs.isKey(KEY_JXK10_ADDR))
    prefs.putInt(KEY_JXK10_ADDR, 1);
  if (!prefs.isKey(KEY_JXK10_ENABLED))
    prefs.putInt(KEY_JXK10_ENABLED, 1);
  if (!prefs.isKey(KEY_SPINDLE_THRESHOLD))
    prefs.putInt(KEY_SPINDLE_THRESHOLD, 30);

  // WEB SERVER CREDENTIALS
  if (!prefs.isKey(KEY_WEB_USERNAME))
    prefs.putString(KEY_WEB_USERNAME, "admin");
  if (!prefs.isKey(KEY_WEB_PASSWORD))
    prefs.putString(KEY_WEB_PASSWORD, "password");
  if (!prefs.isKey(KEY_WEB_PW_CHANGED))
    prefs.putInt(KEY_WEB_PW_CHANGED, 0); // 0 = default, needs change

  // WIFI AP MODE (PHASE 5.8: Configurable AP)
  if (!prefs.isKey(KEY_WIFI_AP_EN))
    prefs.putInt(KEY_WIFI_AP_EN, 1); // ENABLED BY DEFAULT
  if (!prefs.isKey(KEY_WIFI_AP_SSID))
    prefs.putString(KEY_WIFI_AP_SSID, "BISSO-E350-Setup");
  if (!prefs.isKey(KEY_WIFI_AP_PASS))
    prefs.putString(KEY_WIFI_AP_PASS, "password");
  if (!prefs.isKey(KEY_LCD_EN))
    prefs.putInt(KEY_LCD_EN, 1);
}

void configUnifiedLoad() {
  logInfo("[CONFIG] Pre-loading Cache...");

  for (uint8_t i = 0; i < sizeof(critical_keys) / sizeof(critical_keys[0]);
       i++) {
    const char *key = critical_keys[i];

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

result_t configUnifiedInit() {
  logModuleInit("CONFIG");

  // Create mutex for thread-safe cache access
  if (config_cache_mutex == NULL) {
    config_cache_mutex = xSemaphoreCreateRecursiveMutex();
    if (config_cache_mutex == NULL) {
      logError("[CONFIG] [CRITICAL] Failed to create cache mutex!");
    }
  }

  memset(config_table, 0, sizeof(config_table));
  config_count = 0;

  if (!prefs.begin("PosiPro_cfg", false)) {
    logError("[CONFIG] NVS Mount Failed!");
    return RESULT_ERROR_STORAGE;
  }

  initialized = true;
  configSetDefaults();
  configUnifiedLoad();
  logInfo("[CONFIG] Ready. Loaded %d entries.", config_count);
  
  // Log NVS space usage on boot
  configLogNvsStats();

  // Initialize Typed Cache (PHASE 6.7)
  configCacheInit();

  return RESULT_OK;
}

/**
 * @brief Cleanup configuration system resources
 *
 * PHASE 5.10: Resource Leak Fix - Properly cleanup mutex and NVS
 * Should be called before system shutdown/reboot
 */
void configUnifiedCleanup() {
  logInfo("[CONFIG] Cleaning up resources...");

  // Close NVS preferences
  if (initialized) {
    prefs.end();
    initialized = false;
  }

  // Delete mutex
  if (config_cache_mutex != NULL) {
    vSemaphoreDelete(config_cache_mutex);
    config_cache_mutex = NULL;
    logInfo("[CONFIG] Mutex deleted");
  }

  // Clear cache
  config_count = 0;
  logInfo("[CONFIG] Cleanup complete");
}

// ============================================================================
// GETTERS
// ============================================================================

int32_t configGetInt(const char *key, int32_t default_val) {
  if (!initialized)
    return default_val;
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_INT32 &&
      config_table[idx].is_set) {
    return config_table[idx].value.int_val;
  }
  // CRITICAL FIX: Check if key exists before calling getInt()
  // Prevents ESP32 Preferences library from logging ERROR messages for missing
  // keys
  if (prefs.isKey(key)) {
    return prefs.getInt(key, default_val);
  }
  return default_val;
}

float configGetFloat(const char *key, float default_val) {
  if (!initialized)
    return default_val;
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_FLOAT &&
      config_table[idx].is_set) {
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

const char *configGetString(const char *key, const char *default_val) {
  if (!initialized)
    return default_val ? default_val : "";
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_STRING &&
      config_table[idx].is_set) {
    return config_table[idx].value.str_val;
  }

  if (prefs.isKey(key)) {
    char *buffer = configGetStringBuffer();
    if (prefs.getString(key, buffer, CONFIG_STRING_BUFFER_SIZE) > 0) {
      return buffer;
    }
  }
  return default_val ? default_val : "";
}

// ============================================================================
// SETTERS (With Validation)
// ============================================================================

result_t configSetInt(const char *key, int32_t value) {
  if (!initialized)
    return RESULT_NOT_READY;

  // VALIDATION STEP
  int32_t original_value = value;
  value = validateInt(key, value);
  if (original_value != value) {
     // If validation changed the value significantly, we might consider it an error
     // but for now we follow the existing "clamp and log" pattern.
     // However, the audit suggests returning RESULT_ERROR_INVALID_PARAM.
  }

  // PHASE 5.10: Protect config_table writes with mutex
  if (config_cache_mutex != NULL) {
    if (xSemaphoreTakeRecursive(config_cache_mutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
      logWarning("[CONFIG] Mutex timeout in configSetInt");
      return RESULT_TIMEOUT;
    }
  }

  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);
      return RESULT_ERROR_MEMORY;
    }
    idx = config_count++;
    SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
    config_table[idx].type = CONFIG_INT32;
  }

  if (config_table[idx].is_set && config_table[idx].value.int_val == value) {
    if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);
    return RESULT_OK;
  }

  config_table[idx].value.int_val = value;
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();

  if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);

  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putInt(key, value);
    config_dirty = false;
    logInfo("[CONFIG] Set %s = %ld (Saved)", key, (long)value);
  } else {
    logInfo("[CONFIG] Set %s = %ld (Cached)", key, (long)value);
  }

  // PHASE 5.10: Signal configuration change event
  systemEventsSystemSet(EVENT_SYSTEM_CONFIG_CHANGED);

  // Update Typed Cache (PHASE 6.7)
  configCacheUpdate(key);

  return RESULT_OK;
}

result_t configSetFloat(const char *key, float value) {
  if (!initialized)
    return RESULT_NOT_READY;

  // VALIDATION STEP
  value = validateFloat(key, value);

  // PHASE 5.10: Protect config_table writes with mutex
  if (config_cache_mutex != NULL) {
    if (xSemaphoreTakeRecursive(config_cache_mutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
      logWarning("[CONFIG] Mutex timeout in configSetFloat");
      return RESULT_TIMEOUT;
    }
  }

  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);
      return RESULT_ERROR_MEMORY;
    }
    idx = config_count++;
    SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
    config_table[idx].type = CONFIG_FLOAT;
  }

  if (config_table[idx].is_set &&
      fabsf(config_table[idx].value.float_val - value) < 0.0001f) {
    if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);
    return RESULT_OK;
  }

  config_table[idx].value.float_val = value;
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();

  if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);

  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putFloat(key, value);
    config_dirty = false;
  }

  // PHASE 5.10: Signal configuration change event
  systemEventsSystemSet(EVENT_SYSTEM_CONFIG_CHANGED);

  // Update Typed Cache (PHASE 6.7)
  configCacheUpdate(key);

  return RESULT_OK;
}

result_t configSetString(const char *key, const char *value) {
  if (!initialized)
    return RESULT_NOT_READY;

  if (!value)
    return RESULT_INVALID_PARAM;

  // Create a mutable copy for validation
  char validated_value[CONFIG_VALUE_LEN];
  SAFE_STRCPY(validated_value, value, CONFIG_VALUE_LEN);

  // VALIDATION STEP
  validateString(key, validated_value, CONFIG_VALUE_LEN);

  // PHASE 5.10: Protect config_table writes with mutex
  if (config_cache_mutex != NULL) {
    if (xSemaphoreTakeRecursive(config_cache_mutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
      logWarning("[CONFIG] Mutex timeout in configSetString");
      return RESULT_TIMEOUT;
    }
  }

  int idx = findConfigEntry(key);
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);
      return RESULT_ERROR_MEMORY;
    }
    idx = config_count++;
    SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
    config_table[idx].type = CONFIG_STRING;
  }

  if (config_table[idx].is_set &&
      strncmp(config_table[idx].value.str_val, validated_value,
              CONFIG_VALUE_LEN) == 0) {
    if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);
    return RESULT_OK;
  }

  SAFE_STRCPY(config_table[idx].value.str_val, validated_value,
              CONFIG_VALUE_LEN);
  config_table[idx].is_set = true;
  config_dirty = true;
  last_nvs_save = millis();

  if (config_cache_mutex != NULL) xSemaphoreGiveRecursive(config_cache_mutex);

  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putString(key, validated_value);
    config_dirty = false;
    logInfo("[CONFIG] Set %s (Saved)", key);
  } else {
    logInfo("[CONFIG] Set %s (Cached)", key);
  }

  // PHASE 5.10: Signal configuration change event
  systemEventsSystemSet(EVENT_SYSTEM_CONFIG_CHANGED);

  // Update Typed Cache (PHASE 6.7)
  configCacheUpdate(key);

  return RESULT_OK;
}

// ============================================================================
// UTILITIES
// ============================================================================

void configUnifiedFlush() {
  if (!config_dirty)
    return;
  // PHASE 5.1: Wraparound-safe timeout comparison
  if ((uint32_t)(millis() - last_nvs_save) > NVS_CONFIG_SAVE_INTERVAL_MS) {
    configUnifiedSave();
    config_dirty = false;
  }
}

result_t configUnifiedSave() {
  if (!initialized || !config_dirty)
    return RESULT_OK;

  logInfo("[CONFIG] Saving to NVS...");

  // PHASE 5.10: Protect config_table read during save
  if (config_cache_mutex != NULL) {
    if (xSemaphoreTakeRecursive(config_cache_mutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
      logWarning("[CONFIG] Mutex timeout in configUnifiedSave");
      return RESULT_TIMEOUT;
    }
  }

  bool success = true;
  for (int i = 0; i < config_count; i++) {
    if (!config_table[i].is_set)
      continue;
    // Skip if critical key (already write-through)
    if (config_table[i].type == CONFIG_INT32 &&
        isCriticalKey(config_table[i].key))
      continue;

    size_t result = 0;
    switch (config_table[i].type) {
    case CONFIG_INT32:
      result = prefs.putInt(config_table[i].key, config_table[i].value.int_val);
      break;
    case CONFIG_FLOAT:
      result = prefs.putFloat(config_table[i].key, config_table[i].value.float_val);
      break;
    case CONFIG_STRING:
      result = prefs.putString(config_table[i].key, config_table[i].value.str_val);
      break;
    }
    if (result == 0) success = false;
    
    // Prevent task starvation during long save operations
    if (i % 10 == 0) taskYIELD();
  }
  config_dirty = false;
  
  if (config_cache_mutex != NULL) {
    xSemaphoreGiveRecursive(config_cache_mutex);
  }
  
  if (success) {
    logInfo("[CONFIG] Save Complete.");
    return RESULT_OK;
  } else {
    logError("[CONFIG] Some keys failed to save");
    return RESULT_ERROR_STORAGE;
  }
}

result_t configUnifiedReset() {
  logWarning("[CONFIG] Resetting to Factory Defaults...");

  // PHASE 5.10: Clear in-memory cache before reset to prevent stale values
  configUnifiedClear();

  if (!prefs.clear()) {
    logError("[CONFIG] Failed to clear NVS storage");
    return RESULT_ERROR_STORAGE;
  }
  
  configSetDefaults();

  // Safe defaults
  configSetInt(KEY_X_LIMIT_MIN, -500000);
  configSetInt(KEY_X_LIMIT_MAX, 500000);

  logInfo("[CONFIG] Reset Complete. Reboot recommended.");
  return RESULT_OK;
}

void configUnifiedClear() {
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
}

int configGetKeyCount() { return config_count; }

void configUnifiedDiagnostics() {
  serialLoggerLock();
  logPrintln("\n=== CONFIG DIAGNOSTICS ===");
  logPrintf("Strict Limits: %s\r\n", configGetInt(KEY_MOTION_STRICT_LIMITS, 1)
                                           ? "ON (Safe)"
                                           : "OFF (Recovery)");
  logPrintf("Home Fast Profile: %ld\r\n",
                (long)configGetInt(KEY_HOME_PROFILE_FAST, 2));
  logPrintf("Buffer Enable: %s\r\n",
                configGetInt(KEY_MOTION_BUFFER_ENABLE, 1) ? "YES" : "NO");
  logPrintf("Total Keys: %d\r\n", config_count);
  logPrintln("==========================\n");
  serialLoggerUnlock();
}

// PHASE 5.1: Validated getters with bounds checking
int32_t configGetIntValidated(const char *key, int32_t default_val,
                              int32_t min_val, int32_t max_val) {
  if (!key)
    return default_val;

  int32_t value = configGetInt(key, default_val);

  // Apply validation bounds
  if (min_val >= 0 && value < min_val) {
    logWarning(
        "[CONFIG] Value %ld below minimum %ld for key '%s', using minimum",
        (long)value, (long)min_val, key);
    return min_val;
  }

  if (max_val >= 0 && value > max_val) {
    logWarning(
        "[CONFIG] Value %ld exceeds maximum %ld for key '%s', using maximum",
        (long)value, (long)max_val, key);
    return max_val;
  }

  return value;
}

float configGetFloatValidated(const char *key, float default_val, float min_val,
                              float max_val) {
  if (!key)
    return default_val;

  float value = configGetFloat(key, default_val);

  // Apply validation bounds
  if (min_val >= 0.0f && value < min_val) {
    logWarning(
        "[CONFIG] Value %.2f below minimum %.2f for key '%s', using minimum",
        (double)value, (double)min_val, key);
    return min_val;
  }

  if (max_val >= 0.0f && value > max_val) {
    logWarning(
        "[CONFIG] Value %.2f exceeds maximum %.2f for key '%s', using maximum",
        (double)value, (double)max_val, key);
    return max_val;
  }

  return value;
}

// Added for CLI 'config dump' command
void configUnifiedPrintAll() {
  serialLoggerLock();
  for (int i = 0; i < config_count; i++) {
    if (!config_table[i].is_set)
      continue;

    char val_buf[128];
    switch (config_table[i].type) {
    case CONFIG_INT32:
      SAFE_SNPRINTF(val_buf, sizeof(val_buf), "%ld", (long)config_table[i].value.int_val);
      break;
    case CONFIG_FLOAT:
      SAFE_SNPRINTF(val_buf, sizeof(val_buf), "%.3f", config_table[i].value.float_val);
      break;
    case CONFIG_STRING:
      SAFE_SNPRINTF(val_buf, sizeof(val_buf), "\"%s\"", config_table[i].value.str_val);
      break;
    default:
      SAFE_STRCPY(val_buf, "???", sizeof(val_buf));
    }
    
    cliPrintTableRow(config_table[i].key, val_buf, nullptr, 30, 20, 0);
  }
  serialLoggerUnlock();
}

void* configGetMutex() {
  return (void*)config_cache_mutex;
}

// ============================================================================
// NVS SPACE MANAGEMENT
// ============================================================================

void configLogNvsStats() {
  nvs_stats_t nvs_stats;
  esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
  if (err == ESP_OK) {
    uint32_t used_pct = (nvs_stats.used_entries * 100) / nvs_stats.total_entries;
    logInfo("[NVS] Entries: %d/%d used (%d%%), Free: %d", 
            nvs_stats.used_entries, nvs_stats.total_entries, 
            used_pct, nvs_stats.free_entries);
    
    if (used_pct > 80) {
      logWarning("[NVS] WARNING: Storage >80%% full! Consider erasing unused keys.");
    }
  } else {
    logError("[NVS] Failed to get stats: %d", err);
  }
}

void configDumpNvsContents() {
  logInfo("[NVS] Starting NVS Dump...");
  
  // Iterator for all namespaces and keys
  nvs_iterator_t it = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY);
  
  if (it == NULL) {
    logInfo("[NVS] No entries found or iterator failed.");
    return;
  }

  logInfo("\n[NVS] === NVS CONTENT DUMP ===");
  logPrintf("%-12s | %-20s | %-4s | %s\n", "Namespace", "Key", "Type", "Value");
  logPrintf("-------------|----------------------|------|--------------------------------\n");

  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    
    // Open handle to read value
    nvs_handle_t handle;
    esp_err_t err = nvs_open(info.namespace_name, NVS_READONLY, &handle);
    
    char value_str[128] = "ERR";
    
    if (err == ESP_OK) {
        switch(info.type) {
            case NVS_TYPE_U8: {
                uint8_t v; nvs_get_u8(handle, info.key, &v);
                snprintf(value_str, sizeof(value_str), "%u", v);
                break;
            }
            case NVS_TYPE_I8: {
                int8_t v; nvs_get_i8(handle, info.key, &v);
                snprintf(value_str, sizeof(value_str), "%d", v);
                break;
            }
            case NVS_TYPE_U16: {
                uint16_t v; nvs_get_u16(handle, info.key, &v);
                snprintf(value_str, sizeof(value_str), "%u", v);
                break;
            }
            case NVS_TYPE_I16: {
                int16_t v; nvs_get_i16(handle, info.key, &v);
                snprintf(value_str, sizeof(value_str), "%d", v);
                break;
            }
            case NVS_TYPE_U32: {
                uint32_t v; nvs_get_u32(handle, info.key, &v);
                snprintf(value_str, sizeof(value_str), "%u", v);
                break;
            }
            case NVS_TYPE_I32: {
                int32_t v; nvs_get_i32(handle, info.key, &v);
                snprintf(value_str, sizeof(value_str), "%d", v);
                break;
            }
            case NVS_TYPE_U64: {
                uint64_t v; nvs_get_u64(handle, info.key, &v);
                snprintf(value_str, sizeof(value_str), "%llu", v);
                break;
            }
            case NVS_TYPE_I64: {
                int64_t v; nvs_get_i64(handle, info.key, &v);
                snprintf(value_str, sizeof(value_str), "%lld", v);
                break;
            }
            case NVS_TYPE_STR: {
                size_t len = 0;
                nvs_get_str(handle, info.key, NULL, &len);
                if (len < sizeof(value_str)) {
                    nvs_get_str(handle, info.key, value_str, &len);
                } else {
                    snprintf(value_str, sizeof(value_str), "[STR too long: %u]", len);
                }
                break;
            }
            case NVS_TYPE_BLOB: {
                size_t len = 0;
                nvs_get_blob(handle, info.key, NULL, &len);
                snprintf(value_str, sizeof(value_str), "[BLOB: %u bytes]", len);
                break;
            }
            default:
                snprintf(value_str, sizeof(value_str), "?");
        }
        nvs_close(handle);
    } else {
        snprintf(value_str, sizeof(value_str), "OPEN ERR");
    }

    // Short type string
    const char* type_str = "UNK";
    switch(info.type) {
      case NVS_TYPE_U8:  type_str = "U8"; break;
      case NVS_TYPE_I8:  type_str = "I8"; break;
      case NVS_TYPE_U16: type_str = "U16"; break;
      case NVS_TYPE_I16: type_str = "I16"; break;
      case NVS_TYPE_U32: type_str = "U32"; break;
      case NVS_TYPE_I32: type_str = "I32"; break;
      case NVS_TYPE_U64: type_str = "U64"; break;
      case NVS_TYPE_I64: type_str = "I64"; break;
      case NVS_TYPE_STR: type_str = "STR"; break;
      case NVS_TYPE_BLOB: type_str = "BLOB"; break;
      case NVS_TYPE_ANY: type_str = "ANY"; break;
    }

    logPrintf("%-12s | %-20s | %-4s | %s\n", info.namespace_name, info.key, type_str, value_str);
    
    it = nvs_entry_next(it);
  }
  
  logInfo("[NVS] === END DUMP ===\n");
}

void configEraseNamespace(const char* ns) {
  if (ns == NULL || strlen(ns) == 0) return;

  logInfo("[NVS] Erasing namespace '%s'...", ns);
  
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
  
  if (err == ESP_OK) {
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
      nvs_commit(handle);
      logInfo("[NVS] [OK] Namespace '%s' erased.", ns);
    } else {
      logError("[NVS] Failed to erase: %d", err);
    }
    nvs_close(handle);
  } else {
    logError("[NVS] Failed to open namespace '%s': %d", ns, err);
  }
}

bool configEraseNvs() {
  logWarning("[NVS] Erasing all NVS data...");
  prefs.end();
  esp_err_t err = nvs_flash_erase();
  if (err == ESP_OK) {
    logInfo("[NVS] Erase complete. Rebooting in 2 seconds...");
    delay(2000);
    systemSafeReboot("NVS erase complete");

    return true;
  } else {
    logError("[NVS] Erase failed: %d", err);
    return false;
  }
}
