#ifndef CONFIG_MIGRATION_H
#define CONFIG_MIGRATION_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// PHASE 2.5: CONFIG MIGRATION FRAMEWORK
// ============================================================================

// Configuration default values for automatic initialization
typedef struct {
  const char* key;
  const char* value;          // String representation for any type
  const char* type;           // "int32", "float", "string"
  bool required;              // Must exist after migration
} config_default_t;

// Migration statistics
typedef struct {
  uint32_t keys_migrated;
  uint32_t keys_initialized;
  uint32_t keys_validated;
  uint32_t errors;
  uint32_t migration_time_ms;
} migration_stats_t;

// ============================================================================
// CONFIG MIGRATION API
// ============================================================================

// Initialize migration framework
void configMigrationInit();

// Perform migration with default value application
// Returns true if successful
bool configMigrationExecute(uint8_t from_version, uint8_t to_version);

// Apply default values for new configuration keys
// Called after schema upgrade to initialize missing keys
bool configMigrationApplyDefaults(uint8_t to_version);

// Validate configuration after migration
// Checks that all required keys exist and have valid values
bool configMigrationValidate();

// Get migration statistics (last migration)
migration_stats_t configMigrationGetStats();

// Automatic recovery: reset to factory defaults if needed
bool configMigrationRecovery();

// Show migration report
void configMigrationShowReport();

#endif
