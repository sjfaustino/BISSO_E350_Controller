/**
 * @file api_config.cpp
 * @brief Configuration API Implementation
 * @project BISSO E350 Controller
 */

#include "api_config.h"
#include "config_keys.h"
#include "config_unified.h"
#include "serial_logger.h"
#include "string_safety.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include "hardware_config.h"
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "firmware_version.h"
#include <SD.h>
#include "sd_card_manager.h"

static bool validateBool(JsonVariant value, char* error_msg, size_t len);

// ============================================================================
// P2 DRY: Table-driven config field descriptors for import/export
// ============================================================================

typedef enum {
  CFG_INT,
  CFG_FLOAT,
  CFG_STRING
} config_field_type_t;

typedef struct {
  const char* json_key;    // Key in JSON object
  const char* config_key;  // Key in config system (NVS)
  config_field_type_t type;
} config_field_t;

/**
 * @brief Import fields from JSON object using field descriptors
 * @param category The config category for validation
 * @param obj Source JSON object
 * @param fields Array of field descriptors
 * @param count Number of field descriptors
 */
static void importFields(config_category_t category, JsonObject& obj, const config_field_t* fields, size_t count) {
  char error_msg[64];
  for (size_t i = 0; i < count; i++) {
    if (obj.containsKey(fields[i].json_key)) {
      JsonVariant val = obj[fields[i].json_key];
      if (apiConfigValidate(category, fields[i].json_key, val, error_msg, sizeof(error_msg))) {
        switch (fields[i].type) {
          case CFG_INT: configSetInt(fields[i].config_key, val.as<int>()); break;
          case CFG_FLOAT: configSetFloat(fields[i].config_key, val.as<float>()); break;
          case CFG_STRING: configSetString(fields[i].config_key, val.as<const char*>()); break;
        }
      } else {
        logWarning("[API_CONFIG] JSON Import Validation Failed for %s: %s", fields[i].json_key, error_msg);
      }
    }
  }
}

// Field descriptor tables for each config section
static const config_field_t motion_fields[] = {
  {"soft_limit_x_low", KEY_X_LIMIT_MIN, CFG_INT},
  {"soft_limit_x_high", KEY_X_LIMIT_MAX, CFG_INT},
  {"soft_limit_y_low", KEY_Y_LIMIT_MIN, CFG_INT},
  {"soft_limit_y_high", KEY_Y_LIMIT_MAX, CFG_INT},
  {"soft_limit_z_low", KEY_Z_LIMIT_MIN, CFG_INT},
  {"soft_limit_z_high", KEY_Z_LIMIT_MAX, CFG_INT},
  {"home_enable", KEY_HOME_ENABLE, CFG_INT},
  {"home_fast", KEY_HOME_PROFILE_FAST, CFG_INT},
  {"home_slow", KEY_HOME_PROFILE_SLOW, CFG_INT},
};

static const config_field_t vfd_fields[] = {
  {"enabled", KEY_VFD_EN, CFG_INT},
  {"address", KEY_VFD_ADDR, CFG_INT},
};

static const config_field_t encoder_fields[] = {
  {"ppm_x", KEY_PPM_X, CFG_INT},
  {"ppm_y", KEY_PPM_Y, CFG_INT},
  {"ppm_z", KEY_PPM_Z, CFG_INT},
  {"feedback_en", KEY_ENC_FEEDBACK, CFG_INT},
};

static const config_field_t network_int_fields[] = {
  {"wifi_ap_en", KEY_WIFI_AP_EN, CFG_INT},
  {"eth_en", KEY_ETH_ENABLED, CFG_INT},
  {"eth_dhcp", KEY_ETH_DHCP, CFG_INT},
};

static const config_field_t network_str_fields[] = {
  {"wifi_ssid", KEY_WIFI_SSID, CFG_STRING},
  {"wifi_pass", KEY_WIFI_PASS, CFG_STRING},
  {"wifi_ap_ssid", KEY_WIFI_AP_SSID, CFG_STRING},
  {"wifi_ap_pass", KEY_WIFI_AP_PASS, CFG_STRING},
};

static const config_field_t system_fields[] = {
  {"buzzer_en", KEY_BUZZER_EN, CFG_INT},
  {"status_light_en", KEY_STATUS_LIGHT_EN, CFG_INT},
  {"status_light_green", KEY_STATUS_LIGHT_GREEN, CFG_INT},
  {"status_light_yellow", KEY_STATUS_LIGHT_YELLOW, CFG_INT},
  {"status_light_red", KEY_STATUS_LIGHT_RED, CFG_INT},
  {"recovery_en", KEY_RECOV_EN, CFG_INT},
  {"lcd_en", KEY_LCD_EN, CFG_INT},
  {"bootlog_en", KEY_BOOTLOG_EN, CFG_INT},
  {"cli_echo", KEY_CLI_ECHO, CFG_INT},
  {"ota_chk_en", KEY_OTA_CHECK_EN, CFG_INT},
  {"buzzer_pin", KEY_BUZZER_PIN, CFG_INT},
};

static const config_field_t spindle_fields[] = {
  {"jxk10_en", KEY_JXK10_ENABLED, CFG_INT},
  {"jxk10_addr", KEY_JXK10_ADDR, CFG_INT},
  {"yhtc05_en", KEY_YHTC05_ENABLED, CFG_INT},
  {"yhtc05_addr", KEY_YHTC05_ADDR, CFG_INT},
  {"pause_en", KEY_SPINDL_PAUSE_EN, CFG_INT},
};

