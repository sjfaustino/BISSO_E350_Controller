#include "config_schema_versioning.h"
#include "config_unified.h"
#include "config_keys.h" 
#include "fault_logging.h"
#include <Preferences.h>
#include <string.h>

static Preferences schema_prefs;
static uint8_t current_schema_version = CONFIG_SCHEMA_VERSION;

static const schema_record_t schema_history[] = {
  {0, "Initial schema (v0.1)", "Base motion control", 0},
  {1, "Enhanced schema (v4.2)", "Speed calibration, NVS persistence", 1700000000}
};

static const config_key_metadata_t key_metadata[] = {
  {KEY_X_LIMIT_MIN, 0, 0, "int32", "X min limit", true, true},
  {KEY_X_LIMIT_MAX, 0, 0, "int32", "X max limit", true, true},
  {KEY_Y_LIMIT_MIN, 0, 0, "int32", "Y min limit", true, true},
  {KEY_Y_LIMIT_MAX, 0, 0, "int32", "Y max limit", true, true},
  {KEY_Z_LIMIT_MIN, 0, 0, "int32", "Z min limit", true, true},
  {KEY_Z_LIMIT_MAX, 0, 0, "int32", "Z max limit", true, true},
  {KEY_A_LIMIT_MIN, 0, 0, "int32", "A min limit", true, true},
  {KEY_A_LIMIT_MAX, 0, 0, "int32", "A max limit", true, true},
  
  {KEY_DEFAULT_SPEED, 0, 0, "float", "Default speed", true, true},
  {KEY_DEFAULT_ACCEL, 0, 0, "float", "Default accel", true, true},
  
  {KEY_SPEED_CAL_X, 1, 0, "float", "Calib X Speed", true, false},
  {KEY_SPEED_CAL_Y, 1, 0, "float", "Calib Y Speed", true, false},
  {KEY_SPEED_CAL_Z, 1, 0, "float", "Calib Z Speed", true, false},
  {KEY_SPEED_CAL_A, 1, 0, "float", "Calib A Speed", true, false},
  
  {KEY_PPM_X, 0, 0, "int32", "X PPM", true, true},
  {KEY_PPM_Y, 0, 0, "int32", "Y PPM", true, true},
  {KEY_PPM_Z, 0, 0, "int32", "Z PPM", true, true},
  {KEY_PPM_A, 0, 0, "int32", "A PPM", true, true},
  
  {KEY_STALL_TIMEOUT, 0, 0, "int32", "Stall Timeout", true, true},
  {KEY_X_APPROACH, 1, 0, "int32", "X Final Approach", true, true},
  {KEY_MOTION_DEADBAND, 1, 0, "int32", "Motion Deadband", true, true},

  {NULL, 0, 0, NULL, NULL, false, false}
};

void configSchemaVersioningInit() {
  Serial.println("[SCHEMA] Initializing versioning...");
  if (!schema_prefs.begin("bisso_schema", false)) {
    Serial.println("[SCHEMA] [FAIL] Init storage failed");
    return;
  }
  
  uint8_t stored_version = schema_prefs.getUChar("schema_version", CONFIG_SCHEMA_VERSION);
  Serial.printf("[SCHEMA] Stored: v%d | Current: v%d\n", stored_version, CONFIG_SCHEMA_VERSION);
  
  if (stored_version != CONFIG_SCHEMA_VERSION) {
    Serial.println("[SCHEMA] [WARN] Version mismatch. Migrating...");
    configAutoMigrate();
  } else {
    Serial.println("[SCHEMA] [OK] Versions match");
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
  Serial.println("\n=== SCHEMA HISTORY ===");
  for (size_t i = 0; i < sizeof(schema_history)/sizeof(schema_history[0]); i++) {
      Serial.printf("v%d: %s (%s)\n", schema_history[i].version, schema_history[i].description, schema_history[i].changes);
  }
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

migration_result_t configMigrateSchema(uint8_t from_version, uint8_t to_version) {
  migration_result_t result = {false, from_version, to_version, 0, 0, 0, ""};
  Serial.printf("[SCHEMA] Migrating v%d -> v%d\n", from_version, to_version);
  
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
        // V0->V1 Upgrade Logic
        if (from_version == 0 && to_version == 1 && strcmp(key, KEY_DEFAULT_SPEED) == 0) {
             float old_spd = configGetFloat(KEY_DEFAULT_SPEED, 15.0f);
             configSetFloat(KEY_SPEED_CAL_X, old_spd);
             configSetFloat(KEY_SPEED_CAL_Y, old_spd);
             configSetFloat(KEY_SPEED_CAL_Z, old_spd);
             configSetFloat(KEY_SPEED_CAL_A, old_spd);
        }
        result.items_migrated++;
    } else {
        result.items_new++;
    }
  }
  
  configSetSchemaVersion(to_version);
  configUnifiedSave();
  
  result.success = true;
  snprintf(result.migration_log, 512, "Migrated: %lu items", result.items_migrated);
  Serial.printf("[SCHEMA] [OK] Migration complete: %s\n", result.migration_log);
  
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
  if (target_version < CONFIG_SCHEMA_MIN_SUPPORTED) {
      Serial.println("[SCHEMA] [ERR] Target version too old");
      return false;
  }
  Serial.printf("[SCHEMA] Rolling back to v%d\n", target_version);
  configMigrateSchema(CONFIG_SCHEMA_VERSION, target_version);
  return true;
}

void configShowMigrationStatus() {
  Serial.println("\n=== MIGRATION STATUS ===");
  uint8_t stored = configGetStoredSchemaVersion();
  Serial.printf("Stored: v%d | Current: v%d\n", stored, CONFIG_SCHEMA_VERSION);
  if (stored == CONFIG_SCHEMA_VERSION) Serial.println("Status: [SYNCED]");
  else if (stored < CONFIG_SCHEMA_VERSION) Serial.println("Status: [UPGRADE NEEDED]");
  else Serial.println("Status: [DOWNGRADE NEEDED]");
}

void configValidateSchema() {
  Serial.println("[SCHEMA] Validating...");
  uint8_t stored = configGetStoredSchemaVersion();
  if (stored > CONFIG_SCHEMA_VERSION) Serial.println("[SCHEMA] [WARN] Stored version newer");
  else Serial.println("[SCHEMA] [OK] Validation passed");
}

void configShowKeyMetadata() {
  Serial.println("\n=== KEY METADATA ===");
  for (int i = 0; key_metadata[i].key != NULL; i++) {
      Serial.printf("%s (%s): %s\n", key_metadata[i].key, key_metadata[i].type, key_metadata[i].description);
  }
}