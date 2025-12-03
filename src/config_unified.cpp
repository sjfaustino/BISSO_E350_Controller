#include "config_unified.h"
#include "serial_logger.h"
#include <Preferences.h>
#include <string.h>
#include <math.h> // Added for fabsf

// NVS-backed persistent storage
static Preferences prefs;

static config_entry_t config_table[CONFIG_MAX_KEYS];
static int config_count = 0;

// ============================================================================
// BATCH WRITE OPTIMIZATION DEFINITIONS
// ============================================================================

#define NVS_CONFIG_SAVE_INTERVAL_MS 5000 
#define NVS_SAVE_ON_CRITICAL true        

static uint32_t last_nvs_save = 0;
static bool config_dirty = false;

// Critical keys that should save immediately (for safety)
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
    if (strcmp(key, critical_keys[i]) == 0) {
      return true;
    }
  }
  return false;
}

// ============================================================================
// CIRCULAR BUFFER FOR STRING RETURNS
// ============================================================================

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
// ============================================================================

void configUnifiedInit() {
  Serial.println("[CONFIG] Configuration system initializing...");
  memset(config_table, 0, sizeof(config_table));
  config_count = 0;
  
  if (!prefs.begin("bisso_config", false)) {
    Serial.println("[CONFIG] ERROR: Failed to initialize NVS storage!");
    return;
  }
  Serial.println("[CONFIG] NVS storage initialized (wear leveling enabled)");
  
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
  if (!key) {
    logError("[CONFIG] ERROR: NULL key in configSetInt");
    return;
  }
  
  int idx = findConfigEntry(key);
  
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      logError("[CONFIG] ERROR: Config table full");
      return;
    }
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_INT32;
  }

  // --- FIX: Smart Write Protection (Flash Saver) ---
  // If the key exists, is the correct type, and has the same value, do nothing.
  if (config_table[idx].is_set && 
      config_table[idx].type == CONFIG_INT32 && 
      config_table[idx].value.int_val == value) {
      return; // Value hasn't changed, ignore write
  }
  // -------------------------------------------------
  
  // Update in-memory cache
  config_table[idx].value.int_val = value;
  config_table[idx].is_set = true;
  config_table[idx].type = CONFIG_INT32; // Ensure type is correct if reusing slot
  
  // Mark as dirty for batch write
  config_dirty = true;
  last_nvs_save = millis();
  
  // OPTIMIZATION: Save immediately only for critical keys 
  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putInt(key, value);
    logInfo("[CONFIG] Set %s = %d (critical, saved immediately)", key, value);
    config_dirty = false; // Not dirty if saved immediately
  } else {
    // Non-critical value - cache for batch write
    logInfo("[CONFIG] Set %s = %d (cached, will flush with batch)", key, value);
  }
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
  if (!key) {
    logError("[CONFIG] ERROR: NULL key in configSetFloat");
    return;
  }
  
  int idx = findConfigEntry(key);
  
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      logError("[CONFIG] ERROR: Config table full");
      return;
    }
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_FLOAT;
  }
  
  // --- FIX: Smart Write Protection (Flash Saver) ---
  if (config_table[idx].is_set && 
      config_table[idx].type == CONFIG_FLOAT && 
      fabsf(config_table[idx].value.float_val - value) < 0.0001f) {
      return; // Value hasn't changed significantly, ignore write
  }
  // -------------------------------------------------

  // Update in-memory cache
  config_table[idx].value.float_val = value;
  config_table[idx].is_set = true;
  config_table[idx].type = CONFIG_FLOAT;
  
  // Mark as dirty for batch write
  config_dirty = true;
  last_nvs_save = millis();
  
  // OPTIMIZATION: Save immediately only for critical keys 
  if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
    prefs.putFloat(key, value);
    logInfo("[CONFIG] Set %s = %.3f (critical, saved immediately)", key, value);
    config_dirty = false; // Not dirty if saved immediately
  } else {
    // Non-critical value - cache for batch write
    logInfo("[CONFIG] Set %s = %.3f (cached, will flush with batch)", key, value);
  }
}

