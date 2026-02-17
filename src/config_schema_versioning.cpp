#include "config_schema_versioning.h"
#include "config_unified.h"
#include "config_keys.h"
#include "config_migration.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include <Preferences.h>
#include <string.h>

static Preferences schema_prefs;


static const schema_record_t schema_history[] = {
  {0, "Initial schema (v0.1)", "Base motion control", 0},
  {1, "Enhanced schema (v4.2)", "Speed calibration, NVS persistence", 1700000000},
  {2, "PosiPro v1.0.0", "Motion Buffering, Enhanced Security & Compressed Telemetry", 1735000000},
  {3, "PosiPro v1.1.0", "Encoder Config Unification", 1736600000}
};

static const config_key_metadata_t key_metadata[] = {
  // Motion Limits
  {KEY_X_LIMIT_MIN, 0, 0, "int32", "X min limit", true, true},
  {KEY_X_LIMIT_MAX, 0, 0, "int32", "X max limit", true, true},
  {KEY_Y_LIMIT_MIN, 0, 0, "int32", "Y min limit", true, true},
  {KEY_Y_LIMIT_MAX, 0, 0, "int32", "Y max limit", true, true},
  {KEY_Z_LIMIT_MIN, 0, 0, "int32", "Z min limit", true, true},
  {KEY_Z_LIMIT_MAX, 0, 0, "int32", "Z max limit", true, true},
  {KEY_A_LIMIT_MIN, 0, 0, "int32", "A min limit", true, true},
  {KEY_A_LIMIT_MAX, 0, 0, "int32", "A max limit", true, true},
  
  // Dynamics
  {KEY_DEFAULT_SPEED, 0, 0, "float", "Default speed", true, true},
  {KEY_DEFAULT_ACCEL, 0, 0, "float", "Default accel", true, true},
  
  // Calibration
  {KEY_SPEED_CAL_X, 1, 0, "float", "Calib X Speed", true, false},
  {KEY_SPEED_CAL_Y, 1, 0, "float", "Calib Y Speed", true, false},
  {KEY_SPEED_CAL_Z, 1, 0, "float", "Calib Z Speed", true, false},
  {KEY_SPEED_CAL_A, 1, 0, "float", "Calib A Speed", true, false},
  {KEY_PPM_X, 0, 0, "int32", "X PPM", true, true},
  {KEY_PPM_Y, 0, 0, "int32", "Y PPM", true, true},
  {KEY_PPM_Z, 0, 0, "int32", "Z PPM", true, true},
  {KEY_PPM_A, 0, 0, "int32", "A PPM", true, true},
  
  // Advanced Tuning
  {KEY_STALL_TIMEOUT, 0, 0, "int32", "Stall Timeout", true, true},
  {KEY_X_APPROACH, 1, 0, "int32", "X Final Approach (mm)", true, true},
  {KEY_X_APPROACH_MED, 1, 0, "int32", "X Medium Approach (mm)", true, true},
  {KEY_TARGET_MARGIN, 1, 0, "float", "Target Position Margin (mm)", true, true},

  // NEW in v2.0
  {KEY_MOTION_BUFFER_ENABLE, 2, 0, "int32", "Enable Motion Queue (0/1)", true, true},
  {KEY_WIFI_SSID, 2, 0, "string", "WiFi SSID", true, true},
  {KEY_WIFI_PASS, 2, 0, "string", "WiFi Password", true, true},
  {KEY_WIFI_AP_EN, 2, 0, "int32", "WiFi AP Enable (0/1)", true, true},
  {KEY_WIFI_AP_SSID, 2, 0, "string", "WiFi AP SSID", true, true},
  {KEY_WIFI_AP_PASS, 2, 0, "string", "WiFi AP Password", true, true},

  // NEW in v3.0
  {KEY_ENC_BAUD, 3, 0, "int32", "Encoder Baud Rate", true, true},
  {KEY_ENC_INTERFACE, 3, 0, "int32", "Encoder Interface (0=RS232, 1=RS485)", true, true},
  
  // VFD / RS485 Devices (Missing keys added)
  {KEY_VFD_EN, 0, 0, "int32", "Enable VFD (0/1)", true, true},
  {KEY_VFD_ADDR, 0, 0, "int32", "VFD Address", true, true},
  {KEY_JXK10_ENABLED, 0, 0, "int32", "Enable JXK10 (0/1)", true, true},
  {KEY_JXK10_ADDR, 0, 0, "int32", "JXK10 Address", true, true},
  {KEY_YHTC05_ENABLED, 0, 0, "int32", "Enable Tachometer (0/1)", true, true},
  {KEY_YHTC05_ADDR, 0, 0, "int32", "Tachometer Address", true, true},
  {KEY_SPINDLE_THRESHOLD, 0, 0, "int32", "Spindle Threshold (A)", true, true},
  
  // Status Light
  {KEY_STATUS_LIGHT_EN, 0, 0, "int32", "Status Light Enable (0/1)", true, true},
  {KEY_STATUS_LIGHT_GREEN, 0, 0, "int32", "Green Light Pin", true, true},
  {KEY_STATUS_LIGHT_YELLOW, 0, 0, "int32", "Yellow Light Pin", true, true},
  {KEY_STATUS_LIGHT_RED, 0, 0, "int32", "Red Light Pin", true, true},

  // Ethernet
  {KEY_ETH_ENABLED, 0, 0, "int32", "Ethernet Enable (0/1)", true, true},
  {KEY_ETH_DHCP, 0, 0, "int32", "Ethernet DHCP (0/1)", true, true},
  {KEY_ETH_IP, 0, 0, "string", "Ethernet IP", true, true},
  {KEY_ETH_GW, 0, 0, "string", "Ethernet Gateway", true, true},
  {KEY_ETH_MASK, 0, 0, "string", "Ethernet Mask", true, true},
  {KEY_ETH_DNS, 0, 0, "string", "Ethernet DNS", true, true},

  // Buzzer & Recovery
  {KEY_BUZZER_EN, 0, 0, "int32", "Buzzer Enable (0/1)", true, true},
  {KEY_BUZZER_PIN, 0, 0, "int32", "Buzzer Pin", true, true},
  {KEY_RECOV_INTERVAL, 0, 0, "int32", "Recovery Interval (lines)", true, true},

  // Spindle Advanced
  {KEY_SPINDL_PAUSE_EN, 0, 0, "int32", "Spindle Pause Enable", true, true},
  {KEY_SPINDL_PAUSE_THR, 0, 0, "float", "Spindle Pause Current", true, true},
  {KEY_SPINDL_TOOLBREAK_THR, 0, 0, "float", "Tool Break Threshold", true, true},
  {KEY_SPINDLE_POLL_MS, 0, 0, "int32", "Spindle Poll (ms)", true, true},
  {KEY_SPINDLE_RATED_AMPS, 0, 0, "float", "Rated Amps", true, true},
  {KEY_SPINDLE_RATED_RPM, 0, 0, "int32", "Rated RPM", true, true},
  {KEY_BLADE_DIAMETER_MM, 0, 0, "int32", "Blade Diameter (mm)", true, true},

  // VFD Calibration & Safety
  {KEY_VFD_IDLE_RMS, 0, 0, "float", "VFD Idle RMS", true, true},
  {KEY_VFD_IDLE_PEAK, 0, 0, "float", "VFD Idle Peak", true, true},
  {KEY_VFD_STD_CUT_RMS, 0, 0, "float", "VFD Std RMS", true, true},
  {KEY_VFD_STD_CUT_PEAK, 0, 0, "float", "VFD Std Peak", true, true},
  {KEY_VFD_HEAVY_RMS, 0, 0, "float", "VFD Heavy RMS", true, true},
  {KEY_VFD_HEAVY_PEAK, 0, 0, "float", "VFD Heavy Peak", true, true},
  {KEY_VFD_STALL_THR, 0, 0, "float", "VFD Stall Thr", true, true},
  {KEY_VFD_STALL_MARGIN, 0, 0, "int32", "VFD Stall Margin", true, true},
  {KEY_VFD_TEMP_WARN, 0, 0, "int32", "VFD Temp Warn", true, true},
  {KEY_VFD_TEMP_CRIT, 0, 0, "int32", "VFD Temp Crit", true, true},

  // Encoder & Bus
  {KEY_ENC_ERR_THRESHOLD, 0, 0, "float", "Encoder Err Thr", true, true},
  {KEY_ENC_DEV_TIMEOUT, 0, 0, "int32", "Encoder Dev Timeout", true, true},
  {KEY_ENC_FEEDBACK, 0, 0, "int32", "Encoder Feedback (0/1)", true, true},
  {KEY_ENC_PROTO, 0, 0, "int32", "Encoder Protocol", true, true},
  {KEY_ENC_ADDR, 0, 0, "int32", "Encoder Address", true, true},
  {KEY_RS485_BAUD, 0, 0, "int32", "RS485 Baud", true, true},
  {KEY_I2C_SPEED, 0, 0, "int32", "I2C Speed", true, true},
  
  // Homing & Positions
  {KEY_POS_SAFE_X, 0, 0, "float", "Safe X", true, true},
  {KEY_POS_SAFE_Y, 0, 0, "float", "Safe Y", true, true},
  {KEY_POS_SAFE_Z, 0, 0, "float", "Safe Z", true, true},
  {KEY_POS_SAFE_A, 0, 0, "float", "Safe A", true, true},
  {KEY_HOME_ENABLE, 0, 0, "int32", "Home Enable (0/1)", true, true},
  {KEY_HOME_PROFILE_FAST, 0, 0, "int32", "Home Fast Prof", true, true},
  {KEY_HOME_PROFILE_SLOW, 0, 0, "int32", "Home Slow Prof", true, true},

  // Web, OTA, LCD & Misc
  {KEY_WEB_USERNAME, 0, 0, "string", "Web User", true, true},
  {KEY_WEB_PASSWORD, 0, 0, "string", "Web Pass", true, true},
  {KEY_WEB_PORT, 0, 0, "int32", "Web Port", true, true},
  {KEY_WEB_AUTH_ENABLED, 0, 0, "int32", "Web Auth Enable", true, true},
  {KEY_OTA_PASSWORD, 0, 0, "string", "OTA Pass", true, true},
  {KEY_OTA_CHECK_EN, 0, 0, "int32", "OTA Check Enable", true, true},
  {KEY_LCD_EN, 0, 0, "int32", "LCD Enable (0/1)", true, true},
  {KEY_ALARM_PIN, 0, 0, "int32", "Alarm Pin", true, true},
  {KEY_BUTTONS_ENABLED, 0, 0, "int32", "Buttons Enable (0/1)", true, true},
  
  // Misc
  {KEY_CLI_ECHO, 0, 0, "int32", "CLI Echo (0/1)", true, true},
  {KEY_BOOTLOG_EN, 0, 0, "int32", "Bootlog Enable (0/1)", true, true},
  {KEY_RECOV_EN, 0, 0, "int32", "Recovery Enable (0/1)", true, true},

  {NULL, 0, 0, NULL, NULL, false, false}
};