static const config_field_t serial_fields[] = {
  {"encoder_baud", KEY_ENC_BAUD, CFG_INT},
  {"encoder_iface", KEY_ENC_INTERFACE, CFG_INT},
  {"encoder_addr", KEY_ENC_ADDR, CFG_INT},
  {"enc_proto", KEY_ENC_PROTO, CFG_INT},
  {"rs485_baud", KEY_RS485_BAUD, CFG_INT},
  {"i2c_speed", KEY_I2C_SPEED, CFG_INT},
};

static const config_field_t behavior_int_fields[] = {
  {"jog_speed", KEY_DEFAULT_SPEED, CFG_INT},
  {"jog_accel", KEY_DEFAULT_ACCEL, CFG_INT},
  {"x_approach", KEY_X_APPROACH, CFG_INT},
  {"x_approach_med", KEY_X_APPROACH_MED, CFG_INT},
  {"buf_en", KEY_MOTION_BUFFER_ENABLE, CFG_INT},
  {"strict_limits", KEY_MOTION_STRICT_LIMITS, CFG_INT},
  {"stop_timeout", KEY_STOP_TIMEOUT, CFG_INT},
  {"stall_timeout", KEY_STALL_TIMEOUT, CFG_INT},
  {"buttons_en", KEY_BUTTONS_ENABLED, CFG_INT},
};

static const config_field_t behavior_float_fields[] = {
  {"target_margin", KEY_TARGET_MARGIN, CFG_FLOAT},
};

static const config_field_t calibration_fields[] = {
  {"spd_x", KEY_SPEED_CAL_X, CFG_FLOAT},
  {"spd_y", KEY_SPEED_CAL_Y, CFG_FLOAT},
  {"spd_z", KEY_SPEED_CAL_Z, CFG_FLOAT},
  {"spd_a", KEY_SPEED_CAL_A, CFG_FLOAT},
};

static const config_field_t positions_fields[] = {
  {"safe_x", KEY_POS_SAFE_X, CFG_FLOAT},
  {"safe_y", KEY_POS_SAFE_Y, CFG_FLOAT},
  {"safe_z", KEY_POS_SAFE_Z, CFG_FLOAT},
  {"safe_a", KEY_POS_SAFE_A, CFG_FLOAT},
  {"p1_x", KEY_POS_1_X, CFG_FLOAT},
  {"p1_y", KEY_POS_1_Y, CFG_FLOAT},
  {"p1_z", KEY_POS_1_Z, CFG_FLOAT},
  {"p1_a", KEY_POS_1_A, CFG_FLOAT},
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))


// Configuration defaults (matching Altivar 31 + system constraints)
static const motion_config_t default_motion = {
    .soft_limit_low_mm = {0, 0, 0},
    .soft_limit_high_mm = {500, 500, 500},
    .x_approach_slow_mm = 5,
    .x_approach_med_mm = 20,
    .target_margin_mm = 0.1f};

static const encoder_config_t default_encoder = {.ppm = {100, 100, 100},
                                                 .calibrated = {0, 0, 0}};

// Current configuration in RAM
static motion_config_t current_motion;
static encoder_config_t current_encoder;
static SemaphoreHandle_t config_mutex = NULL;

/**
 * @brief Initialize API
 */
void apiConfigInit(void) {
  if (config_mutex == NULL) {
      config_mutex = xSemaphoreCreateMutex();
  }
  logInfo("[API_CONFIG] Initializing");
  apiConfigLoad();
  logInfo("[API_CONFIG] Ready");
}

/**
 * @brief Load configuration from NVS
 */
bool apiConfigLoad(void) {
  if (config_mutex) xSemaphoreTake(config_mutex, portMAX_DELAY);
  // Load motion config
  current_motion.soft_limit_low_mm[0] = configGetInt(KEY_X_LIMIT_MIN, 0);
  current_motion.soft_limit_high_mm[0] = configGetInt(KEY_X_LIMIT_MAX, 500);
  current_motion.soft_limit_low_mm[1] = configGetInt(KEY_Y_LIMIT_MIN, 0);
  current_motion.soft_limit_high_mm[1] = configGetInt(KEY_Y_LIMIT_MAX, 500);
  current_motion.soft_limit_low_mm[2] = configGetInt(KEY_Z_LIMIT_MIN, 0);
  current_motion.soft_limit_high_mm[2] = configGetInt(KEY_Z_LIMIT_MAX, 500);

  // Load Approach/Tuning params
  current_motion.x_approach_slow_mm = configGetInt(KEY_X_APPROACH, 5);
  current_motion.x_approach_med_mm = configGetInt(KEY_X_APPROACH_MED, 20);
  current_motion.target_margin_mm = configGetFloat(KEY_TARGET_MARGIN, 0.1f);

  // Load encoder config
  current_encoder.ppm[0] = configGetInt(KEY_PPM_X, 100);
  current_encoder.ppm[1] = configGetInt(KEY_PPM_Y, 100);
  current_encoder.ppm[2] = configGetInt(KEY_PPM_Z, 100);
  // Calibration flags are runtime-only, not persisted
  current_encoder.calibrated[0] = 0;
  current_encoder.calibrated[1] = 0;
  current_encoder.calibrated[2] = 0;

  if (config_mutex) xSemaphoreGive(config_mutex);
  logInfo("[API_CONFIG] Configuration loaded from NVS");
  return true;
}