const char* configGetString(const char* key, const char* default_val) {
  if (!key) {
    logError("[CONFIG] ERROR: NULL key in configGetString");
    return default_val;
  }
  
  // Check in-memory cache first
  int idx = findConfigEntry(key);
  if (idx >= 0 && config_table[idx].type == CONFIG_STRING && config_table[idx].is_set) {
    return config_table[idx].value.str_val;
  }
  
  // Check NVS for persistent value (SLOW PATH - needs buffer)
  if (prefs.isKey(key)) {
    // Get next available buffer from circular pool
    char* buffer = configGetStringBuffer();
    
    // Read from NVS into buffer
    size_t bytes_read = prefs.getString(key, buffer, CONFIG_STRING_BUFFER_SIZE);
    if (bytes_read > 0) {
      buffer[CONFIG_STRING_BUFFER_SIZE - 1] = '\0';
      
      // Cache the value for next time
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
  
  if (!default_val) {
    return "";
  }
  
  return default_val;
}

void configSetString(const char* key, const char* value) {
  if (!key) {
    logError("[CONFIG] ERROR: NULL key in configSetString");
    return;
  }
  
  int idx = findConfigEntry(key);
  
  if (idx < 0) {
    if (config_count >= CONFIG_MAX_KEYS) {
      Serial.println("[CONFIG] ERROR: Config table full");
      return;
    }
    idx = config_count++;
    strncpy(config_table[idx].key, key, CONFIG_KEY_LEN - 1);
    config_table[idx].key[CONFIG_KEY_LEN - 1] = '\0';
    config_table[idx].type = CONFIG_STRING;
  }
  
  // --- FIX: Smart Write Protection (Flash Saver) ---
  if (config_table[idx].is_set && 
      config_table[idx].type == CONFIG_STRING && 
      strncmp(config_table[idx].value.str_val, value, CONFIG_VALUE_LEN) == 0) {
      return; // Value hasn't changed
  }
  // -------------------------------------------------

  // Update in-memory cache
  strncpy(config_table[idx].value.str_val, value, CONFIG_VALUE_LEN - 1);
  config_table[idx].value.str_val[CONFIG_VALUE_LEN - 1] = '\0';
  config_table[idx].is_set = true;
  config_table[idx].type = CONFIG_STRING;
  
  // Mark as dirty for batch write
  config_dirty = true;
  last_nvs_save = millis();
  
  logInfo("[CONFIG] Set %s = %s (cached, will flush with batch)", key, value);
}

// ----------------------------------------------------------------------------
// BATCH FLUSH IMPLEMENTATION (Called by a low-priority task/loop)
// ----------------------------------------------------------------------------

void configUnifiedFlush() {
    if (!config_dirty) {
        return;
    }
    
    if (millis() - last_nvs_save > NVS_CONFIG_SAVE_INTERVAL_MS) {
        configUnifiedSave();
        config_dirty = false;
    }
}

// ----------------------------------------------------------------------------
// NVS WRITE IMPLEMENTATION
// ----------------------------------------------------------------------------

void configUnifiedSave() {
  Serial.print("[CONFIG] Saving ");
  Serial.print(config_count);
  Serial.println(" entries to NVS...");
  
  // Write each entry to NVS
  for (int i = 0; i < config_count; i++) {
    if (!config_table[i].is_set) continue;
    
    // Only save if not already saved via the critical path
    if (config_table[i].type != CONFIG_INT32 || !isCriticalKey(config_table[i].key)) {
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
  }
  
  // Explicitly end NVS session to ensure data is written to flash reliably
  prefs.end(); 
  prefs.begin("bisso_config", false); // Restart session for continued use

  logInfo("[CONFIG] ✅ Saved to NVS (%u free entries remaining)", prefs.freeEntries());
  last_nvs_save = millis();
  config_dirty = false;
}

void configUnifiedLoad() {
  Serial.println("[CONFIG] Loading configuration from NVS...");
  
  Serial.println("[CONFIG] Configuration system ready for on-demand loading");
  
  Serial.println("[CONFIG] ✅ NVS storage ready for on-demand access");
}

void configUnifiedReset() {
  Serial.println("[CONFIG] Resetting to defaults and saving to NVS...");
  
  // --- Reset critical values first (which will auto-save) ---
  configSetInt("x_soft_limit_min", -500000); // 500mm in counts
  configSetInt("x_soft_limit_max", 500000);
  configSetInt("y_soft_limit_min", -300000); // 300mm in counts
  configSetInt("y_soft_limit_max", 300000);
  configSetInt("z_soft_limit_min", -50000);  // 50mm in counts
  configSetInt("z_soft_limit_max", 150000);  // 150mm in counts
  configSetInt("a_soft_limit_min", -45000);  // 45 degrees in counts
  configSetInt("a_soft_limit_max", 45000);

  // --- Reset non-critical values (will be batch-saved by configUnifiedSave()) ---
  configSetFloat("default_speed_mm_s", 15.0f); 
  configSetFloat("default_acceleration", 5.0f);
  
  configSetInt("encoder_ppm_x", 1000); 
  configSetInt("encoder_ppm_y", 1000);
  configSetInt("encoder_ppm_z", 1000);
  configSetInt("encoder_ppm_a", 1000); 
  
  configSetInt("alarm_pin", 2);
  configSetInt("stall_timeout_ms", 2000);
  
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
  Serial.println("[CONFIG] Configuration cache cleared");
}

int configGetKeyCount() {
  return config_count;
}

void configUnifiedDiagnostics() {
  Serial.println("\n[CONFIG] === Configuration Diagnostics ===");
  Serial.print("Total Entries in Cache: ");
  Serial.println(config_count);
  Serial.print("Configuration Dirty: ");
  Serial.println(config_dirty ? "YES" : "NO");
  
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