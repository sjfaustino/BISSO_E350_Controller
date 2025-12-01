#include "config_schema_versioning.h"
#include "config_unified.h"
#include "fault_logging.h"
#include <Preferences.h>
#include <string.h>

static Preferences schema_prefs;
static uint8_t current_schema_version = CONFIG_SCHEMA_VERSION;

// Schema history and descriptions
static const schema_record_t schema_history[] = {
  {
    .version = 0,
    .description = "Initial schema (v0.1)",
    .changes = "Initial release with basic motion control",
    .timestamp = 0
  },
  {
    .version = 1,
    .description = "Enhanced schema (v4.2)",
    .changes = "Added speed calibration, NVS persistence, I²C recovery",
    .timestamp = 1700000000
  }
};

// Key metadata table - defines when keys were added/removed and how to migrate
static const config_key_metadata_t key_metadata[] = {
  // Motion limits - X axis
  {
    .key = "x_soft_limit_min",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "X axis minimum soft limit",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "x_soft_limit_max",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "X axis maximum soft limit",
    .migrate_forward = true,
    .migrate_backward = true
  },
  
  // Motion limits - Y axis
  {
    .key = "y_soft_limit_min",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "Y axis minimum soft limit",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "y_soft_limit_max",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "Y axis maximum soft limit",
    .migrate_forward = true,
    .migrate_backward = true
  },
  
  // Motion limits - Z axis
  {
    .key = "z_soft_limit_min",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "Z axis minimum soft limit",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "z_soft_limit_max",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "Z axis maximum soft limit",
    .migrate_forward = true,
    .migrate_backward = true
  },
  
  // Motion limits - A axis
  {
    .key = "a_soft_limit_min",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "A axis minimum soft limit",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "a_soft_limit_max",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "A axis maximum soft limit",
    .migrate_forward = true,
    .migrate_backward = true
  },
  
  // Motion parameters - v0
  {
    .key = "default_speed_mm_s",
    .version_added = 0,
    .version_removed = 0,
    .type = "float",
    .description = "Default motion speed in mm/s",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "default_acceleration",
    .version_added = 0,
    .version_removed = 0,
    .type = "float",
    .description = "Default acceleration in mm/s²",
    .migrate_forward = true,
    .migrate_backward = true
  },
  
  // Speed calibrations - NEW in v1
  {
    .key = "speed_X_mm_s",
    .version_added = 1,
    .version_removed = 0,
    .type = "float",
    .description = "Calibrated X axis speed in mm/s",
    .migrate_forward = true,
    .migrate_backward = false 
  },
  {
    .key = "speed_Y_mm_s",
    .version_added = 1,
    .version_removed = 0,
    .type = "float",
    .description = "Calibrated Y axis speed in mm/s",
    .migrate_forward = true,
    .migrate_backward = false
  },
  {
    .key = "speed_Z_mm_s",
    .version_added = 1,
    .version_removed = 0,
    .type = "float",
    .description = "Calibrated Z axis speed in mm/s",
    .migrate_forward = true,
    .migrate_backward = false
  },
  {
    .key = "speed_A_mm_s",
    .version_added = 1,
    .version_removed = 0,
    .type = "float",
    .description = "Calibrated A axis speed in mm/s",
    .migrate_forward = true,
    .migrate_backward = false
  },
  
  // Encoder settings - v0
  {
    .key = "encoder_ppm_x",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "X axis pulses per mm",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "encoder_ppm_y",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "Y axis pulses per mm",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "encoder_ppm_z",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "Z axis pulses per mm",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "encoder_ppm_a",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "A axis pulses per mm",
    .migrate_forward = true,
    .migrate_backward = true
  },
  
  // Safety settings - v0
  {
    .key = "alarm_pin",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "GPIO pin for alarm output",
    .migrate_forward = true,
    .migrate_backward = true
  },
  {
    .key = "stall_timeout_ms",
    .version_added = 0,
    .version_removed = 0,
    .type = "int32",
    .description = "Motion stall detection timeout in ms",
    .migrate_forward = true,
    .migrate_backward = true
  },
  
  // Terminator
  {
    .key = NULL,
    .version_added = 0,
    .version_removed = 0,
    .type = NULL,
    .description = NULL,
    .migrate_forward = false,
    .migrate_backward = false
  }
};

