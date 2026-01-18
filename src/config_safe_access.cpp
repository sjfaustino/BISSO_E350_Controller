#include "config_safe_access.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "config_unified.h" // Needed to access the underlying config getters/setters
#include "string_safety.h"  // For safe string operations

// ============================================================================
// SAFE CONFIGURATION ACCESS IMPLEMENTATION
// ============================================================================

#define CONFIG_MUTEX_TIMEOUT_MS 100

int32_t configGetIntSafe(const char* key, int32_t default_value) {
  if (!key) {
    logError("configGetIntSafe: NULL key");
    return default_value;
  }
  
  int32_t value = default_value;
  
  // Acquire mutex for safe access
  if (!taskLockMutex((SemaphoreHandle_t)configGetMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configGetIntSafe: Mutex timeout for key '%s'", key);
    faultLogWarning(FAULT_BOOT_FAILED, "Config mutex timeout on read");
    return default_value;
  }
  
  // *** FIX: Retrieve the actual value from the unified configuration cache/NVS ***
  value = configGetInt(key, default_value);
  
  // Release mutex
  taskUnlockMutex((SemaphoreHandle_t)configGetMutex());
  
  return value;
}

bool configGetStringSafe(const char* key, char* buffer, size_t buffer_size, 
                        const char* default_value) {
  if (!key || !buffer || buffer_size == 0) {
    logError("configGetStringSafe: Invalid parameters");
    if (buffer && buffer_size > 0) {
      safe_strcpy(buffer, buffer_size, default_value ? default_value : "");
    }
    return false;
  }
  
  const char* result_ptr = default_value;
  
  // Acquire mutex
  if (!taskLockMutex((SemaphoreHandle_t)configGetMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configGetStringSafe: Mutex timeout for key '%s'", key);
    safe_strcpy(buffer, buffer_size, default_value ? default_value : "");
    return false;
  }
  
  // *** FIX: Retrieve the actual string pointer from the unified configuration ***
  result_ptr = configGetString(key, default_value);
  
  // Copy to the user's buffer while still holding the lock (configGetString uses a rotating pool)
  safe_strcpy(buffer, buffer_size, result_ptr);
  
  // Release mutex
  taskUnlockMutex((SemaphoreHandle_t)configGetMutex());
  
  return true;
}

bool configSetIntSafe(const char* key, int32_t value) {
  if (!key) {
    logError("configSetIntSafe: NULL key");
    return false;
  }
  
  // Acquire mutex
  if (!taskLockMutex((SemaphoreHandle_t)configGetMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configSetIntSafe: Mutex timeout for key '%s'", key);
    faultLogWarning(FAULT_BOOT_FAILED, "Config mutex timeout on write");
    return false;
  }
  
  // *** FIX: Write configuration to the unified configuration layer ***
  configSetInt(key, value);
  
  // Release mutex
  taskUnlockMutex((SemaphoreHandle_t)configGetMutex());
  
  return true;
}

bool configSetStringSafe(const char* key, const char* value) {
  if (!key) {
    logError("configSetStringSafe: NULL key");
    return false;
  }
  
  if (!value) {
    logWarning("configSetStringSafe: NULL value for key '%s'", key);
    return false;
  }
  
  // Acquire mutex
  if (!taskLockMutex((SemaphoreHandle_t)configGetMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configSetStringSafe: Mutex timeout for key '%s'", key);
    return false;
  }
  
  // *** FIX: Write configuration to the unified configuration layer ***
  configSetString(key, value);
  
  // Release mutex
  taskUnlockMutex((SemaphoreHandle_t)configGetMutex());
  
  return true;
}

// NOTE: Soft Limits access is handled directly by the motion module in the PLC model.
// These functions are kept here as a fallback/interface for the config layer, 
// using the simple setters/getters for NVS access via the unified config layer.

bool configGetSoftLimitsSafe(uint8_t axis, int32_t* min_pos, int32_t* max_pos) {
  if (!min_pos || !max_pos || axis >= 4) {
    logError("configGetSoftLimitsSafe: Invalid parameters");
    return false;
  }
  
  // Note: We skip the mutex here because configGetIntSafe already wraps it. 
  // However, for consistency, let's wrap the logic or call the underlying NVS keys directly.

  const char* min_keys[] = {"x_soft_limit_min", "y_soft_limit_min", "z_soft_limit_min", "a_soft_limit_min"};
  const char* max_keys[] = {"x_soft_limit_max", "y_soft_limit_max", "z_soft_limit_max", "a_soft_limit_max"};

  // Retrieve values safely (configGetIntSafe handles its own mutex)
  *min_pos = configGetIntSafe(min_keys[axis], -500000); 
  *max_pos = configGetIntSafe(max_keys[axis], 500000); 
  
  logDebug("Soft limits axis %d: %ld to %ld", axis, *min_pos, *max_pos);
  return true;
}

bool configSetSoftLimitsSafe(uint8_t axis, int32_t min_pos, int32_t max_pos) {
  if (axis >= 4 || min_pos >= max_pos) {
    logError("configSetSoftLimitsSafe: Invalid parameters");
    return false;
  }
  
  const char* min_keys[] = {"x_soft_limit_min", "y_soft_limit_min", "z_soft_limit_min", "a_soft_limit_min"};
  const char* max_keys[] = {"x_soft_limit_max", "y_soft_limit_max", "z_soft_limit_max", "a_soft_limit_max"};

  // Set values safely (configSetIntSafe handles its own mutex and persistence)
  bool success_min = configSetIntSafe(min_keys[axis], min_pos);
  bool success_max = configSetIntSafe(max_keys[axis], max_pos);
  
  if (success_min && success_max) {
      logInfo("Soft limits updated axis %d: %ld to %ld", axis, min_pos, max_pos);
      return true;
  }
  return false;
}
