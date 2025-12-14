/**
 * @file api_config.cpp
 * @brief Configuration API Implementation
 * @project BISSO E350 Controller
 */

#include "api_config.h"
#include "config_unified.h"
#include "config_keys.h"
#include "string_safety.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// Configuration defaults (matching Altivar 31 + system constraints)
static const motion_config_t default_motion = {
    .soft_limit_low_mm = {0, 0, 0},
    .soft_limit_high_mm = {500, 500, 500}
};

static const vfd_config_t default_vfd = {
    .min_speed_hz = 1,      // LSP (Altivar 31 minimum)
    .max_speed_hz = 105,    // HSP (Altivar 31 maximum)
    .acc_time_ms = 600,     // 0.6 seconds
    .dec_time_ms = 400      // 0.4 seconds
};

static const encoder_config_t default_encoder = {
    .ppm = {100, 100, 100},
    .calibrated = {0, 0, 0}
};

// Current configuration in RAM
static motion_config_t current_motion;
static vfd_config_t current_vfd;
static encoder_config_t current_encoder;

/**
 * @brief Initialize API
 */
void apiConfigInit(void)
{
    logInfo("[API_CONFIG] Initializing");
    apiConfigLoad();
    logInfo("[API_CONFIG] Ready");
}

/**
 * @brief Load configuration from NVS
 */
bool apiConfigLoad(void)
{
    // Load motion config
    current_motion.soft_limit_low_mm[0] = configGetInt(KEY_X_LIMIT_MIN, 0);
    current_motion.soft_limit_high_mm[0] = configGetInt(KEY_X_LIMIT_MAX, 500);
    current_motion.soft_limit_low_mm[1] = configGetInt(KEY_Y_LIMIT_MIN, 0);
    current_motion.soft_limit_high_mm[1] = configGetInt(KEY_Y_LIMIT_MAX, 500);
    current_motion.soft_limit_low_mm[2] = configGetInt(KEY_Z_LIMIT_MIN, 0);
    current_motion.soft_limit_high_mm[2] = configGetInt(KEY_Z_LIMIT_MAX, 500);

    // Load VFD config (using idle RMS as proxy for min speed characteristics)
    // Note: VFD has fixed acceleration/deceleration timings via Modbus, not configurable here
    current_vfd.min_speed_hz = 1;      // Altivar31 minimum (LSP)
    current_vfd.max_speed_hz = 105;    // Altivar31 maximum (HSP)
    current_vfd.acc_time_ms = 600;     // Fixed in VFD configuration
    current_vfd.dec_time_ms = 400;     // Fixed in VFD configuration

    // Load encoder config
    current_encoder.ppm[0] = configGetInt(KEY_PPM_X, 100);
    current_encoder.ppm[1] = configGetInt(KEY_PPM_Y, 100);
    current_encoder.ppm[2] = configGetInt(KEY_PPM_Z, 100);
    // Calibration flags are runtime-only, not persisted
    current_encoder.calibrated[0] = 0;
    current_encoder.calibrated[1] = 0;
    current_encoder.calibrated[2] = 0;

    logInfo("[API_CONFIG] Configuration loaded from NVS");
    return true;
}

/**
 * @brief Save configuration to NVS
 */
bool apiConfigSave(void)
{
    // Save motion config
    configSetInt(KEY_X_LIMIT_MIN, current_motion.soft_limit_low_mm[0]);
    configSetInt(KEY_X_LIMIT_MAX, current_motion.soft_limit_high_mm[0]);
    configSetInt(KEY_Y_LIMIT_MIN, current_motion.soft_limit_low_mm[1]);
    configSetInt(KEY_Y_LIMIT_MAX, current_motion.soft_limit_high_mm[1]);
    configSetInt(KEY_Z_LIMIT_MIN, current_motion.soft_limit_low_mm[2]);
    configSetInt(KEY_Z_LIMIT_MAX, current_motion.soft_limit_high_mm[2]);

    // VFD config - speeds and timings are fixed in Altivar31 and set via Modbus
    // No need to save these as they are hardware constants

    // Save encoder config (PPM only, calibration flags are runtime-only)
    configSetInt(KEY_PPM_X, current_encoder.ppm[0]);
    configSetInt(KEY_PPM_Y, current_encoder.ppm[1]);
    configSetInt(KEY_PPM_Z, current_encoder.ppm[2]);

    logInfo("[API_CONFIG] Configuration saved to NVS");
    return true;
}

/**
 * @brief Reset configuration to defaults
 */
bool apiConfigReset(void)
{
    current_motion = default_motion;
    current_vfd = default_vfd;
    current_encoder = default_encoder;

    logWarning("[API_CONFIG] Configuration reset to defaults");
    return apiConfigSave();
}

/**
 * @brief Validate soft limit configuration
 */
static bool validateSoftLimit(const char* key, JsonVariant value, char* error_msg, size_t error_msg_len)
{
    if (!value.is<uint16_t>()) {
        snprintf(error_msg, error_msg_len, "Soft limit must be numeric (0-1000 mm)");
        return false;
    }

    uint16_t val = value.as<uint16_t>();
    if (val > 1000) {
        snprintf(error_msg, error_msg_len, "Soft limit cannot exceed 1000 mm");
        return false;
    }

    return true;
}