void configSchemaVersioningInit() {
  logInfo("[SCHEMA] Initializing versioning...");
  if (!schema_prefs.begin("bisso_schema", false)) {
    logError("[SCHEMA] Init storage failed");
    return;
  }

  uint8_t stored_version = schema_prefs.getUChar("schema_version", CONFIG_SCHEMA_VERSION);
  logPrintf("[SCHEMA] Stored: v%d | Current: v%d\n", stored_version, CONFIG_SCHEMA_VERSION);

  if (stored_version != CONFIG_SCHEMA_VERSION) {
    logWarning("[SCHEMA] Version mismatch. Executing migration...");

    configMigrationInit();
    bool success = configMigrationExecute(stored_version, CONFIG_SCHEMA_VERSION);

    if (success) {
      logInfo("[SCHEMA] [OK] Migration successful");
      configMigrationShowReport();
    } else {
      logError("[SCHEMA] Migration failed");
      faultLogWarning(FAULT_CONFIGURATION_INVALID, "Schema migration failed");
    }
  } else {
    logInfo("[SCHEMA] [OK] Versions match");
  }
}

uint8_t configGetSchemaVersion() { return CONFIG_SCHEMA_VERSION; }
void configSetSchemaVersion(uint8_t version) { schema_prefs.putUChar("schema_version", version); }
uint8_t configGetStoredSchemaVersion() { return schema_prefs.getUChar("schema_version", CONFIG_SCHEMA_VERSION); }

