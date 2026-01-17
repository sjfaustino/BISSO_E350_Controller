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

static bool validateBool(JsonVariant value, char* error_msg, size_t len);



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

/**
 * @brief Initialize API
 */
void apiConfigInit(void) {
  logInfo("[API_CONFIG] Initializing");
  apiConfigLoad();
  logInfo("[API_CONFIG] Ready");
}

/**
 * @brief Load configuration from NVS
 */
bool apiConfigLoad(void) {
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

  logInfo("[API_CONFIG] Configuration loaded from NVS");
  return true;
}

/**
 * @brief Save configuration to NVS
 */
bool apiConfigSave(void) {
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

  logInfo("[API_CONFIG] Configuration saved to NVS");
  return true;
}

/**
 * @brief Reset configuration to defaults
 */
bool apiConfigReset(void) {
  current_motion = default_motion;
  current_encoder = default_encoder;

  logWarning("[API_CONFIG] Configuration reset to defaults");
  return apiConfigSave();
}

/**
 * @brief Validate soft limit configuration
 * PHASE 5.10: Changed from uint16_t to int32_t to support negative limits
 * (e.g., machines with coordinate systems crossing zero: min=-500, max=500)
 */
static bool validateSoftLimit(const char *key, JsonVariant value,
                              char *error_msg, size_t error_msg_len) {
  if (!value.is<int>()) {
    snprintf(error_msg, error_msg_len,
             "Soft limit must be numeric (-10000 to +10000 mm)");
    return false;
  }

  int32_t val = value.as<int32_t>();
  if (val < -10000 || val > 10000) {
    snprintf(error_msg, error_msg_len, "Soft limit must be between -10000 and +10000 mm");
    return false;
  }

  return true;
}

/**
 * @brief Validate VFD speed configuration
 */