void configSchemaVersioningInit() {
  Serial.println("[SCHEMA] Configuration schema versioning system initializing...");
  
  if (!schema_prefs.begin("bisso_schema", false)) {
    Serial.println("[SCHEMA] ERROR: Failed to initialize schema storage!");
    return;
  }
  
  // Check stored version
  uint8_t stored_version = schema_prefs.getUChar("schema_version", CONFIG_SCHEMA_VERSION);
  Serial.print("[SCHEMA] Stored schema version: ");
  Serial.println(stored_version);
  
  Serial.print("[SCHEMA] Current schema version: ");
  Serial.println(CONFIG_SCHEMA_VERSION);
  
  // Check if migration is needed
  if (stored_version != CONFIG_SCHEMA_VERSION) {
    Serial.println("[SCHEMA] ⚠️  Schema version mismatch - migration needed!");
    configAutoMigrate();
  } else {
    Serial.println("[SCHEMA] ✅ Schema versions match");
  }
  
  Serial.println("[SCHEMA] ✅ Schema versioning system ready");
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
  for (int i = 0; i < sizeof(schema_history) / sizeof(schema_history[0]); i++) {
    if (schema_history[i].description == NULL) break;
    if (schema_history[i].version == version) {
      return schema_history[i].description;
    }
  }
  return "UNKNOWN";
}

void configShowSchemaHistory() {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║           CONFIGURATION SCHEMA HISTORY & VERSIONS              ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  for (int i = 0; i < sizeof(schema_history) / sizeof(schema_history[0]); i++) {
    if (schema_history[i].description == NULL) break;
    
    Serial.print("[SCHEMA v");
    Serial.print(schema_history[i].version);
    Serial.print("] ");
    Serial.println(schema_history[i].description);
    
    Serial.print("  Changes: ");
    Serial.println(schema_history[i].changes);
    Serial.println();
  }
}

bool configIsKeyActiveInVersion(const char* key, uint8_t version) {
  for (int i = 0; key_metadata[i].key != NULL; i++) {
    if (strcmp(key_metadata[i].key, key) == 0) {
      // Check if key was added before this version
      if (key_metadata[i].version_added > version) {
        return false;
      }
      
      // Check if key was removed before this version
      if (key_metadata[i].version_removed != 0 && key_metadata[i].version_removed <= version) {
        return false;
      }
      
      return true;
    }
  }
  
  return false;
}

const char* configGetKeyMetadata(const char* key) {
  for (int i = 0; key_metadata[i].key != NULL; i++) {
    if (strcmp(key_metadata[i].key, key) == 0) {
      return key_metadata[i].description;
    }
  }
  return "UNKNOWN";
}

migration_result_t configMigrateSchema(uint8_t from_version, uint8_t to_version) {
  migration_result_t result = {false, from_version, to_version, 0, 0, 0, ""};
  
  Serial.println("[SCHEMA] ===== CONFIGURATION MIGRATION =====");
  Serial.print("[SCHEMA] FROM schema v");
  Serial.print(from_version);
  Serial.print(" TO schema v");
  Serial.println(to_version);
  
  migration_direction_t direction = (to_version > from_version) ? MIGRATION_UPGRADE : MIGRATION_DOWNGRADE;
  
  // Iterate through all keys
  for (int i = 0; key_metadata[i].key != NULL; i++) {
    const char* key = key_metadata[i].key;
    
    // Check if key is active in source version
    if (!configIsKeyActiveInVersion(key, from_version)) {
      result.items_skipped++;
      continue;
    }
    
    // Check if key should be migrated in this direction
    if (direction == MIGRATION_UPGRADE && !key_metadata[i].migrate_forward) {
      result.items_skipped++;
      continue;
    }
    
    if (direction == MIGRATION_DOWNGRADE && !key_metadata[i].migrate_backward) {
      result.items_skipped++;
      continue;
    }
    
    // Check if key is active in target version
    bool active_in_target = configIsKeyActiveInVersion(key, to_version);
    
    if (!active_in_target && direction == MIGRATION_UPGRADE) {
      // Key is new in target version - set defaults
      result.items_new++;
    } else if (active_in_target) {
      // Key exists in both - migrate it
      
      // *** FIX: IMPLEMENT MIGRATION LOGIC (V0 -> V1 UPGRADE) ***
      
      if (direction == MIGRATION_UPGRADE && from_version == 0 && to_version == 1) {
          if (strcmp(key, "default_speed_mm_s") == 0) {
              // Copy old default speed value to new calibrated slots (X, Y, Z, A)
              float old_speed = configGetFloat("default_speed_mm_s", 15.0f); 
              configSetFloat("speed_X_mm_s", old_speed);
              configSetFloat("speed_Y_mm_s", old_speed);
              configSetFloat("speed_Z_mm_s", old_speed);
              configSetFloat("speed_A_mm_s", old_speed);
              // Note: The new speed_X/Y/Z/A keys are counted via the 'items_new' path
          }
      }
      
      // General migration step: Force read from NVS to update the unified cache 
      // (This ensures all old keys are loaded and re-saved under the new version structure)
      if (strcmp(key_metadata[i].type, "int32") == 0) {
          int32_t val = configGetInt(key, 0); 
          configSetInt(key, val);
      } else if (strcmp(key_metadata[i].type, "float") == 0) {
          float val = configGetFloat(key, 0.0f);
          configSetFloat(key, val);
      }
      
      result.items_migrated++;
    } else {
      result.items_skipped++;
    }
  }
  
  // Update stored version
  configSetSchemaVersion(to_version);
  
  // Force a full save after migration to persist new keys/values
  configUnifiedSave();
  
  result.success = true;
  snprintf(result.migration_log, sizeof(result.migration_log),
    "Migrated: %lu, New: %lu, Skipped: %lu",
    result.items_migrated, result.items_new, result.items_skipped);
  
  Serial.print("[SCHEMA] ✅ Migration complete: ");
  Serial.println(result.migration_log);
  
  return result;
}

