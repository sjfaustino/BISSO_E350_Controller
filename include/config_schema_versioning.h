#ifndef CONFIG_SCHEMA_VERSIONING_H
#define CONFIG_SCHEMA_VERSIONING_H

#include <Arduino.h>

// Current schema version
#define CONFIG_SCHEMA_VERSION 2
#define CONFIG_SCHEMA_MIN_SUPPORTED 0

// Migration direction
typedef enum {
  MIGRATION_NONE = 0,
  MIGRATION_UPGRADE = 1,
  MIGRATION_DOWNGRADE = 2
} migration_direction_t;

// Migration result
typedef struct {
  bool success;                     // Migration completed successfully
  uint8_t from_version;             // Source schema version
  uint8_t to_version;               // Target schema version
  uint32_t items_migrated;          // Number of items migrated
  uint32_t items_skipped;           // Items skipped (obsolete)
  uint32_t items_new;               // New items added
  char migration_log[512];          // Migration log message
} migration_result_t;

// Schema change record
typedef struct {
  uint8_t version;                  // Schema version
  const char* description;          // Version description
  const char* changes;              // What changed
  uint32_t timestamp;               // When it was defined (Unix timestamp)
} schema_record_t;

// Configuration key metadata
typedef struct {
  const char* key;                  // Configuration key name
  uint8_t version_added;            // When key was added
  uint8_t version_removed;          // When key was removed (0 = still active)
  const char* type;                 // Data type (int32, float, string)
  const char* description;          // Human description
  bool migrate_forward;             // Migrate to newer versions
  bool migrate_backward;            // Migrate to older versions
} config_key_metadata_t;

// Initialize schema versioning
void configSchemaVersioningInit();

// Version management
uint8_t configGetSchemaVersion();
void configSetSchemaVersion(uint8_t version);
uint8_t configGetStoredSchemaVersion();

// Schema information
const char* configGetSchemaDescription(uint8_t version);
void configShowSchemaHistory();

// Migration
migration_result_t configMigrateSchema(uint8_t from_version, uint8_t to_version);
migration_result_t configAutoMigrate();
bool configIsMigrationNeeded();

// Backwards compatibility
bool configIsKeyActiveInVersion(const char* key, uint8_t version);
const char* configGetKeyMetadata(const char* key);

// Diagnostics
void configShowMigrationStatus();
void configShowKeyMetadata();
void configValidateSchema();

// Rollback
bool configRollbackToVersion(uint8_t target_version);

#endif