static bool validateVfdSpeed(const char *key, JsonVariant value,
                             char *error_msg, size_t error_msg_len) {
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
static bool validateEncoderPpm(const char *key, JsonVariant value,
                               char *error_msg, size_t error_msg_len) {
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
    // No specific validation yet
    break;

    return false;
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


/**
 * @brief Set configuration value
 */
bool apiConfigSet(config_category_t category, const char *key,
                  JsonVariant value) {
  char error_msg[256];

  // Validate first
  if (!apiConfigValidate(category, key, value, error_msg, sizeof(error_msg))) {
    logWarning("[API_CONFIG] Validation failed for %s: %s", key, error_msg);
    return false;
  }

  // Apply based on category
  switch (category) {
  case CONFIG_CATEGORY_MOTION:
    // PHASE 5.10: Changed from uint16_t to int32_t to support negative limits
    if (strcmp(key, "soft_limit_x_low") == 0) {
      current_motion.soft_limit_low_mm[0] = value.as<int32_t>();
    } else if (strcmp(key, "soft_limit_x_high") == 0) {
      current_motion.soft_limit_high_mm[0] = value.as<int32_t>();
    } else if (strcmp(key, "soft_limit_y_low") == 0) {
      current_motion.soft_limit_low_mm[1] = value.as<int32_t>();
    } else if (strcmp(key, "soft_limit_y_high") == 0) {
      current_motion.soft_limit_high_mm[1] = value.as<int32_t>();
    } else if (strcmp(key, "soft_limit_z_low") == 0) {
      current_motion.soft_limit_low_mm[2] = value.as<int32_t>();
    } else if (strcmp(key, "soft_limit_z_high") == 0) {
      current_motion.soft_limit_high_mm[2] = value.as<int32_t>();
    } else if (strcmp(key, "x_appr_slow") == 0) {
      current_motion.x_approach_slow_mm = value.as<int32_t>();
    } else if (strcmp(key, "x_appr_med") == 0) {
      current_motion.x_approach_med_mm = value.as<int32_t>();
    } else if (strcmp(key, "tgt_margin") == 0) {
      current_motion.target_margin_mm = value.as<float>();
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

  case CONFIG_CATEGORY_NETWORK:
    // Direct NVS saves for network to ensure they persist immediately
    // Note: network_manager reloads these on reboot or on specific commands
    if (strcmp(key, "wifi_ssid") == 0) {
        // Station SSID is special - managed by WiFi lib, but we can save to NVS buffer if needed
        // For now, we assume ConfigSetString will be called by caller or we use configSetString here
        // BUT apiConfigSet typically updates RAM state. 
        // NetworkManager reads NVS directly. So we should write to NVS.
        // However, apiConfigSet is usually followed by apiConfigSave for some categories.
        // Let's write to NVS directly here since Network doesn't have a "current_network" struct in RAM in this file.
        // Station creds are usually handled by WiFi.begin() persistence, but backing up in NVS keys is good practice if we unify.
        // Actually, let's just use configSetString for these.
        // But wait, apiConfigSet returns void/bool. Caller might call apiConfigSave later.
        // Since we don't have RAM struct, let's write to NVS cache (which is what configSetString does).
        // WARNING: apiConfigSave() (lines 83-101) only saves Motion/Encoder!
        // So relying on apiConfigSave() to save Network is WRONG if we only update NVS cache here and don't flush.
        // configSetString updates the cache. configUnifiedSave() flushes it.
        // We should probably call configSetString here.
        configSetString(KEY_WIFI_SSID, value.as<const char*>());
    } else if (strcmp(key, "wifi_pass") == 0) {
        configSetString(KEY_WIFI_PASS, value.as<const char*>());
    } else if (strcmp(key, "wifi_ap_en") == 0) {
        configSetInt(KEY_WIFI_AP_EN, value.as<int>());
    } else if (strcmp(key, "wifi_ap_ssid") == 0) {
        configSetString(KEY_WIFI_AP_SSID, value.as<const char*>());
    } else if (strcmp(key, "wifi_ap_pass") == 0) {
        configSetString(KEY_WIFI_AP_PASS, value.as<const char*>());
    } else if (strcmp(key, "eth_en") == 0) {
        configSetInt(KEY_ETH_ENABLED, value.as<int>());
    }

    break;

  case CONFIG_CATEGORY_SYSTEM:
    if (strcmp(key, "cli_echo") == 0) {
        configSetInt(KEY_CLI_ECHO, value.as<int>());
    } else if (strcmp(key, "ota_chk_en") == 0) {
        configSetInt(KEY_OTA_CHECK_EN, value.as<int>());
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
bool apiConfigGet(config_category_t category, JsonDocument &json_doc) {
  JsonObject obj = json_doc.to<JsonObject>();

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
    JsonArray ppm = obj["ppm"].to<JsonArray>();
    ppm.add(current_encoder.ppm[0]);
    ppm.add(current_encoder.ppm[1]);
    ppm.add(current_encoder.ppm[2]);
    JsonArray cal = obj["calibrated"].to<JsonArray>();
    cal.add(current_encoder.calibrated[0]);
    cal.add(current_encoder.calibrated[1]);
    cal.add(current_encoder.calibrated[2]);
    break;
  }

  case CONFIG_CATEGORY_NETWORK: {
    // Station
    obj["wifi_ssid"] = configGetString(KEY_WIFI_SSID, ""); 
    obj["wifi_pass"] = configGetString(KEY_WIFI_PASS, "");
    // AP - Default to ENABLED (1) if missing to match system default
    obj["wifi_ap_en"] = configGetInt(KEY_WIFI_AP_EN, 1);
    obj["wifi_ap_ssid"] = configGetString(KEY_WIFI_AP_SSID, "BISSO-E350-Setup");
    obj["wifi_ap_pass"] = configGetString(KEY_WIFI_AP_PASS, "password");
    obj["eth_en"] = configGetInt(KEY_ETH_ENABLED, 0);
    break;
  }

  case CONFIG_CATEGORY_SYSTEM: {
      obj["cli_echo"] = configGetInt(KEY_CLI_ECHO, 1); // Default is Enabled (echo ON) for usability
      obj["ota_chk_en"] = configGetInt(KEY_OTA_CHECK_EN, 0);
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
bool apiConfigGetSchema(config_category_t category, JsonDocument &json_doc) {
  JsonObject obj = json_doc.to<JsonObject>();

  switch (category) {
  case CONFIG_CATEGORY_MOTION: {
    // PHASE 5.10: Changed min from 0 to -10000 to support negative coordinates
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

  // MEMORY FIX: Use StaticJsonDocument to prevent heap fragmentation
  // Sized for motion + VFD + encoder config (~30 key-value pairs)
  JsonDocument doc;

  // Metadata (placeholder, updated by web_server)
  doc["timestamp"] = ""; 
  doc["firmware"] = ""; 

  // 1. Motion Config
  JsonObject motion = doc["motion"].to<JsonObject>();
  JsonDocument motionDoc; apiConfigGet(CONFIG_CATEGORY_MOTION, motionDoc);
  motion.set(motionDoc.as<JsonObject>());
  // Add extras not in category getter
  motion["home_enable"] = configGetInt(KEY_HOME_ENABLE, 0);
  motion["home_fast"] = configGetInt(KEY_HOME_PROFILE_FAST, 100);
  motion["home_slow"] = configGetInt(KEY_HOME_PROFILE_SLOW, 20);

  // 2. VFD Config (hardware settings only - speed/timing are fixed in Altivar31)
  JsonObject vfd = doc["vfd"].to<JsonObject>();
  vfd["enabled"] = configGetInt(KEY_VFD_EN, 0);
  vfd["address"] = configGetInt(KEY_VFD_ADDR, 2);

  // 3. Encoder Config
  JsonObject encoder = doc["encoder"].to<JsonObject>();
  JsonDocument encDoc; apiConfigGet(CONFIG_CATEGORY_ENCODER, encDoc);
  encoder.set(encDoc.as<JsonObject>());
  encoder["feedback_en"] = configGetInt(KEY_ENC_FEEDBACK, 0);

  // 4. Network
  JsonObject net = doc["network"].to<JsonObject>();
  // Use NVS-stored credentials, not currently connected ones
  net["wifi_ssid"] = configGetString(KEY_WIFI_SSID, "");
  net["wifi_pass"] = configGetString(KEY_WIFI_PASS, "");
  net["wifi_ap_en"] = configGetInt(KEY_WIFI_AP_EN, 0);
  net["wifi_ap_ssid"] = configGetString(KEY_WIFI_AP_SSID, "BISSO-E350-Setup");
  net["wifi_ap_pass"] = configGetString(KEY_WIFI_AP_PASS, "password");
  net["eth_en"] = configGetInt(KEY_ETH_ENABLED, 1);
  net["eth_dhcp"] = configGetInt(KEY_ETH_DHCP, 1);
  net["eth_ip"] = configGetString(KEY_ETH_IP, "");

  // 5. System/Peripherals
  JsonObject sys = doc["system"].to<JsonObject>();
  sys["buzzer_en"] = configGetInt(KEY_BUZZER_EN, 1);
  sys["status_light_en"] = configGetInt(KEY_STATUS_LIGHT_EN, 0);
  sys["recovery_en"] = configGetInt(KEY_RECOV_EN, 1);
  sys["lcd_en"] = configGetInt(KEY_LCD_EN, 1);
  sys["bootlog_en"] = configGetInt(KEY_BOOTLOG_EN, 1);
  sys["cli_echo"] = configGetInt(KEY_CLI_ECHO, 1);  // Default ON for usability
  sys["ota_chk_en"] = configGetInt(KEY_OTA_CHECK_EN, 0);
  
  // 6. Spindle (JXK10/Tach)
  JsonObject spindle = doc["spindle"].to<JsonObject>();
  spindle["jxk10_en"] = configGetInt(KEY_JXK10_ENABLED, 0);
  spindle["jxk10_addr"] = configGetInt(KEY_JXK10_ADDR, 1);
  spindle["yhtc05_en"] = configGetInt(KEY_YHTC05_ENABLED, 0);
  spindle["yhtc05_addr"] = configGetInt(KEY_YHTC05_ADDR, 3);
  spindle["pause_en"] = configGetInt(KEY_SPINDL_PAUSE_EN, 1);

  // 7. Serial Communication
  JsonObject serial = doc["serial"].to<JsonObject>();
  serial["encoder_baud"] = configGetInt(KEY_ENC_BAUD, 9600);
  serial["rs485_baud"] = configGetInt(KEY_RS485_BAUD, 9600);
  serial["i2c_speed"] = configGetInt(KEY_I2C_SPEED, 100000);

  // 8. Hardware Pins (Dynamic Mapping)
  JsonObject hw = doc["hardware"].to<JsonObject>();
  for (size_t i = 0; i < SIGNAL_COUNT; i++) {
      hw[signalDefinitions[i].key] = getPin(signalDefinitions[i].key);
  }

  // 8. Motion Behavior & Safety
  JsonObject behavior = doc["behavior"].to<JsonObject>();
  behavior["jog_speed"] = configGetInt(KEY_DEFAULT_SPEED, 3000);
  behavior["jog_accel"] = configGetInt(KEY_DEFAULT_ACCEL, 500);
  behavior["x_approach"] = configGetInt(KEY_X_APPROACH, 5);            // Final approach (SLOW) - 5mm
  behavior["x_approach_med"] = configGetInt(KEY_X_APPROACH_MED, 20);   // Medium approach - 20mm
  behavior["target_margin"] = configGetFloat(KEY_TARGET_MARGIN, 0.1f); // Target position margin - 0.1mm
  behavior["buf_en"] = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
  behavior["strict_limits"] = configGetInt(KEY_MOTION_STRICT_LIMITS, 0);
  behavior["stop_timeout"] = configGetInt(KEY_STOP_TIMEOUT, 2000);
  behavior["stall_timeout"] = configGetInt(KEY_STALL_TIMEOUT, 1000);
  behavior["buttons_en"] = configGetInt(KEY_BUTTONS_ENABLED, 1);

  // 9. Speed Calibration
  JsonObject cal = doc["calibration"].to<JsonObject>();
  cal["spd_x"] = configGetFloat(KEY_SPEED_CAL_X, 1.0f);
  cal["spd_y"] = configGetFloat(KEY_SPEED_CAL_Y, 1.0f);
  cal["spd_z"] = configGetFloat(KEY_SPEED_CAL_Z, 1.0f);
  cal["spd_a"] = configGetFloat(KEY_SPEED_CAL_A, 1.0f);

  // 10. Positions (Safe/User)
  JsonObject pos = doc["positions"].to<JsonObject>();
  pos["safe_x"] = configGetFloat(KEY_POS_SAFE_X, 0.0f);
  pos["safe_y"] = configGetFloat(KEY_POS_SAFE_Y, 0.0f);
  pos["safe_z"] = configGetFloat(KEY_POS_SAFE_Z, 0.0f);
  pos["safe_a"] = configGetFloat(KEY_POS_SAFE_A, 0.0f);
  pos["p1_x"] = configGetFloat(KEY_POS_1_X, 0.0f);
  pos["p1_y"] = configGetFloat(KEY_POS_1_Y, 0.0f);
  pos["p1_z"] = configGetFloat(KEY_POS_1_Z, 0.0f);
  pos["p1_a"] = configGetFloat(KEY_POS_1_A, 0.0f);

  // 11. Work Coordinate Systems (G54-G59)
  JsonObject wcs = doc["wcs"].to<JsonObject>();
  char wcsKey[16];
  for (int s = 0; s < 6; s++) {
      for (int a = 0; a < 4; a++) {
          snprintf(wcsKey, sizeof(wcsKey), "g%d_%c", 54 + s, "xyza"[a]);
          wcs[wcsKey] = configGetFloat(wcsKey, 0.0f);
      }
  }

  // 12. Security (Credentials)
  JsonObject sec = doc["security"].to<JsonObject>();
  sec["web_user"] = configGetString(KEY_WEB_USERNAME, "admin");
  sec["web_pass"] = configGetString(KEY_WEB_PASSWORD, "bisso");
  sec["ota_pass"] = configGetString(KEY_OTA_PASSWORD, "bisso-ota");

  // 13. Statistics/Counters
  JsonObject stats = doc["stats"].to<JsonObject>();
  stats["runtime_mins"] = configGetInt(KEY_RUNTIME_MINS, 0);
  stats["cycles"] = configGetInt(KEY_CYCLE_COUNT, 0);
  stats["maint_mins"] = configGetInt(KEY_LAST_MAINT_MINS, 0);
  
  return serializeJson(doc, buffer, buffer_size);
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

/**
 * @brief Import configuration from JSON
 * @param doc JSON document containing configuration
 * @return true if import successful
 */
bool apiConfigImportJSON(const JsonVariant& doc) {
  if (doc.isNull()) return false;
  
  // 1. Motion Config
  if (doc["motion"].is<JsonObject>()) {
    JsonObject m = doc["motion"];
    if (m["soft_limit_x_low"].is<int>()) configSetInt(KEY_X_LIMIT_MIN, m["soft_limit_x_low"]);
    if (m["soft_limit_x_high"].is<int>()) configSetInt(KEY_X_LIMIT_MAX, m["soft_limit_x_high"]);
    if (m["soft_limit_y_low"].is<int>()) configSetInt(KEY_Y_LIMIT_MIN, m["soft_limit_y_low"]);
    if (m["soft_limit_y_high"].is<int>()) configSetInt(KEY_Y_LIMIT_MAX, m["soft_limit_y_high"]);
    if (m["soft_limit_z_low"].is<int>()) configSetInt(KEY_Z_LIMIT_MIN, m["soft_limit_z_low"]);
    if (m["soft_limit_z_high"].is<int>()) configSetInt(KEY_Z_LIMIT_MAX, m["soft_limit_z_high"]);
    
    // Extras
    if (m["home_enable"].is<int>()) configSetInt(KEY_HOME_ENABLE, m["home_enable"]);
    if (m["home_fast"].is<int>()) configSetInt(KEY_HOME_PROFILE_FAST, m["home_fast"]);
    if (m["home_slow"].is<int>()) configSetInt(KEY_HOME_PROFILE_SLOW, m["home_slow"]);
  }

  // 2. VFD Config
  if (doc["vfd"].is<JsonObject>()) {
    JsonObject v = doc["vfd"];
    if (v["enabled"].is<int>()) configSetInt(KEY_VFD_EN, v["enabled"]);
    if (v["address"].is<int>()) configSetInt(KEY_VFD_ADDR, v["address"]);
  }

  // 3. Encoder Config
  if (doc["encoder"].is<JsonObject>()) {
    JsonObject e = doc["encoder"];
    if (e["ppm_x"].is<int>()) configSetInt(KEY_PPM_X, e["ppm_x"]);
    if (e["ppm_y"].is<int>()) configSetInt(KEY_PPM_Y, e["ppm_y"]);
    if (e["ppm_z"].is<int>()) configSetInt(KEY_PPM_Z, e["ppm_z"]);
    if (e["feedback_en"].is<int>()) configSetInt(KEY_ENC_FEEDBACK, e["feedback_en"]);
  }

  // 4. Network
  if (doc["network"].is<JsonObject>()) {
    JsonObject n = doc["network"];
    if (n["wifi_ap_en"].is<int>()) configSetInt(KEY_WIFI_AP_EN, n["wifi_ap_en"]);
    if (n["eth_en"].is<int>()) configSetInt(KEY_ETH_ENABLED, n["eth_en"]);
    if (n["eth_dhcp"].is<int>()) configSetInt(KEY_ETH_DHCP, n["eth_dhcp"]);
    // Credentials might be skipped for security or included
    if (n["wifi_ssid"].is<const char*>()) configSetString(KEY_WIFI_SSID, n["wifi_ssid"]);
    if (n["wifi_pass"].is<const char*>()) configSetString(KEY_WIFI_PASS, n["wifi_pass"]);
    if (n["wifi_ap_ssid"].is<const char*>()) configSetString(KEY_WIFI_AP_SSID, n["wifi_ap_ssid"]);
    if (n["wifi_ap_pass"].is<const char*>()) configSetString(KEY_WIFI_AP_PASS, n["wifi_ap_pass"]);
  }

  // 5. System
  if (doc["system"].is<JsonObject>()) {
    JsonObject s = doc["system"];
    if (s["buzzer_en"].is<int>()) configSetInt(KEY_BUZZER_EN, s["buzzer_en"]);
    if (s["status_light_en"].is<int>()) configSetInt(KEY_STATUS_LIGHT_EN, s["status_light_en"]);
    if (s["recovery_en"].is<int>()) configSetInt(KEY_RECOV_EN, s["recovery_en"]);
    if (s["lcd_en"].is<int>()) configSetInt(KEY_LCD_EN, s["lcd_en"]);
    if (s["bootlog_en"].is<int>()) configSetInt(KEY_BOOTLOG_EN, s["bootlog_en"]);
    if (s["cli_echo"].is<int>()) configSetInt(KEY_CLI_ECHO, s["cli_echo"]);
    if (s["ota_chk_en"].is<int>()) configSetInt(KEY_OTA_CHECK_EN, s["ota_chk_en"]);
  }

  // 6. Spindle
  if (doc["spindle"].is<JsonObject>()) {
     JsonObject sp = doc["spindle"];
     if (sp["jxk10_en"].is<int>()) configSetInt(KEY_JXK10_ENABLED, sp["jxk10_en"]);
     if (sp["jxk10_addr"].is<int>()) configSetInt(KEY_JXK10_ADDR, sp["jxk10_addr"]);
     if (sp["yhtc05_en"].is<int>()) configSetInt(KEY_YHTC05_ENABLED, sp["yhtc05_en"]);
     if (sp["yhtc05_addr"].is<int>()) configSetInt(KEY_YHTC05_ADDR, sp["yhtc05_addr"]);
     if (sp["pause_en"].is<int>()) configSetInt(KEY_SPINDL_PAUSE_EN, sp["pause_en"]);
  }

  // 7. Serial
  if (doc["serial"].is<JsonObject>()) {
     JsonObject sr = doc["serial"];
     if (sr["encoder_baud"].is<int>()) configSetInt(KEY_ENC_BAUD, sr["encoder_baud"]);
     if (sr["rs485_baud"].is<int>()) configSetInt(KEY_RS485_BAUD, sr["rs485_baud"]);
     if (sr["i2c_speed"].is<int>()) configSetInt(KEY_I2C_SPEED, sr["i2c_speed"]);
  }

  // 8. Hardware Pins
  if (doc["hardware"].is<JsonObject>()) {
    JsonObject h = doc["hardware"];
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        const char* key = signalDefinitions[i].key;
        if (h[key].is<int>()) {
            setPin(key, h[key]); 
        }
    }
  }

  // 9. Behavior
  if (doc["behavior"].is<JsonObject>()) {
    JsonObject b = doc["behavior"];
    if (b["jog_speed"].is<int>()) configSetInt(KEY_DEFAULT_SPEED, b["jog_speed"]);
    if (b["jog_accel"].is<int>()) configSetInt(KEY_DEFAULT_ACCEL, b["jog_accel"]);
    if (b["x_approach"].is<int>()) configSetInt(KEY_X_APPROACH, b["x_approach"]);
    if (b["x_approach_med"].is<int>()) configSetInt(KEY_X_APPROACH_MED, b["x_approach_med"]);
    if (b["target_margin"].is<float>()) configSetFloat(KEY_TARGET_MARGIN, b["target_margin"]);
    if (b["buf_en"].is<int>()) configSetInt(KEY_MOTION_BUFFER_ENABLE, b["buf_en"]);
    if (b["strict_limits"].is<int>()) configSetInt(KEY_MOTION_STRICT_LIMITS, b["strict_limits"]);
    if (b["stop_timeout"].is<int>()) configSetInt(KEY_STOP_TIMEOUT, b["stop_timeout"]);
    if (b["stall_timeout"].is<int>()) configSetInt(KEY_STALL_TIMEOUT, b["stall_timeout"]);
    if (b["buttons_en"].is<int>()) configSetInt(KEY_BUTTONS_ENABLED, b["buttons_en"]);
  }

  // 10. Calibration
  if (doc["calibration"].is<JsonObject>()) {
    JsonObject c = doc["calibration"];
    if (c["spd_x"].is<float>()) configSetFloat(KEY_SPEED_CAL_X, c["spd_x"]);
    if (c["spd_y"].is<float>()) configSetFloat(KEY_SPEED_CAL_Y, c["spd_y"]);
    if (c["spd_z"].is<float>()) configSetFloat(KEY_SPEED_CAL_Z, c["spd_z"]);
    if (c["spd_a"].is<float>()) configSetFloat(KEY_SPEED_CAL_A, c["spd_a"]);
  }

  // 11. Positions
  if (doc["positions"].is<JsonObject>()) {
     JsonObject p = doc["positions"];
     if (p["safe_x"].is<float>()) configSetFloat(KEY_POS_SAFE_X, p["safe_x"]);
     if (p["safe_y"].is<float>()) configSetFloat(KEY_POS_SAFE_Y, p["safe_y"]);
     if (p["safe_z"].is<float>()) configSetFloat(KEY_POS_SAFE_Z, p["safe_z"]);
     if (p["safe_a"].is<float>()) configSetFloat(KEY_POS_SAFE_A, p["safe_a"]);
     if (p["p1_x"].is<float>()) configSetFloat(KEY_POS_1_X, p["p1_x"]);
     if (p["p1_y"].is<float>()) configSetFloat(KEY_POS_1_Y, p["p1_y"]);
     if (p["p1_z"].is<float>()) configSetFloat(KEY_POS_1_Z, p["p1_z"]);
     if (p["p1_a"].is<float>()) configSetFloat(KEY_POS_1_A, p["p1_a"]);
  }

  return true;
}