/**
 * @brief Save configuration to NVS
 */
bool apiConfigSave(void) {
  if (config_mutex) xSemaphoreTake(config_mutex, portMAX_DELAY);
  // Save motion config
  configSetInt(KEY_X_LIMIT_MIN, current_motion.soft_limit_low_mm[0]);
  configSetInt(KEY_X_LIMIT_MAX, current_motion.soft_limit_high_mm[0]);
  configSetInt(KEY_Y_LIMIT_MIN, current_motion.soft_limit_low_mm[1]);
  configSetInt(KEY_Y_LIMIT_MAX, current_motion.soft_limit_high_mm[1]);
  configSetInt(KEY_Z_LIMIT_MIN, current_motion.soft_limit_low_mm[2]);
  configSetInt(KEY_Z_LIMIT_MAX, current_motion.soft_limit_high_mm[2]);

  // Save Approach/Tuning params
  configSetInt(KEY_X_APPROACH, current_motion.x_approach_slow_mm);
  configSetInt(KEY_X_APPROACH_MED, current_motion.x_approach_med_mm);
  configSetFloat(KEY_TARGET_MARGIN, current_motion.target_margin_mm);

  // VFD config - speeds and timings are fixed in Altivar31 and set via Modbus
  // No need to save these as they are hardware constants

  // Save encoder config (PPM only, calibration flags are runtime-only)
  configSetInt(KEY_PPM_X, current_encoder.ppm[0]);
  configSetInt(KEY_PPM_Y, current_encoder.ppm[1]);
  configSetInt(KEY_PPM_Z, current_encoder.ppm[2]);

  // Flush all pending config changes (including network) to NVS
  configUnifiedSave();

  if (config_mutex) xSemaphoreGive(config_mutex);
  logInfo("[API_CONFIG] Configuration saved to NVS");
  return true;
}

/**
 * @brief Reset configuration to defaults
 */
bool apiConfigReset(void) {
  if (config_mutex) xSemaphoreTake(config_mutex, portMAX_DELAY);
  current_motion = default_motion;
  current_encoder = default_encoder;
  if (config_mutex) xSemaphoreGive(config_mutex);

  logWarning("[API_CONFIG] Configuration reset to defaults");
  return apiConfigSave();
}

/**
 * @brief P4 DRY: Generic integer range validator
 * Consolidates common validation pattern used by soft limits, VFD speeds, PPM, etc.
 */
static bool validateIntRange(JsonVariant value, int32_t min_val, int32_t max_val,
                             const char* field_name, const char* unit,
                             char* error_msg, size_t error_msg_len) {
  if (!value.is<int>()) {
    snprintf(error_msg, error_msg_len, "%s must be numeric (%d to %d %s)",
             field_name, (int)min_val, (int)max_val, unit ? unit : "");
    return false;
  }

  int32_t val = value.as<int32_t>();
  if (val < min_val || val > max_val) {
    snprintf(error_msg, error_msg_len, "%s must be between %d and %d %s",
             field_name, (int)min_val, (int)max_val, unit ? unit : "");
    return false;
  }

  return true;
}

/**
 * @brief Validate soft limit configuration
 * Changed from uint16_t to int32_t to support negative limits
 */
static bool validateSoftLimit(const char *key, JsonVariant value,
                              char *error_msg, size_t error_msg_len) {
  if (!validateIntRange(value, -10000, 10000, "Soft limit", "mm", error_msg, error_msg_len)) {
      return false;
  }
  
  // Cross-check bounds to ensure low < high
  int32_t val = value.as<int32_t>();
  if (strcmp(key, "soft_limit_x_low") == 0 && val >= current_motion.soft_limit_high_mm[0]) {
      snprintf(error_msg, error_msg_len, "X Low Limit must be < X High Limit (%ld)", (long)current_motion.soft_limit_high_mm[0]);
      return false;
  }
  if (strcmp(key, "soft_limit_x_high") == 0 && val <= current_motion.soft_limit_low_mm[0]) {
      snprintf(error_msg, error_msg_len, "X High Limit must be > X Low Limit (%ld)", (long)current_motion.soft_limit_low_mm[0]);
      return false;
  }
  if (strcmp(key, "soft_limit_y_low") == 0 && val >= current_motion.soft_limit_high_mm[1]) {
      snprintf(error_msg, error_msg_len, "Y Low Limit must be < Y High Limit (%ld)", (long)current_motion.soft_limit_high_mm[1]);
      return false;
  }
  if (strcmp(key, "soft_limit_y_high") == 0 && val <= current_motion.soft_limit_low_mm[1]) {
      snprintf(error_msg, error_msg_len, "Y High Limit must be > Y Low Limit (%ld)", (long)current_motion.soft_limit_low_mm[1]);
      return false;
  }
  if (strcmp(key, "soft_limit_z_low") == 0 && val >= current_motion.soft_limit_high_mm[2]) {
      snprintf(error_msg, error_msg_len, "Z Low Limit must be < Z High Limit (%ld)", (long)current_motion.soft_limit_high_mm[2]);
      return false;
  }
  if (strcmp(key, "soft_limit_z_high") == 0 && val <= current_motion.soft_limit_low_mm[2]) {
      snprintf(error_msg, error_msg_len, "Z High Limit must be > Z Low Limit (%ld)", (long)current_motion.soft_limit_low_mm[2]);
      return false;
  }

  return true;
}

