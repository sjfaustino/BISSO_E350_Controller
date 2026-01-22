/**
 * @file api_config.h
 * @brief Configuration API Endpoints for Web Settings Page
 * @project BISSO E350 Controller
 * @details Provides REST API for system configuration management (soft limits, VFD parameters, encoder calibration)
 */

#ifndef API_CONFIG_H
#define API_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ArduinoJson.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration categories
 */
typedef enum {
    CONFIG_CATEGORY_MOTION = 0,         // Motion control (soft limits)
    CONFIG_CATEGORY_VFD = 1,            // VFD parameters (speed, ramps)
    CONFIG_CATEGORY_ENCODER = 2,        // Encoder calibration (PPM)
    CONFIG_CATEGORY_SAFETY = 3,         // Safety thresholds
    CONFIG_CATEGORY_THERMAL = 4,        // Thermal protection
    CONFIG_CATEGORY_NETWORK = 5,        // WiFi/Ethernet settings
    CONFIG_CATEGORY_SYSTEM = 6,         // System settings (CLI, OTA, etc.)
    CONFIG_CATEGORY_SPINDLE = 7,        // JXK10, Tach, Pause
    CONFIG_CATEGORY_SERIAL = 8,         // Baud rates, I2C speed
    CONFIG_CATEGORY_HARDWARE = 9,       // Pin mapping
    CONFIG_CATEGORY_BEHAVIOR = 10,      // Jog speed, Accel, Margins
    CONFIG_CATEGORY_CALIBRATION = 11,   // Speed calibration
    CONFIG_CATEGORY_POSITIONS = 12,     // Safe/User positions
    CONFIG_CATEGORY_WCS = 13,           // Work Coordinate Systems
    CONFIG_CATEGORY_SECURITY = 14,      // Credentials
    CONFIG_CATEGORY_STATS = 15          // Runtime, Cycle count
} config_category_t;

/**
 * @brief Motion configuration structure
 * @note PHASE 5.10: Changed from uint16_t to int32_t to support negative coordinates
 */
typedef struct {
    int32_t soft_limit_low_mm[3];       // Lower limit for X, Y, Z (mm) - supports negative
    int32_t soft_limit_high_mm[3];      // Upper limit for X, Y, Z (mm)
    int32_t x_approach_slow_mm;         // X Slow approach threshold (mm)
    int32_t x_approach_med_mm;          // X Medium approach threshold (mm)
    float target_margin_mm;             // Target position margin (mm)
} motion_config_t;

/**
 * @brief VFD configuration structure
 */
typedef struct {
    uint16_t min_speed_hz;              // Minimum speed (LSP)
    uint16_t max_speed_hz;              // Maximum speed (HSP)
    uint16_t acc_time_ms;               // Acceleration time
    uint16_t dec_time_ms;               // Deceleration time
} vfd_config_t;

/**
 * @brief Encoder configuration structure
 */
typedef struct {
    uint16_t ppm[3];                    // Pulses per mm for X, Y, Z
    uint8_t calibrated[3];              // Calibration status per axis
} encoder_config_t;

/**
 * @brief Initialize configuration API
 */
void apiConfigInit(void);

/**
 * @brief Get current configuration as JSON
 * @param category Configuration category to retrieve
 * @param json_doc Output JSON document
 * @return true if successful
 */
bool apiConfigGet(config_category_t category, JsonVariant json_doc);

/**
 * @brief Set configuration value with validation
 * @param category Configuration category
 * @param key Configuration key
 * @param value Configuration value (as JSON)
 * @return true if valid and accepted
 */
bool apiConfigSet(config_category_t category, const char* key, JsonVariant value);

/**
 * @brief Validate configuration change before applying
 * @param category Configuration category
 * @param key Configuration key
 * @param value Proposed value
 * @param error_msg Output buffer for error message
 * @param error_msg_len Size of error buffer
 * @return true if validation passed
 */
bool apiConfigValidate(config_category_t category, const char* key, JsonVariant value,
                       char* error_msg, size_t error_msg_len);

/**
 * @brief Get configuration schema (for client-side validation hints)
 * @param category Configuration category
 * @param json_doc Output JSON document with schema
 * @return true if successful
 */
bool apiConfigGetSchema(config_category_t category, JsonVariant json_doc);

/**
 * @brief Trigger encoder calibration for specified axis
 * @param axis Axis to calibrate (0=X, 1=Y, 2=Z)
 * @param ppm Calibration value (pulses per millimeter)
 * @return true if calibration started
 */
bool apiConfigCalibrateEncoder(uint8_t axis, uint16_t ppm);

/**
 * @brief Save all configuration to persistent storage (NVS)
 * @return true if save successful
 */
bool apiConfigSave(void);

/**
 * @brief Load all configuration from persistent storage (NVS)
 * @return true if load successful
 */
bool apiConfigLoad(void);

/**
 * @brief Reset configuration to defaults
 * @return true if reset successful
 */
bool apiConfigReset(void);

/**
 * @brief Export configuration as JSON
 * @param buffer Output buffer
 * @param buffer_size Maximum size
 * @return Bytes written
 */
size_t apiConfigExportJSON(char* buffer, size_t buffer_size);

/**
 * @brief Import configuration from JSON
 * @param doc JSON document containing configuration
 * @return true if import successful
 */
bool apiConfigImportJSON(const JsonVariant& doc);

#ifdef __cplusplus
/**
 * @brief Populate a JsonDocument with the full system configuration.
 * @param doc JSON document to populate.
 */
void apiConfigPopulate(JsonDocument& doc);
#endif

/**
 * @brief Print configuration to serial console
 */
void apiConfigPrint(void);

#ifdef __cplusplus
}
#endif

#endif // API_CONFIG_H
