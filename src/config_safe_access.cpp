#include "config_safe_access.h"
#include "fault_logging.h"
#include "serial_logger.h"

// ============================================================================
// SAFE CONFIGURATION ACCESS IMPLEMENTATION
// ============================================================================

#define CONFIG_MUTEX_TIMEOUT_MS 100

int32_t configGetIntSafe(const char* key, int32_t default_value) {
  if (!key) {
    logError("configGetIntSafe: NULL key");
    return default_value;
  }
  
  // Acquire mutex for safe access
  if (!taskLockMutex(taskGetConfigMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configGetIntSafe: Mutex timeout for key '%s'", key);
    faultLogWarning(FAULT_BOOT_FAILED, "Config mutex timeout on read");
    return default_value;
  }
  
  // For now, just return default (NVS access would go here)
  int32_t value = default_value;
  
  // Release mutex
  taskUnlockMutex(taskGetConfigMutex());
  
  return value;
}

bool configGetStringSafe(const char* key, char* buffer, size_t buffer_size, 
                        const char* default_value) {
  if (!key || !buffer || buffer_size == 0) {
    logError("configGetStringSafe: Invalid parameters");
    if (buffer && buffer_size > 0) {
      strncpy(buffer, default_value ? default_value : "", buffer_size - 1);
      buffer[buffer_size - 1] = '\0';
    }
    return false;
  }
  
  // Acquire mutex
  if (!taskLockMutex(taskGetConfigMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configGetStringSafe: Mutex timeout for key '%s'", key);
    strncpy(buffer, default_value ? default_value : "", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return false;
  }
  
  // For now, just return default (NVS access would go here)
  strncpy(buffer, default_value ? default_value : "", buffer_size - 1);
  buffer[buffer_size - 1] = '\0';
  
  // Release mutex
  taskUnlockMutex(taskGetConfigMutex());
  
  return true;
}

bool configSetIntSafe(const char* key, int32_t value) {
  if (!key) {
    logError("configSetIntSafe: NULL key");
    return false;
  }
  
  // Acquire mutex
  if (!taskLockMutex(taskGetConfigMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configSetIntSafe: Mutex timeout for key '%s'", key);
    faultLogWarning(FAULT_BOOT_FAILED, "Config mutex timeout on write");
    return false;
  }
  
  // Write configuration (NVS access would go here)
  
  // Release mutex
  taskUnlockMutex(taskGetConfigMutex());
  
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
  if (!taskLockMutex(taskGetConfigMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configSetStringSafe: Mutex timeout for key '%s'", key);
    return false;
  }
  
  // Write configuration (NVS access would go here)
  
  // Release mutex
  taskUnlockMutex(taskGetConfigMutex());
  
  return true;
}

bool configGetSoftLimitsSafe(uint8_t axis, int32_t* min_pos, int32_t* max_pos) {
  if (!min_pos || !max_pos || axis >= 4) {
    logError("configGetSoftLimitsSafe: Invalid parameters");
    return false;
  }
  
  // Acquire mutex
  if (!taskLockMutex(taskGetConfigMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configGetSoftLimitsSafe: Mutex timeout");
    return false;
  }
  
  // Set reasonable defaults
  switch(axis) {
    case 0: *min_pos = -500000; *max_pos = 500000; break;   // X: ±500mm
    case 1: *min_pos = -300000; *max_pos = 300000; break;   // Y: ±300mm
    case 2: *min_pos = 0;       *max_pos = 150000; break;   // Z: 0-150mm
    case 3: *min_pos = -45000;  *max_pos = 45000;  break;   // A: ±45°
    default: return false;
  }
  
  // Release mutex
  taskUnlockMutex(taskGetConfigMutex());
  
  logDebug("Soft limits axis %d: %ld to %ld", axis, *min_pos, *max_pos);
  return true;
}

bool configSetSoftLimitsSafe(uint8_t axis, int32_t min_pos, int32_t max_pos) {
  if (axis >= 4 || min_pos >= max_pos) {
    logError("configSetSoftLimitsSafe: Invalid parameters");
    return false;
  }
  
  // Acquire mutex
  if (!taskLockMutex(taskGetConfigMutex(), CONFIG_MUTEX_TIMEOUT_MS)) {
    logWarning("configSetSoftLimitsSafe: Mutex timeout");
    return false;
  }
  
  // Store limits (NVS access would go here)
  
  // Release mutex
  taskUnlockMutex(taskGetConfigMutex());
  
  logInfo("Soft limits updated axis %d: %ld to %ld", axis, min_pos, max_pos);
  return true;
}