/**
 * @brief Validate VFD speed configuration
 */
static bool validateVfdSpeed(const char *key, JsonVariant value,
                             char *error_msg, size_t error_msg_len) {
  (void)key;
  return validateIntRange(value, 1, 105, "Speed", "Hz", error_msg, error_msg_len);
}

/**
 * @brief Validate encoder PPM
 */
static bool validateEncoderPpm(const char *key, JsonVariant value,
                               char *error_msg, size_t error_msg_len) {
  (void)key;
  return validateIntRange(value, 50, 200, "PPM", "", error_msg, error_msg_len);
}


/**
 * @brief Validate configuration change
 */
bool apiConfigValidate(config_category_t category, const char *key,
                       JsonVariant value, char *error_msg,
                       size_t error_msg_len) {
  if (!key || !error_msg)
    return false;

  memset(error_msg, 0, error_msg_len);

  switch (category) {
  case CONFIG_CATEGORY_MOTION:
    if (strstr(key, "soft_limit")) {
      return validateSoftLimit(key, value, error_msg, error_msg_len);
    } else if (strstr(key, "x_appr") || strstr(key, "tgt_margin")) {
      if (!value.is<int>() && !value.is<float>()) {
        snprintf(error_msg, error_msg_len, "Value must be numeric");
        return false;
      }
      return true;
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

  case CONFIG_CATEGORY_SYSTEM:
    if (strcmp(key, "cli_echo") == 0 || strcmp(key, "ota_chk_en") == 0) {
        return validateBool(value, error_msg, error_msg_len);
    }
    break;

  case CONFIG_CATEGORY_SAFETY:
  case CONFIG_CATEGORY_THERMAL:
  case CONFIG_CATEGORY_NETWORK:
  case CONFIG_CATEGORY_SPINDLE:
  case CONFIG_CATEGORY_SERIAL:
  case CONFIG_CATEGORY_HARDWARE:
  case CONFIG_CATEGORY_BEHAVIOR:
  case CONFIG_CATEGORY_CALIBRATION:
  case CONFIG_CATEGORY_POSITIONS:
  case CONFIG_CATEGORY_WCS:
  case CONFIG_CATEGORY_SECURITY:
  case CONFIG_CATEGORY_STATS:
  default:
    // No specific validation yet
    break;
  }

  return true;
}

static bool validateBool(JsonVariant value, char* error_msg, size_t len) {
    if (!value.is<int>() && !value.is<bool>()) {
        snprintf(error_msg, len, "Value must be 0 or 1");
        return false;
    }
    return true;
}


static bool setFieldFromTable(const char* key, JsonVariant value, const config_field_t* fields, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(key, fields[i].json_key) == 0) {
      switch (fields[i].type) {
        case CFG_INT: configSetInt(fields[i].config_key, value.as<int>()); break;
        case CFG_FLOAT: configSetFloat(fields[i].config_key, value.as<float>()); break;
        case CFG_STRING: configSetString(fields[i].config_key, value.as<const char*>()); break;
      }
      return true;
    }
  }
  return false;
}

bool apiConfigSet(config_category_t category, const char *key, JsonVariant value) {
  bool found = false;

  switch (category) {
  case CONFIG_CATEGORY_MOTION:
    found = setFieldFromTable(key, value, motion_fields, ARRAY_SIZE(motion_fields));
    break;

  case CONFIG_CATEGORY_ENCODER:
    found = setFieldFromTable(key, value, encoder_fields, ARRAY_SIZE(encoder_fields));
    if (strcmp(key, "encoder_baud") == 0) {
      uint32_t baud = value.as<uint32_t>();
      configSetInt(KEY_ENC_BAUD, baud);
      if (configGetInt(KEY_ENC_INTERFACE, 0) == 1) {
        logInfo("[API_CONFIG] Syncing rs485_baud -> %lu (RS485 shared)", (unsigned long)baud);
        configSetInt(KEY_RS485_BAUD, baud);
      }
      found = true;
    }
    break;

  case CONFIG_CATEGORY_NETWORK:
    if (setFieldFromTable(key, value, network_int_fields, ARRAY_SIZE(network_int_fields))) found = true;
    else if (setFieldFromTable(key, value, network_str_fields, ARRAY_SIZE(network_str_fields))) found = true;
    break;

  case CONFIG_CATEGORY_SYSTEM:
    found = setFieldFromTable(key, value, system_fields, ARRAY_SIZE(system_fields));
    if (strcmp(key, "rs485_baud") == 0 || strcmp(key, KEY_RS485_BAUD) == 0) {
      uint32_t baud = value.as<uint32_t>();
      configSetInt(KEY_RS485_BAUD, baud);
      if (configGetInt(KEY_ENC_INTERFACE, 0) == 1) {
        logInfo("[API_CONFIG] Syncing encoder_baud -> %lu (RS485 shared)", (unsigned long)baud);
        configSetInt(KEY_ENC_BAUD, baud);
      }
      found = true;
    }
    break;

  case CONFIG_CATEGORY_VFD:
    found = setFieldFromTable(key, value, vfd_fields, ARRAY_SIZE(vfd_fields));
    break;

  case CONFIG_CATEGORY_SPINDLE:
    found = setFieldFromTable(key, value, spindle_fields, ARRAY_SIZE(spindle_fields));
    break;

  case CONFIG_CATEGORY_SERIAL:
    found = setFieldFromTable(key, value, serial_fields, ARRAY_SIZE(serial_fields));
    break;

  case CONFIG_CATEGORY_BEHAVIOR:
    if (setFieldFromTable(key, value, behavior_int_fields, ARRAY_SIZE(behavior_int_fields))) found = true;
    else if (setFieldFromTable(key, value, behavior_float_fields, ARRAY_SIZE(behavior_float_fields))) found = true;
    break;

  case CONFIG_CATEGORY_CALIBRATION:
    found = setFieldFromTable(key, value, calibration_fields, ARRAY_SIZE(calibration_fields));
    break;

  case CONFIG_CATEGORY_POSITIONS:
    found = setFieldFromTable(key, value, positions_fields, ARRAY_SIZE(positions_fields));
    break;

  default:
    return false;
  }

  if (found) {
    // Note: Caller (e.g., config batch API) is responsible for calling apiConfigLoad() 
    // or we manually update the RAM struct if we want immediate reflection without NVS reload overhead.
    // For single-key HTTP POSTs, the web server wrapper should invoke apiConfigLoad() or apiConfigSave().
    logInfo("[API_CONFIG] Configuration updated: %s", key);
    return true;
  }
  return false;
}

