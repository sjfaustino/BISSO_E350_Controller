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
  {KEY_X_APPROACH, 1, 0, "int32", "X Final Approach", true, true},
  {KEY_MOTION_DEADBAND, 1, 0, "int32", "Motion Deadband", true, true},
  {KEY_MOTION_APPROACH_MODE, 1, 0, "int32", "Approach Mode", true, true},

  // NEW in v2.0
  {KEY_MOTION_BUFFER_ENABLE, 2, 0, "int32", "Enable Motion Queue (0/1)", true, true},
  {KEY_WIFI_SSID, 2, 0, "string", "WiFi SSID", true, true},
  {KEY_WIFI_PASSWORD, 2, 0, "string", "WiFi Password", true, true},
  {KEY_WIFI_AP_EN, 2, 0, "int32", "WiFi AP Enable (0/1)", true, true},
  {KEY_WIFI_AP_SSID, 2, 0, "string", "WiFi AP SSID", true, true},
  {KEY_WIFI_AP_PASS, 2, 0, "string", "WiFi AP Password", true, true},

  // NEW in v3.0
  {KEY_ENC_BAUD, 3, 0, "int32", "Encoder Baud Rate", true, true},
  {KEY_ENC_INTERFACE, 3, 0, "int32", "Encoder Interface (0=RS232, 1=RS485)", true, true},

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
  Serial.println("\n=== SCHEMA HISTORY ===");
  for (size_t i = 0; i < sizeof(schema_history)/sizeof(schema_history[0]); i++) {
      Serial.printf("v%d: %s (%s)\n", schema_history[i].version, schema_history[i].description, schema_history[i].changes);
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
  Serial.println("\n=== MIGRATION STATUS ===");
  uint8_t stored = configGetStoredSchemaVersion();
  Serial.printf("Stored: v%d | Current: v%d\n", stored, CONFIG_SCHEMA_VERSION);
  if (stored == CONFIG_SCHEMA_VERSION) Serial.println("Status: [SYNCED]");
  else if (stored < CONFIG_SCHEMA_VERSION) Serial.println("Status: [UPGRADE NEEDED]");
  else Serial.println("Status: [DOWNGRADE NEEDED]");
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
  Serial.println("\n=== KEY METADATA ===");
  for (int i = 0; key_metadata[i].key != NULL; i++) {
      Serial.printf("%s (%s): %s\n", key_metadata[i].key, key_metadata[i].type, key_metadata[i].description);
  }
  serialLoggerUnlock();
}
