#ifndef CONFIG_SAFE_ACCESS_H
#define CONFIG_SAFE_ACCESS_H

#include <Arduino.h>
#include "config_manager.h"
#include "task_manager.h"

// ============================================================================
// SAFE CONFIGURATION ACCESS WITH MUTEX PROTECTION
// ============================================================================

/**
 * Safe wrapper for reading configuration with mutex protection
 * * @param key Configuration key
 * @param default_value Default value if not found
 * @return Configuration value or default
 * * Thread-safe with timeout-based mutex acquisition
 */
int32_t configGetIntSafe(const char* key, int32_t default_value);

/**
 * Safe wrapper for reading string configuration
 * * @param key Configuration key
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param default_value Default value if not found
 * @return true if successful, false if timeout/error
 */
bool configGetStringSafe(const char* key, char* buffer, size_t buffer_size, 
                        const char* default_value);

/**
 * Safe wrapper for writing integer configuration
 * * @param key Configuration key
 * @param value Value to write
 * @return true if successful, false if timeout/error
 */
bool configSetIntSafe(const char* key, int32_t value);

/**
 * Safe wrapper for writing string configuration
 * * @param key Configuration key
 * @param value Value to write
 * @return true if successful, false if timeout/error
 */
bool configSetStringSafe(const char* key, const char* value);

/**
 * Safe wrapper for reading soft limits
 * * @param axis Axis index (0-3)
 * @param min_pos Pointer to store minimum position
 * @param max_pos Pointer to store maximum position
 * @return true if successful, false if timeout/error
 */
bool configGetSoftLimitsSafe(uint8_t axis, int32_t* min_pos, int32_t* max_pos);

/**
 * Safe wrapper for writing soft limits
 * * @param axis Axis index (0-3)
 * @param min_pos Minimum position
 * @param max_pos Maximum position
 * @return true if successful, false if timeout/error
 */
bool configSetSoftLimitsSafe(uint8_t axis, int32_t min_pos, int32_t max_pos);

#endif // CONFIG_SAFE_ACCESS_H