/**
 * @brief Get configuration as JSON
 */
bool apiConfigGet(config_category_t category, JsonVariant doc) {
  if (config_mutex) xSemaphoreTake(config_mutex, portMAX_DELAY);
  JsonObject obj = doc.is<JsonObject>() ? doc.as<JsonObject>() : doc.to<JsonObject>();

  switch (category) {
  case CONFIG_CATEGORY_MOTION: {
    obj["soft_limit_x_low"] = current_motion.soft_limit_low_mm[0];
    obj["soft_limit_x_high"] = current_motion.soft_limit_high_mm[0];
    obj["soft_limit_y_low"] = current_motion.soft_limit_low_mm[1];
    obj["soft_limit_y_high"] = current_motion.soft_limit_high_mm[1];
    obj["soft_limit_z_low"] = current_motion.soft_limit_low_mm[2];
    obj["soft_limit_z_high"] = current_motion.soft_limit_high_mm[2];
    obj["x_appr_slow"] = current_motion.x_approach_slow_mm;
    obj["x_appr_med"] = current_motion.x_approach_med_mm;
    obj["tgt_margin"] = current_motion.target_margin_mm;
    break;
  }

  case CONFIG_CATEGORY_ENCODER: {
    obj["ppm_x"] = current_encoder.ppm[0];
    obj["ppm_y"] = current_encoder.ppm[1];
    obj["ppm_z"] = current_encoder.ppm[2];
    break;
  }

  case CONFIG_CATEGORY_NETWORK: {
    // Station - Use String() to force ArduinoJson to copy values
    // (configGetString uses rotating buffer that gets overwritten)
    obj["wifi_ssid"] = String(configGetString(KEY_WIFI_SSID, "")); 
    obj["wifi_pass"] = String(configGetString(KEY_WIFI_PASS, ""));
    // AP - Default to ENABLED (1) if missing to match system default
    obj["wifi_ap_en"] = configGetInt(KEY_WIFI_AP_EN, 1);
    obj["wifi_ap_ssid"] = String(configGetString(KEY_WIFI_AP_SSID, "BISSO-E350-Setup"));
    obj["wifi_ap_pass"] = String(configGetString(KEY_WIFI_AP_PASS, "password"));
    obj["eth_en"] = configGetInt(KEY_ETH_ENABLED, 0);
    obj["eth_dhcp"] = configGetInt(KEY_ETH_DHCP, 1);
    obj["eth_ip"] = configGetString(KEY_ETH_IP, "");
    break;
  }

  case CONFIG_CATEGORY_SYSTEM: {
      obj["buzzer_en"] = configGetInt(KEY_BUZZER_EN, 1);
      obj["status_light_en"] = configGetInt(KEY_STATUS_LIGHT_EN, 0);
      obj["status_light_green"] = configGetInt(KEY_STATUS_LIGHT_GREEN, 13);
      obj["status_light_yellow"] = configGetInt(KEY_STATUS_LIGHT_YELLOW, 14);
      obj["status_light_red"] = configGetInt(KEY_STATUS_LIGHT_RED, 15);
      obj["recovery_en"] = configGetInt(KEY_RECOV_EN, 1);
      obj["lcd_en"] = configGetInt(KEY_LCD_EN, 1);
      obj["bootlog_en"] = configGetInt(KEY_BOOTLOG_EN, 1);
      obj["cli_echo"] = configGetInt(KEY_CLI_ECHO, 1); // Default ON for usability
      obj["ota_chk_en"] = configGetInt(KEY_OTA_CHECK_EN, 0);
      obj["buzzer_pin"] = configGetInt(KEY_BUZZER_PIN, 16);
      break;
  }

  case CONFIG_CATEGORY_SPINDLE: {
    obj["jxk10_en"] = configGetInt(KEY_JXK10_ENABLED, 0);
    obj["jxk10_addr"] = configGetInt(KEY_JXK10_ADDR, 1);
    obj["yhtc05_en"] = configGetInt(KEY_YHTC05_ENABLED, 0);
    obj["yhtc05_addr"] = configGetInt(KEY_YHTC05_ADDR, 3);
    obj["pause_en"] = configGetInt(KEY_SPINDL_PAUSE_EN, 1);
    break;
  }

  case CONFIG_CATEGORY_SERIAL: {
    obj["encoder_baud"] = configGetInt(KEY_ENC_BAUD, 9600);
    obj["encoder_iface"] = configGetInt(KEY_ENC_INTERFACE, 1);
    obj["encoder_addr"] = configGetInt(KEY_ENC_ADDR, 0);
    obj["enc_proto"] = configGetInt(KEY_ENC_PROTO, 0); // 0=ASCII, 1=Modbus
    obj["rs485_baud"] = configGetInt(KEY_RS485_BAUD, 9600);
    obj["i2c_speed"] = configGetInt(KEY_I2C_SPEED, 100000);
    break;
  }

  case CONFIG_CATEGORY_HARDWARE: {
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
      obj[signalDefinitions[i].key] = getPin(signalDefinitions[i].key);
    }
    break;
  }

  case CONFIG_CATEGORY_BEHAVIOR: {
    obj["jog_speed"] = configGetInt(KEY_DEFAULT_SPEED, 3000);
    obj["jog_accel"] = configGetInt(KEY_DEFAULT_ACCEL, 500);
    obj["x_approach"] = configGetInt(KEY_X_APPROACH, 5);            // Final approach (SLOW) - 5mm
    obj["x_approach_med"] = configGetInt(KEY_X_APPROACH_MED, 20);   // Medium approach - 20mm
    obj["target_margin"] = configGetFloat(KEY_TARGET_MARGIN, 0.1f); // Target position margin - 0.1mm
    obj["buf_en"] = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
    obj["strict_limits"] = configGetInt(KEY_MOTION_STRICT_LIMITS, 0);
    obj["stop_timeout"] = configGetInt(KEY_STOP_TIMEOUT, 2000);
    obj["stall_timeout"] = configGetInt(KEY_STALL_TIMEOUT, 1000);
    obj["buttons_en"] = configGetInt(KEY_BUTTONS_ENABLED, 1);
    break;
  }

  case CONFIG_CATEGORY_CALIBRATION: {
    obj["spd_x"] = configGetFloat(KEY_SPEED_CAL_X, 1.0f);
    obj["spd_y"] = configGetFloat(KEY_SPEED_CAL_Y, 1.0f);
    obj["spd_z"] = configGetFloat(KEY_SPEED_CAL_Z, 1.0f);
    obj["spd_a"] = configGetFloat(KEY_SPEED_CAL_A, 1.0f);
    break;
  }

  case CONFIG_CATEGORY_POSITIONS: {
    obj["safe_x"] = configGetFloat(KEY_POS_SAFE_X, 0.0f);
    obj["safe_y"] = configGetFloat(KEY_POS_SAFE_Y, 0.0f);
    obj["safe_z"] = configGetFloat(KEY_POS_SAFE_Z, 0.0f);
    obj["safe_a"] = configGetFloat(KEY_POS_SAFE_A, 0.0f);
    obj["p1_x"] = configGetFloat(KEY_POS_1_X, 0.0f);
    obj["p1_y"] = configGetFloat(KEY_POS_1_Y, 0.0f);
    obj["p1_z"] = configGetFloat(KEY_POS_1_Z, 0.0f);
    obj["p1_a"] = configGetFloat(KEY_POS_1_A, 0.0f);
    break;
  }

  case CONFIG_CATEGORY_WCS: {
    char wcsKey[16];
    for (int s = 0; s < 6; s++) {
      for (int a = 0; a < 4; a++) {
        snprintf(wcsKey, sizeof(wcsKey), "g%d_%c", 54 + s, "xyza"[a]);
        obj[wcsKey] = configGetFloat(wcsKey, 0.0f);
      }
    }
    break;
  }

  case CONFIG_CATEGORY_SECURITY: {
    obj["web_user"] = configGetString(KEY_WEB_USERNAME, "admin");
    obj["web_pass"] = configGetString(KEY_WEB_PASSWORD, "bisso");
    obj["ota_pass"] = configGetString(KEY_OTA_PASSWORD, "bisso-ota");
    break;
  }

  case CONFIG_CATEGORY_STATS: {
    obj["runtime_mins"] = configGetInt(KEY_RUNTIME_MINS, 0);
    obj["cycles"] = configGetInt(KEY_CYCLE_COUNT, 0);
    obj["maint_mins"] = configGetInt(KEY_LAST_MAINT_MINS, 0);
    break;
  }

  default:
    if (config_mutex) xSemaphoreGive(config_mutex);
    return false;
  }

  if (config_mutex) xSemaphoreGive(config_mutex);
  return true;
}

