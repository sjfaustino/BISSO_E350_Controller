#include "config_schema_versioning.h"
#include "config_unified.h"
#include "fault_logging.h"
#include <Preferences.h>
#include <string.h>

static Preferences schema_prefs;
static uint8_t current_schema_version = CONFIG_SCHEMA_VERSION;

// Schema history for documentation/tracking
static const schema_record_t schema_history[] = {
  {0, "Initial schema (v0.1)", "Base motion control", 0},
  {1, "Enhanced schema (v4.2)", "Speed calibration, NVS persistence", 1700000000}
};

// Metadata defining the lifespan and type of every config key
static const config_key_metadata_t key_metadata[] = {
  {"x_soft_limit_min", 0, 0, "int32", "X min limit", true, true},
  {"x_soft_limit_max", 0, 0, "int32", "X max limit", true, true},
  {"y_soft_limit_min", 0, 0, "int32", "Y min limit", true, true},
  {"y_soft_limit_max", 0, 0, "int32", "Y max limit", true, true},
  {"z_soft_limit_min", 0, 0, "int32", "Z min limit", true, true},
  {"z_soft_limit_max", 0, 0, "int32", "Z max limit", true, true},
  {"a_soft_limit_min", 0, 0, "int32", "A min limit", true, true},
  {"a_soft_limit_max", 0, 0, "int32", "A max limit", true, true},
  {"default_speed_mm_s", 0, 0, "float", "Default speed", true, true},
  {"default_acceleration", 0, 0, "float", "Default accel", true, true},
  {"speed_X_mm_s", 1, 0, "float", "Calib X Speed", true, false},
  {"speed_Y_mm_s", 1, 0, "float", "Calib Y Speed", true, false},
  {"speed_Z_mm_s", 1, 0, "float", "Calib Z Speed", true, false},
  {"speed_A_mm_s", 1, 0, "float", "Calib A Speed", true, false},
  {"encoder_ppm_x", 0, 0, "int32", "X PPM", true, true},
  {"encoder_ppm_y", 0, 0, "int32", "Y PPM", true, true},
  {"encoder_ppm_z", 0, 0, "int32", "Z PPM", true, true},
  {"encoder_ppm_a", 0, 0, "int32", "A PPM", true, true},
  {"stall_timeout_ms", 0, 0, "int32", "Stall Timeout", true, true},
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

uint8_t configGetSchemaVersion() {
  return CONFIG_SCHEMA_VERSION;
}

void configSetSchemaVersion(uint8_t version) {
  schema_prefs.putUChar("schema_version", version);
  current_schema_version = version;
}

uint8_t configGetStoredSchemaVersion() {
  return schema_prefs.getUChar("schema_version", CONFIG_SCHEMA_VERSION);
}

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
    
    // Skip keys not relevant to source version
    if (!configIsKeyActiveInVersion(key, from_version)) {
      result.items_skipped++;
      continue;
    }
    
    // Check migration flags
    if (direction == MIGRATION_UPGRADE && !key_metadata[i].migrate_forward) {
        result.items_skipped++;
        continue;
    }
    
    // Simple migration: Ensure key exists in NVS, set default if new
    if (configIsKeyActiveInVersion(key, to_version)) {
        // V0->V1 Upgrade Logic for Speed
        if (from_version == 0 && to_version == 1 && strcmp(key, "default_speed_mm_s") == 0) {
             float old_spd = configGetFloat("default_speed_mm_s", 15.0f);
             configSetFloat("speed_X_mm_s", old_spd);
             configSetFloat("speed_Y_mm_s", old_spd);
             configSetFloat("speed_Z_mm_s", old_spd);
             configSetFloat("speed_A_mm_s", old_spd);
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

void configShowKeyMetadata() {
  Serial.println("\n=== KEY METADATA ===");
  for (int i = 0; key_metadata[i].key != NULL; i++) {
      Serial.printf("%s (%s): %s\n", key_metadata[i].key, key_metadata[i].type, key_metadata[i].description);
  }
}

void configValidateSchema() {
  Serial.println("[SCHEMA] Validating...");
  uint8_t stored = configGetStoredSchemaVersion();
  if (stored > CONFIG_SCHEMA_VERSION) {
      Serial.println("[SCHEMA] [WARN] Stored version newer than firmware");
  } else if (stored < CONFIG_SCHEMA_MIN_SUPPORTED) {
      Serial.println("[SCHEMA] [FAIL] Stored version obsolete");
      faultLogError(FAULT_CONFIGURATION_INVALID, "Schema obsolete");
  } else {
      Serial.println("[SCHEMA] [OK] Validation passed");
  }
}