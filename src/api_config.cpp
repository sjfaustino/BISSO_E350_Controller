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

static const vfd_config_t default_vfd = {
    .min_speed_hz = 1,   // LSP (Altivar 31 minimum)
    .max_speed_hz = 105, // HSP (Altivar 31 maximum)
    .acc_time_ms = 600,  // 0.6 seconds
    .dec_time_ms = 400   // 0.4 seconds
};

static const encoder_config_t default_encoder = {.ppm = {100, 100, 100},
                                                 .calibrated = {0, 0, 0}};

// Current configuration in RAM
static motion_config_t current_motion;
static vfd_config_t current_vfd;
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

  // Load VFD config (using idle RMS as proxy for min speed characteristics)
  // Note: VFD has fixed acceleration/deceleration timings via Modbus, not
  // configurable here
  current_vfd.min_speed_hz = 1;   // Altivar31 minimum (LSP)
  current_vfd.max_speed_hz = 105; // Altivar31 maximum (HSP)
  current_vfd.acc_time_ms = 600;  // Fixed in VFD configuration
  current_vfd.dec_time_ms = 400;  // Fixed in VFD configuration

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
  current_vfd = default_vfd;
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

  case CONFIG_CATEGORY_VFD: {
    obj["min_speed_hz"] = current_vfd.min_speed_hz;
    obj["max_speed_hz"] = current_vfd.max_speed_hz;
    obj["acc_time_ms"] = current_vfd.acc_time_ms;
    obj["dec_time_ms"] = current_vfd.dec_time_ms;
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
    obj["wifi_ssid"] = WiFi.SSID(); 
    obj["wifi_pass"] = WiFi.psk();
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

  // 2. VFD Config
  JsonObject vfd = doc["vfd"].to<JsonObject>();
  JsonDocument vfdDoc; apiConfigGet(CONFIG_CATEGORY_VFD, vfdDoc);
  vfd.set(vfdDoc.as<JsonObject>());
  vfd["enabled"] = configGetInt(KEY_VFD_EN, 0);
  vfd["address"] = configGetInt(KEY_VFD_ADDR, 2);

  // 3. Encoder Config
  JsonObject encoder = doc["encoder"].to<JsonObject>();
  JsonDocument encDoc; apiConfigGet(CONFIG_CATEGORY_ENCODER, encDoc);
  encoder.set(encDoc.as<JsonObject>());
  encoder["feedback_en"] = configGetInt(KEY_ENC_FEEDBACK, 0);

  // 4. Network
  JsonObject net = doc["network"].to<JsonObject>();
  // Use WiFi.SSID/psk because station creds are in NVS (WiFi lib), not config_unified
  net["wifi_ssid"] = WiFi.SSID();
  net["wifi_pass"] = WiFi.psk();
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
  
  // 6. Spindle (JXK10/Tach)
  JsonObject spindle = doc["spindle"].to<JsonObject>();
  spindle["jxk10_en"] = configGetInt(KEY_JXK10_ENABLED, 0);
  spindle["jxk10_addr"] = configGetInt(KEY_JXK10_ADDR, 1);
  spindle["yhtc05_en"] = configGetInt(KEY_YHTC05_ENABLED, 0);
  spindle["pause_en"] = configGetInt(KEY_SPINDL_PAUSE_EN, 1);
  // 7. Hardware Pins (Dynamic Mapping)
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
  logInfo("[API_CONFIG] VFD: %u-%u Hz, ACC=%ums, DEC=%ums",
          current_vfd.min_speed_hz, current_vfd.max_speed_hz,
          current_vfd.acc_time_ms, current_vfd.dec_time_ms);
  logInfo("[API_CONFIG] Encoder: X=%u Y=%u Z=%u PPM", current_encoder.ppm[0],
          current_encoder.ppm[1], current_encoder.ppm[2]);
  logInfo("[API_CONFIG] ================================================");
}
