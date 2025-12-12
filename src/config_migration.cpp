/**
 * @file config_migration.cpp
 * @brief PHASE 2.5 Configuration Migration Framework
 *
 * Provides automatic configuration migration when schema versions change.
 * Handles key initialization with defaults and post-migration validation.
 */

#include "config_migration.h"
#include "config_unified.h"
#include "config_schema_versioning.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include <string.h>

// ============================================================================
// MIGRATION STATE
// ============================================================================

static migration_stats_t last_migration_stats = {0, 0, 0, 0, 0};
static bool migration_initialized = false;

// Default values for new configuration keys
// These are applied when upgrading to a version that adds new keys
static const config_default_t default_values[] = {
  // Add defaults for any new keys introduced in future versions
  // Format: {KEY_NAME, "default_value", "type", required}
  // Examples:
  // {KEY_NEW_PARAM_V2, "100", "int32", true},
  // {KEY_NEW_SPEED_V3, "2.5", "float", true},

  {NULL, NULL, NULL, false}  // Sentinel
};

// ============================================================================
// INITIALIZATION
// ============================================================================

void configMigrationInit() {
  if (migration_initialized) return;

  migration_initialized = true;
  memset(&last_migration_stats, 0, sizeof(migration_stats_t));

  logInfo("[MIGRATION] Config migration framework initialized");
}

// ============================================================================
// MIGRATION EXECUTION
// ============================================================================

bool configMigrationExecute(uint8_t from_version, uint8_t to_version) {
  if (!migration_initialized) configMigrationInit();

  uint32_t start_time = millis();
  logInfo("[MIGRATION] Executing migration: v%d -> v%d", from_version, to_version);

  // Reset statistics
  memset(&last_migration_stats, 0, sizeof(migration_stats_t));

  // Phase 1: Schema migration (handled by existing system)
  migration_result_t schema_result = configMigrateSchema(from_version, to_version);

  if (!schema_result.success) {
    logError("[MIGRATION] Schema migration failed");
    last_migration_stats.errors++;
    return false;
  }

  last_migration_stats.keys_migrated = schema_result.items_migrated;

  // Phase 2: Apply defaults for new keys
  if (to_version > from_version) {  // Upgrade only
    if (!configMigrationApplyDefaults(to_version)) {
      logWarning("[MIGRATION] Some defaults failed to apply");
      last_migration_stats.errors++;
    }
  }

  // Phase 3: Validate configuration
  if (!configMigrationValidate()) {
    logError("[MIGRATION] Post-migration validation failed");
    last_migration_stats.errors++;
    faultLogWarning(FAULT_CONFIGURATION_INVALID, "Config validation failed after migration");
    return false;
  }

  last_migration_stats.migration_time_ms = millis() - start_time;
  logInfo("[MIGRATION] [OK] Migration complete in %lu ms",
          (unsigned long)last_migration_stats.migration_time_ms);

  return true;
}

// ============================================================================
// DEFAULT VALUE APPLICATION
// ============================================================================

bool configMigrationApplyDefaults(uint8_t to_version) {
  logInfo("[MIGRATION] Applying defaults for v%d", to_version);

  uint32_t applied = 0;

  // Iterate through default values and apply any that are for this version
  for (int i = 0; default_values[i].key != NULL; i++) {
    const char* key = default_values[i].key;

    // Check if key is active in target version
    if (!configIsKeyActiveInVersion(key, to_version)) {
      continue;
    }

    // Check if key already exists (don't overwrite)
    if (configHasKey(key)) {
      last_migration_stats.keys_validated++;
      continue;
    }

    // Apply default value based on type
    bool success = false;

    if (strcmp(default_values[i].type, "int32") == 0) {
      int32_t val = atoi(default_values[i].value);
      success = configSetInt32(key, val);
    } else if (strcmp(default_values[i].type, "float") == 0) {
      float val = atof(default_values[i].value);
      success = configSetFloat(key, val);
    } else if (strcmp(default_values[i].type, "string") == 0) {
      success = configSetString(key, default_values[i].value);
    }

    if (success) {
      applied++;
      last_migration_stats.keys_initialized++;
      logInfo("[MIGRATION] Applied default: %s = %s", key, default_values[i].value);
    } else {
      logWarning("[MIGRATION] Failed to apply default: %s", key);
      if (default_values[i].required) {
        last_migration_stats.errors++;
      }
    }
  }

  if (applied > 0) {
    configUnifiedSave();
  }

  return (last_migration_stats.errors == 0);
}

// ============================================================================
// POST-MIGRATION VALIDATION
// ============================================================================

bool configMigrationValidate() {
  logInfo("[MIGRATION] Validating configuration");

  uint32_t valid = 0;
  uint32_t invalid = 0;

  // Check required defaults
  for (int i = 0; default_values[i].key != NULL; i++) {
    if (!default_values[i].required) continue;

    const char* key = default_values[i].key;
    uint8_t current_version = configGetSchemaVersion();

    if (!configIsKeyActiveInVersion(key, current_version)) {
      continue;
    }

    if (!configHasKey(key)) {
      logError("[MIGRATION] Required key missing: %s", key);
      invalid++;
    } else {
      valid++;
    }
  }

  last_migration_stats.keys_validated = valid;

  return (invalid == 0);
}

// ============================================================================
// RECOVERY & DIAGNOSTICS
// ============================================================================

bool configMigrationRecovery() {
  logWarning("[MIGRATION] Attempting recovery with factory defaults");

  // This would reset configuration to factory defaults
  // For now, just log and validate
  return configMigrationValidate();
}

migration_stats_t configMigrationGetStats() {
  return last_migration_stats;
}

void configMigrationShowReport() {
  Serial.println("\n=== MIGRATION REPORT ===");
  Serial.printf("Keys Migrated: %lu\n", (unsigned long)last_migration_stats.keys_migrated);
  Serial.printf("Keys Initialized: %lu\n", (unsigned long)last_migration_stats.keys_initialized);
  Serial.printf("Keys Validated: %lu\n", (unsigned long)last_migration_stats.keys_validated);
  Serial.printf("Errors: %lu\n", (unsigned long)last_migration_stats.errors);
  Serial.printf("Time: %lu ms\n", (unsigned long)last_migration_stats.migration_time_ms);
  Serial.println();
}

// ============================================================================
// INTEGRATION HELPERS
// ============================================================================

// These functions should be called from config_unified to check key existence
// Stubbed here - actual implementation in config_unified
extern bool configHasKey(const char* key);
extern bool configSetInt32(const char* key, int32_t value);
extern bool configSetFloat(const char* key, float value);
extern bool configSetString(const char* key, const char* value);
extern void configUnifiedSave();