const char* configGetSchemaDescription(uint8_t version) {
  for (size_t i = 0; i < sizeof(schema_history)/sizeof(schema_history[0]); i++) {
      if (schema_history[i].version == version) return schema_history[i].description;
  }
  return "Unknown";
}

void configShowSchemaHistory() {
  serialLoggerLock();
  logPrintln("\n=== SCHEMA HISTORY ===");
  for (size_t i = 0; i < sizeof(schema_history)/sizeof(schema_history[0]); i++) {
      logPrintf("v%d: %s (%s)\n", schema_history[i].version, schema_history[i].description, schema_history[i].changes);
  }
  serialLoggerUnlock();
}

bool configIsKeyActiveInVersion(const char* key, uint8_t version) {
  for (int i = 0; key_metadata[i].key != NULL; i++) {
    if (strcmp(key_metadata[i].key, key) == 0) {
      if (key_metadata[i].version_added > version) return false;
      if (key_metadata[i].version_removed != 0 && key_metadata[i].version_removed <= version) return false;
      return true;
    }
  }
  return false;
}

const char* configGetKeyMetadata(const char* key) {
  for (int i = 0; key_metadata[i].key != NULL; i++) {
    if (strcmp(key_metadata[i].key, key) == 0) return key_metadata[i].description;
  }
  return "Unknown";
}