/**
 * @brief Validate VFD speed configuration
 */
static bool validateVfdSpeed(const char* key, JsonVariant value, char* error_msg, size_t error_msg_len)
{
    if (!value.is<uint16_t>()) {
        snprintf(error_msg, error_msg_len, "Speed must be numeric (1-105 Hz)");
        return false;
    }

    uint16_t val = value.as<uint16_t>();
    if (val < 1 || val > 105) {
        snprintf(error_msg, error_msg_len, "Speed must be between 1 and 105 Hz");
        return false;
    }

    return true;
}

/**
 * @brief Validate encoder PPM
 */
static bool validateEncoderPpm(const char* key, JsonVariant value, char* error_msg, size_t error_msg_len)
{
    if (!value.is<uint16_t>()) {
        snprintf(error_msg, error_msg_len, "PPM must be numeric (50-200)");
        return false;
    }

    uint16_t val = value.as<uint16_t>();
    if (val < 50 || val > 200) {
        snprintf(error_msg, error_msg_len, "PPM must be between 50 and 200");
        return false;
    }

    return true;
}

/**
 * @brief Validate configuration change
 */
bool apiConfigValidate(config_category_t category, const char* key, JsonVariant value,
                       char* error_msg, size_t error_msg_len)
{
    if (!key || !error_msg) return false;

    memset(error_msg, 0, error_msg_len);

    switch (category) {
        case CONFIG_CATEGORY_MOTION:
            if (strstr(key, "soft_limit")) {
                return validateSoftLimit(key, value, error_msg, error_msg_len);
            }
            break;

        case CONFIG_CATEGORY_VFD:
            if (strstr(key, "speed_hz")) {
                return validateVfdSpeed(key, value, error_msg, error_msg_len);
            }
            break;

        case CONFIG_CATEGORY_ENCODER:
            if (strstr(key, "ppm")) {
                return validateEncoderPpm(key, value, error_msg, error_msg_len);
            }
            break;

        default:
            snprintf(error_msg, error_msg_len, "Unknown configuration category");
            return false;
    }

    return true;
}

/**
 * @brief Set configuration value
 */
bool apiConfigSet(config_category_t category, const char* key, JsonVariant value)
{
    char error_msg[256];

    // Validate first
    if (!apiConfigValidate(category, key, value, error_msg, sizeof(error_msg))) {
        logWarning("[API_CONFIG] Validation failed for %s: %s", key, error_msg);
        return false;
    }

    // Apply based on category
    switch (category) {
        case CONFIG_CATEGORY_MOTION:
            if (strcmp(key, "soft_limit_x_low") == 0) {
                current_motion.soft_limit_low_mm[0] = value.as<uint16_t>();
            } else if (strcmp(key, "soft_limit_x_high") == 0) {
                current_motion.soft_limit_high_mm[0] = value.as<uint16_t>();
            } else if (strcmp(key, "soft_limit_y_low") == 0) {
                current_motion.soft_limit_low_mm[1] = value.as<uint16_t>();
            } else if (strcmp(key, "soft_limit_y_high") == 0) {
                current_motion.soft_limit_high_mm[1] = value.as<uint16_t>();
            } else if (strcmp(key, "soft_limit_z_low") == 0) {
                current_motion.soft_limit_low_mm[2] = value.as<uint16_t>();
            } else if (strcmp(key, "soft_limit_z_high") == 0) {
                current_motion.soft_limit_high_mm[2] = value.as<uint16_t>();
            }
            break;

        case CONFIG_CATEGORY_VFD:
            if (strcmp(key, "min_speed_hz") == 0) {
                current_vfd.min_speed_hz = value.as<uint16_t>();
            } else if (strcmp(key, "max_speed_hz") == 0) {
                current_vfd.max_speed_hz = value.as<uint16_t>();
            } else if (strcmp(key, "acc_time_ms") == 0) {
                current_vfd.acc_time_ms = value.as<uint16_t>();
            } else if (strcmp(key, "dec_time_ms") == 0) {
                current_vfd.dec_time_ms = value.as<uint16_t>();
            }
            break;

        case CONFIG_CATEGORY_ENCODER:
            if (strstr(key, "ppm_x")) {
                current_encoder.ppm[0] = value.as<uint16_t>();
            } else if (strstr(key, "ppm_y")) {
                current_encoder.ppm[1] = value.as<uint16_t>();
            } else if (strstr(key, "ppm_z")) {
                current_encoder.ppm[2] = value.as<uint16_t>();
            }
            break;

        default:
            return false;
    }

    logInfo("[API_CONFIG] Configuration updated: %s", key);
    return true;
}

/**
 * @brief Get configuration as JSON
 */
