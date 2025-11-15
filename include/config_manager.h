#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// CONFIGURATION MANAGEMENT TYPES
// ============================================================================

typedef enum {
  CONFIG_PRESET_DEFAULT,           // Factory defaults
  CONFIG_PRESET_PERFORMANCE,       // Optimized for speed
  CONFIG_PRESET_PRECISION,         // Optimized for accuracy
  CONFIG_PRESET_CONSERVATIVE,      // Safe/stable settings
  CONFIG_PRESET_CUSTOM             // User-defined
} config_preset_t;

typedef struct {
  int32_t max_position;            // Maximum safe position
  int32_t min_position;            // Minimum safe position (can be negative)
  uint32_t max_velocity;           // Maximum safe velocity
  uint32_t max_acceleration;       // Maximum safe acceleration
  uint8_t num_axes;                // Number of axes
  uint32_t timeout_ms;             // Operation timeout
  uint8_t retry_count;             // Retry attempts
} config_limits_t;

typedef struct {
  char name[64];                   // Preset name
  char description[128];           // Preset description
  uint32_t timestamp;              // Creation timestamp
  bool valid;                      // Is preset valid
} config_preset_info_t;

// ============================================================================
// CONFIGURATION EXPORT/IMPORT FUNCTIONS
// ============================================================================

/**
 * @brief Export current configuration to JSON format
 * @param buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return Number of bytes written, 0 on error
 */
size_t configExportToJSON(char* buffer, size_t buffer_size);

/**
 * @brief Import configuration from JSON format
 * @param json_string JSON string to parse
 * @return true if import successful, false otherwise
 */
bool configImportFromJSON(const char* json_string);

/**
 * @brief Export configuration to file (SD card or NVS)
 * @param filename Filename to save to
 * @return true if export successful, false otherwise
 */
bool configExportToFile(const char* filename);

/**
 * @brief Import configuration from file (SD card or NVS)
 * @param filename Filename to load from
 * @return true if import successful, false otherwise
 */
bool configImportFromFile(const char* filename);

// ============================================================================
// CONFIGURATION PRESET FUNCTIONS
// ============================================================================

/**
 * @brief Load a preset configuration
 * @param preset Preset type to load
 * @return true if preset loaded successfully, false otherwise
 */
bool configLoadPreset(config_preset_t preset);

/**
 * @brief Save current configuration as a preset
 * @param preset Preset type to save to
 * @param name Name for this preset
 * @return true if preset saved successfully, false otherwise
 */
bool configSaveAsPreset(config_preset_t preset, const char* name);

/**
 * @brief Get information about a preset
 * @param preset Preset type
 * @return Preset information structure
 */
config_preset_info_t configGetPresetInfo(config_preset_t preset);

/**
 * @brief List all available presets
 * @return Number of presets available
 */
uint8_t configListPresets();

/**
 * @brief Delete a preset (cannot delete factory defaults)
 * @param preset Preset to delete
 * @return true if deleted successfully, false otherwise
 */
bool configDeletePreset(config_preset_t preset);

/**
 * @brief Reset to factory defaults
 * @return true if reset successful, false otherwise
 */
bool configResetToDefaults();

// ============================================================================
// CONFIGURATION COMPARISON FUNCTIONS
// ============================================================================

/**
 * @brief Compare current config with a saved configuration
 * @param json_compare JSON string to compare with
 * @param diff_buffer Buffer to store differences
 * @param diff_size Size of diff buffer
 * @return Number of differences found
 */
int configCompareTo(const char* json_compare, char* diff_buffer, size_t diff_size);

/**
 * @brief Get configuration diff in human-readable format
 * @param buffer Output buffer
 * @param buffer_size Size of buffer
 * @return Number of bytes written
 */
size_t configGetDiffSummary(char* buffer, size_t buffer_size);

/**
 * @brief Check if configuration differs from preset
 * @param preset Preset to compare against
 * @return Number of differences found
 */
uint8_t configDiffFromPreset(config_preset_t preset);

// ============================================================================
// CONFIGURATION VALIDATION FUNCTIONS
// ============================================================================

/**
 * @brief Validate entire configuration
 * @param strict true for strict validation, false for lenient
 * @return true if configuration is valid, false otherwise
 */
bool configValidate(bool strict);

/**
 * @brief Validate specific configuration parameter
 * @param key Parameter name
 * @param value Parameter value
 * @return true if parameter is valid, false otherwise
 */
bool configValidateParameter(const char* key, const char* value);

/**
 * @brief Get validation errors for current configuration
 * @param buffer Output buffer for error messages
 * @param buffer_size Size of buffer
 * @return Number of errors found
 */
uint8_t configGetValidationErrors(char* buffer, size_t buffer_size);

/**
 * @brief Get configuration limits
 * @return Configuration limits structure
 */
config_limits_t configGetLimits();

/**
 * @brief Update configuration limits
 * @param limits New limits structure
 * @return true if limits updated successfully, false otherwise
 */
bool configSetLimits(const config_limits_t* limits);

/**
 * @brief Print configuration validation report
 */
void configPrintValidationReport();

#endif