migration_result_t configAutoMigrate() {
  uint8_t stored = configGetStoredSchemaVersion();
  uint8_t current = CONFIG_SCHEMA_VERSION;
  
  if (stored == current) {
    migration_result_t result = {true, current, current, 0, 0, 0, "No migration needed"};
    return result;
  }
  
  if (stored > current) {
    Serial.println("[SCHEMA] WARNING: Stored version is newer than current!");
    faultLogWarning(FAULT_CONFIGURATION_INVALID, "Stored schema version newer than current");
  }
  
  return configMigrateSchema(stored, current);
}

bool configIsMigrationNeeded() {
  uint8_t stored = configGetStoredSchemaVersion();
  uint8_t current = CONFIG_SCHEMA_VERSION;
  return stored != current;
}

bool configRollbackToVersion(uint8_t target_version) {
  if (target_version < CONFIG_SCHEMA_MIN_SUPPORTED) {
    Serial.print("[SCHEMA] ERROR: Target version ");
    Serial.print(target_version);
    Serial.print(" is below minimum supported version ");
    Serial.println(CONFIG_SCHEMA_MIN_SUPPORTED);
    return false;
  }
  
  Serial.print("[SCHEMA] Rolling back to version ");
  Serial.println(target_version);
  
  // Rollback implementation relies on configMigrateSchema running backward logic
  configMigrateSchema(CONFIG_SCHEMA_VERSION, target_version);
  return true;
}

void configShowMigrationStatus() {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║            CONFIGURATION MIGRATION STATUS                      ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  uint8_t stored = configGetStoredSchemaVersion();
  uint8_t current = CONFIG_SCHEMA_VERSION;
  
  Serial.print("[SCHEMA] Current Version: ");
  Serial.println(current);
  
  Serial.print("[SCHEMA] Stored Version: ");
  Serial.println(stored);
  
  if (stored == current) {
    Serial.println("[SCHEMA] Status: ✅ IN SYNC");
  } else if (stored < current) {
    Serial.print("[SCHEMA] Status: ⚠️  NEEDS UPGRADE (v");
    Serial.print(stored);
    Serial.print(" → v");
    Serial.print(current);
    Serial.println(")");
    
    Serial.println("[SCHEMA] Action: Run 'config_migrate' to upgrade");
  } else {
    Serial.println("[SCHEMA] Status: ❌ DOWNGRADE NEEDED");
    Serial.println("[SCHEMA] Warning: System is newer than stored config");
  }
  
  Serial.println();
}

void configShowKeyMetadata() {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║           CONFIGURATION KEY METADATA                           ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("[KEYS] Configuration Keys by Version:\n");
  
  // Show keys by version
  for (uint8_t v = 0; v <= CONFIG_SCHEMA_VERSION; v++) {
    Serial.print("[v");
    Serial.print(v);
    Serial.println("]");
    
    bool found = false;
    for (int i = 0; key_metadata[i].key != NULL; i++) {
      if (key_metadata[i].version_added == v) {
        found = true;
        Serial.print("  + ");
        Serial.print(key_metadata[i].key);
        Serial.print(" (");
        Serial.print(key_metadata[i].type);
        Serial.print("): ");
        Serial.println(key_metadata[i].description);
      }
    }
    
    if (!found) {
      Serial.println("  (no new keys)");
    }
    Serial.println();
  }
}

void configValidateSchema() {
  Serial.println("[SCHEMA] Validating configuration schema...");
  
  uint8_t stored = configGetStoredSchemaVersion();
  uint8_t issues = 0;
  
  // Check stored version validity
  if (stored > CONFIG_SCHEMA_VERSION) {
    Serial.println("[SCHEMA] ⚠️  WARNING: Stored version newer than current");
    issues++;
  }
  
  if (stored < CONFIG_SCHEMA_MIN_SUPPORTED) {
    Serial.println("[SCHEMA] ❌ ERROR: Stored version below minimum supported");
    issues++;
  }
  
  if (issues == 0) {
    Serial.println("[SCHEMA] ✅ Schema validation passed");
  } else {
    Serial.print("[SCHEMA] ❌ Found ");
    Serial.print(issues);
    Serial.println(" issue(s)");
    faultLogError(FAULT_CONFIGURATION_INVALID, "Schema validation failed");
  }
}