bool apiConfigGet(config_category_t category, JsonDocument& json_doc)
{
    JsonObject obj = json_doc.to<JsonObject>();

    switch (category) {
        case CONFIG_CATEGORY_MOTION: {
            obj["soft_limit_x_low"] = current_motion.soft_limit_low_mm[0];
            obj["soft_limit_x_high"] = current_motion.soft_limit_high_mm[0];
            obj["soft_limit_y_low"] = current_motion.soft_limit_low_mm[1];
            obj["soft_limit_y_high"] = current_motion.soft_limit_high_mm[1];
            obj["soft_limit_z_low"] = current_motion.soft_limit_low_mm[2];
            obj["soft_limit_z_high"] = current_motion.soft_limit_high_mm[2];
            break;
        }

        case CONFIG_CATEGORY_VFD: {
            obj["min_speed_hz"] = current_vfd.min_speed_hz;
            obj["max_speed_hz"] = current_vfd.max_speed_hz;
            obj["acc_time_ms"] = current_vfd.acc_time_ms;
            obj["dec_time_ms"] = current_vfd.dec_time_ms;
            break;
        }

        case CONFIG_CATEGORY_ENCODER: {
            JsonArray ppm = obj.createNestedArray("ppm");
            ppm.add(current_encoder.ppm[0]);
            ppm.add(current_encoder.ppm[1]);
            ppm.add(current_encoder.ppm[2]);
            JsonArray cal = obj.createNestedArray("calibrated");
            cal.add(current_encoder.calibrated[0]);
            cal.add(current_encoder.calibrated[1]);
            cal.add(current_encoder.calibrated[2]);
            break;
        }

        default:
            return false;
    }

    return true;
}

/**
 * @brief Get configuration schema
 */
bool apiConfigGetSchema(config_category_t category, JsonDocument& json_doc)
{
    JsonObject obj = json_doc.to<JsonObject>();

    switch (category) {
        case CONFIG_CATEGORY_MOTION: {
            obj["soft_limit_x_low"]["type"] = "integer";
            obj["soft_limit_x_low"]["min"] = 0;
            obj["soft_limit_x_low"]["max"] = 1000;
            obj["soft_limit_x_low"]["unit"] = "mm";
            // Similar for other axes...
            break;
        }

        case CONFIG_CATEGORY_VFD: {
            obj["min_speed_hz"]["type"] = "integer";
            obj["min_speed_hz"]["min"] = 1;
            obj["min_speed_hz"]["max"] = 105;
            obj["max_speed_hz"]["type"] = "integer";
            obj["max_speed_hz"]["min"] = 1;
            obj["max_speed_hz"]["max"] = 105;
            break;
        }

        case CONFIG_CATEGORY_ENCODER: {
            obj["ppm"]["type"] = "array";
            obj["ppm"]["element_type"] = "integer";
            obj["ppm"]["min"] = 50;
            obj["ppm"]["max"] = 200;
            break;
        }

        default:
            return false;
    }

    return true;
}

/**
 * @brief Trigger encoder calibration
 */
bool apiConfigCalibrateEncoder(uint8_t axis, uint16_t ppm)
{
    if (axis > 2) return false;
    if (ppm < 50 || ppm > 200) return false;

    current_encoder.ppm[axis] = ppm;
    current_encoder.calibrated[axis] = 1;

    logInfo("[API_CONFIG] Encoder %c calibrated: %u PPM", 'X' + axis, ppm);
    return true;
}

/**
 * @brief Export configuration as JSON
 */
size_t apiConfigExportJSON(char* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 256) return 0;

    StaticJsonDocument<1024> doc;

    JsonObject motion = doc.createNestedObject("motion");
    motion["soft_limit_x_low"] = current_motion.soft_limit_low_mm[0];
    motion["soft_limit_x_high"] = current_motion.soft_limit_high_mm[0];

    JsonObject vfd = doc.createNestedObject("vfd");
    vfd["min_speed_hz"] = current_vfd.min_speed_hz;
    vfd["max_speed_hz"] = current_vfd.max_speed_hz;

    return serializeJson(doc, buffer, buffer_size);
}

/**
 * @brief Print configuration to serial
 */
void apiConfigPrint(void)
{
    logInfo("[API_CONFIG] ============ Configuration Summary ============");
    logInfo("[API_CONFIG] Motion: X[%u-%u] Y[%u-%u] Z[%u-%u] mm",
        current_motion.soft_limit_low_mm[0], current_motion.soft_limit_high_mm[0],
        current_motion.soft_limit_low_mm[1], current_motion.soft_limit_high_mm[1],
        current_motion.soft_limit_low_mm[2], current_motion.soft_limit_high_mm[2]);
    logInfo("[API_CONFIG] VFD: %u-%u Hz, ACC=%ums, DEC=%ums",
        current_vfd.min_speed_hz, current_vfd.max_speed_hz,
        current_vfd.acc_time_ms, current_vfd.dec_time_ms);
    logInfo("[API_CONFIG] Encoder: X=%u Y=%u Z=%u PPM",
        current_encoder.ppm[0], current_encoder.ppm[1], current_encoder.ppm[2]);
    logInfo("[API_CONFIG] ================================================");
}