/**
 * @brief Get configuration schema
 */
bool apiConfigGetSchema(config_category_t category, JsonVariant json_doc) {
  JsonObject obj = json_doc.to<JsonObject>();

  switch (category) {
  case CONFIG_CATEGORY_MOTION: {
    obj["soft_limit_x_low"]["type"] = "integer";
    obj["soft_limit_x_low"]["min"] = -10000;
    obj["soft_limit_x_low"]["max"] = 10000;
    obj["soft_limit_x_low"]["unit"] = "mm";
    obj["soft_limit_x_high"]["type"] = "integer";
    obj["soft_limit_x_high"]["min"] = -10000;
    obj["soft_limit_x_high"]["max"] = 10000;
    obj["soft_limit_x_high"]["unit"] = "mm";
    obj["soft_limit_y_low"]["type"] = "integer";
    obj["soft_limit_y_low"]["min"] = -10000;
    obj["soft_limit_y_low"]["max"] = 10000;
    obj["soft_limit_y_low"]["unit"] = "mm";
    obj["soft_limit_y_high"]["type"] = "integer";
    obj["soft_limit_y_high"]["min"] = -10000;
    obj["soft_limit_y_high"]["max"] = 10000;
    obj["soft_limit_y_high"]["unit"] = "mm";
    obj["soft_limit_z_low"]["type"] = "integer";
    obj["soft_limit_z_low"]["min"] = -10000;
    obj["soft_limit_z_low"]["max"] = 10000;
    obj["soft_limit_z_low"]["unit"] = "mm";
    obj["soft_limit_z_high"]["type"] = "integer";
    obj["soft_limit_z_high"]["min"] = -10000;
    obj["soft_limit_z_high"]["max"] = 10000;
    obj["soft_limit_z_high"]["unit"] = "mm";
    break;
  }

  case CONFIG_CATEGORY_ENCODER: {
    obj["ppm_x"]["type"] = "integer";
    obj["ppm_x"]["min"] = 50;
    obj["ppm_x"]["max"] = 200;
    obj["ppm_y"]["type"] = "integer";
    obj["ppm_y"]["min"] = 50;
    obj["ppm_y"]["max"] = 200;
    obj["ppm_z"]["type"] = "integer";
    obj["ppm_z"]["min"] = 50;
    obj["ppm_z"]["max"] = 200;
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
bool apiConfigCalibrateEncoder(uint8_t axis, uint16_t ppm) {
  if (axis > 2)
    return false;
  if (ppm < 50 || ppm > 200)
    return false;

  current_encoder.ppm[axis] = ppm;
  current_encoder.calibrated[axis] = 1;

  logInfo("[API_CONFIG] Encoder %c calibrated: %u PPM", 'X' + axis, ppm);
  return true;
}

/**
 * @brief Export configuration as JSON
 */
size_t apiConfigExportJSON(char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size < 256)
    return 0;

  JsonDocument doc;
  apiConfigPopulate(doc);
  return serializeJson(doc, buffer, buffer_size);
}

void apiConfigPopulate(JsonDocument& doc) {
  // Metadata
  char timestamp[32];
  time_t t = time(NULL);
  if (t > 1000000) { // Check if time is set
      struct tm timeinfo;
      localtime_r(&t, &timeinfo);
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
  } else {
      snprintf(timestamp, sizeof(timestamp), "Boot+%lu ms", (unsigned long)millis());
  }
  doc["timestamp"] = timestamp; 
  char verStr[32];
  firmwareGetVersionString(verStr, sizeof(verStr));
  doc["firmware"] = verStr; 

  apiConfigGet(CONFIG_CATEGORY_MOTION, doc["motion"]);
  apiConfigGet(CONFIG_CATEGORY_VFD, doc["vfd"]);
  apiConfigGet(CONFIG_CATEGORY_ENCODER, doc["encoder"]);
  apiConfigGet(CONFIG_CATEGORY_NETWORK, doc["network"]);
  apiConfigGet(CONFIG_CATEGORY_SYSTEM, doc["system"]);
  apiConfigGet(CONFIG_CATEGORY_SPINDLE, doc["spindle"]);
  apiConfigGet(CONFIG_CATEGORY_SERIAL, doc["serial"]);
  apiConfigGet(CONFIG_CATEGORY_HARDWARE, doc["hardware"]);
  apiConfigGet(CONFIG_CATEGORY_BEHAVIOR, doc["behavior"]);
  apiConfigGet(CONFIG_CATEGORY_CALIBRATION, doc["calibration"]);
  apiConfigGet(CONFIG_CATEGORY_POSITIONS, doc["positions"]);
  apiConfigGet(CONFIG_CATEGORY_WCS, doc["wcs"]);
  apiConfigGet(CONFIG_CATEGORY_SECURITY, doc["security"]);
  apiConfigGet(CONFIG_CATEGORY_STATS, doc["stats"]);
}

/**
 * @brief Print configuration to serial
 */
void apiConfigPrint(void) {
  logInfo("[API_CONFIG] ============ Configuration Summary ============");
  logInfo(
      "[API_CONFIG] Motion: X[%u-%u] Y[%u-%u] Z[%u-%u] mm",
      current_motion.soft_limit_low_mm[0], current_motion.soft_limit_high_mm[0],
      current_motion.soft_limit_low_mm[1], current_motion.soft_limit_high_mm[1],
      current_motion.soft_limit_low_mm[2],
      current_motion.soft_limit_high_mm[2]);
  logInfo("[API_CONFIG] Encoder: X=%u Y=%u Z=%u PPM", current_encoder.ppm[0],
          current_encoder.ppm[1], current_encoder.ppm[2]);
  logInfo("[API_CONFIG] ================================================");
}

bool apiConfigBackupSD(const char* filename) {
    if (!sdCardIsMounted()) return false;
    
    // Ensure directory exists
    // The previous implementation mangled the directory string during recursive mkdir, 
    // crashing the subsequent SD.open() with deep folder hierarchies.
    char dir[128];
    strncpy(dir, filename, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (strlen(dir) > 0) sdCardMkdirRecursive(dir);
    }
  
    File f = SD.open(filename, FILE_WRITE);
    if (!f) return false;
  
    JsonDocument doc;
    apiConfigPopulate(doc);
    
    size_t bytes = serializeJson(doc, f);
    f.close();
    
    if (bytes > 0) {
        logInfo("[API_CONFIG] Backup saved to SD: %s (%zu bytes)", filename, bytes);
        return true;
    }
    return false;
}

bool apiConfigRestoreSD(const char* filename) {
    if (!sdCardIsMounted()) return false;
    
    File f = SD.open(filename, FILE_READ);
    if (!f) return false;
  
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
  
    if (err) {
        logError("[API_CONFIG] Failed to parse SD backup: %s", err.c_str());
        return false;
    }
  
    if (apiConfigImportJSON(doc)) {
        logInfo("[API_CONFIG] Restored from SD: %s", filename);
        return true;
    }
    return false;
}

/**
 * @brief Import configuration from JSON
 * @param doc JSON document containing configuration
 * @return true if import successful
 * 
 * P2 DRY: Uses table-driven importFields() helper instead of individual if statements
 */
bool apiConfigImportJSON(const JsonVariant& doc) {
  if (doc.isNull()) return false;
  
  // 1. Motion Config
  if (doc["motion"].is<JsonObject>()) {
    JsonObject m = doc["motion"];
    importFields(CONFIG_CATEGORY_MOTION, m, motion_fields, ARRAY_SIZE(motion_fields));
  }

  // 2. VFD Config
  if (doc["vfd"].is<JsonObject>()) {
    JsonObject v = doc["vfd"];
    importFields(CONFIG_CATEGORY_VFD, v, vfd_fields, ARRAY_SIZE(vfd_fields));
  }

  // 3. Encoder Config
  if (doc["encoder"].is<JsonObject>()) {
    JsonObject e = doc["encoder"];
    importFields(CONFIG_CATEGORY_ENCODER, e, encoder_fields, ARRAY_SIZE(encoder_fields));
  }

  // 4. Network (has both int and string fields)
  if (doc["network"].is<JsonObject>()) {
    JsonObject n = doc["network"];
    importFields(CONFIG_CATEGORY_NETWORK, n, network_int_fields, ARRAY_SIZE(network_int_fields));
    importFields(CONFIG_CATEGORY_NETWORK, n, network_str_fields, ARRAY_SIZE(network_str_fields));
  }

  // 5. System
  if (doc["system"].is<JsonObject>()) {
    JsonObject s = doc["system"];
    importFields(CONFIG_CATEGORY_SYSTEM, s, system_fields, ARRAY_SIZE(system_fields));
  }

  // 6. Spindle
  if (doc["spindle"].is<JsonObject>()) {
    JsonObject sp = doc["spindle"];
    importFields(CONFIG_CATEGORY_SPINDLE, sp, spindle_fields, ARRAY_SIZE(spindle_fields));
  }

  // 7. Serial
  if (doc["serial"].is<JsonObject>()) {
    JsonObject sr = doc["serial"];
    importFields(CONFIG_CATEGORY_SERIAL, sr, serial_fields, ARRAY_SIZE(serial_fields));
  }

  // 8. Hardware Pins (special handling - uses setPin instead of configSet)
  if (doc["hardware"].is<JsonObject>()) {
    JsonObject h = doc["hardware"];
    char error_msg[64];
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        const char* key = signalDefinitions[i].key;
        if (h.containsKey(key)) {
            JsonVariant val = h[key];
            if (apiConfigValidate(CONFIG_CATEGORY_HARDWARE, key, val, error_msg, sizeof(error_msg))) {
                setPin(key, val.as<int>()); 
            } else {
                logWarning("[API_CONFIG] JSON Import Validation Failed for %s: %s", key, error_msg);
            }
        }
    }
  }

  // 9. Behavior (has both int and float fields)
  if (doc["behavior"].is<JsonObject>()) {
    JsonObject b = doc["behavior"];
    importFields(CONFIG_CATEGORY_BEHAVIOR, b, behavior_int_fields, ARRAY_SIZE(behavior_int_fields));
    importFields(CONFIG_CATEGORY_BEHAVIOR, b, behavior_float_fields, ARRAY_SIZE(behavior_float_fields));
  }

  // 10. Calibration
  if (doc["calibration"].is<JsonObject>()) {
    JsonObject c = doc["calibration"];
    importFields(CONFIG_CATEGORY_CALIBRATION, c, calibration_fields, ARRAY_SIZE(calibration_fields));
  }

  // 11. Positions
  if (doc["positions"].is<JsonObject>()) {
    JsonObject p = doc["positions"];
    importFields(CONFIG_CATEGORY_POSITIONS, p, positions_fields, ARRAY_SIZE(positions_fields));
  }

  // Flush to NVS and reload RAM state
  configUnifiedSave();
  apiConfigLoad();
  return true;
}