const char* configGetKeyType(const char* key) {
  for (int i = 0; key_metadata[i].key != NULL; i++) {
    if (strcmp(key_metadata[i].key, key) == 0) return key_metadata[i].type;
  }
  return NULL;
}

migration_result_t configMigrateSchema(uint8_t from_version, uint8_t to_version) {
  migration_result_t result = {false, from_version, to_version, 0, 0, 0, ""};
  logPrintf("[SCHEMA] Migrating v%d -> v%d\n", from_version, to_version);
  
  migration_direction_t direction = (to_version > from_version) ? MIGRATION_UPGRADE : MIGRATION_DOWNGRADE;
  
  for (int i = 0; key_metadata[i].key != NULL; i++) {
    const char* key = key_metadata[i].key;
    
    if (!configIsKeyActiveInVersion(key, from_version)) {
      result.items_skipped++;
      continue;
    }
    
    if (direction == MIGRATION_UPGRADE && !key_metadata[i].migrate_forward) {
        result.items_skipped++;
        continue;
    }
    
    if (configIsKeyActiveInVersion(key, to_version)) {
        result.items_migrated++;
    } else {
        result.items_new++;
    }
  }
  
  configSetSchemaVersion(to_version);
  configUnifiedSave();
  
  result.success = true;
  snprintf(result.migration_log, 512, "Migrated: %lu items", (unsigned long)result.items_migrated);
  logInfo("[SCHEMA] [OK] Migration complete: %s", result.migration_log);
  
  return result;
}

migration_result_t configAutoMigrate() {
  uint8_t stored = configGetStoredSchemaVersion();
  return configMigrateSchema(stored, CONFIG_SCHEMA_VERSION);
}

bool configIsMigrationNeeded() {
  return configGetStoredSchemaVersion() != CONFIG_SCHEMA_VERSION;
}

bool configRollbackToVersion(uint8_t target_version) {
  if (target_version > CONFIG_SCHEMA_VERSION) { 
      logError("[SCHEMA] Target version too new");
      return false;
  }
  logPrintf("[SCHEMA] Rolling back to v%d\n", target_version);
  configMigrateSchema(CONFIG_SCHEMA_VERSION, target_version);
  return true;
}

void configShowMigrationStatus() {
  serialLoggerLock();
  logPrintln("\n=== MIGRATION STATUS ===");
  uint8_t stored = configGetStoredSchemaVersion();
  logPrintf("Stored: v%d | Current: v%d\r\n", stored, CONFIG_SCHEMA_VERSION);
  if (stored == CONFIG_SCHEMA_VERSION) logPrintln("Status: [SYNCED]");
  else if (stored < CONFIG_SCHEMA_VERSION) logPrintln("Status: [UPGRADE NEEDED]");
  else logPrintln("Status: [DOWNGRADE NEEDED]");
  serialLoggerUnlock();
}

void configValidateSchema() {
  logInfo("[SCHEMA] Validating...");
  uint8_t stored = configGetStoredSchemaVersion();
  if (stored > CONFIG_SCHEMA_VERSION) logWarning("[SCHEMA] Stored version newer");
  else logInfo("[SCHEMA] [OK] Validation passed");
}

void configShowKeyMetadata() {
  serialLoggerLock();
  logPrintln("\n=== KEY METADATA ===");
  for (int i = 0; key_metadata[i].key != NULL; i++) {
      logPrintf("%s (%s): %s\n", key_metadata[i].key, key_metadata[i].type, key_metadata[i].description);
  }
  serialLoggerUnlock();
}
