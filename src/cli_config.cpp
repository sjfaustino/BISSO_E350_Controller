#include "cli.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_schema_versioning.h"
#include "config_validator.h"
#include <stdlib.h> // For atoi
#include <string.h> // For strcmp

// ============================================================================
// FORWARD DECLARATIONS (from other files/self)
// ============================================================================

void cmd_config_show(int argc, char** argv);
void cmd_config_reset(int argc, char** argv);
void cmd_config_save(int argc, char** argv);
void cmd_config_schema_show(int argc, char** argv);
void cmd_config_migrate(int argc, char** argv);
void cmd_config_rollback(int argc, char** argv);
void cmd_config_validate(int argc, char** argv);

// ============================================================================
// REGISTRATION (Standard)
// ============================================================================

void cliRegisterConfigCommands() {
  // NOTE: In the consolidated model (using cmd_config_main as dispatcher),
  // these individual registrations are deprecated but often left for tool compatibility.
  cliRegisterCommand("config", "Show configuration", cmd_config_show);
  cliRegisterCommand("config_reset", "Reset config to defaults", cmd_config_reset);
  cliRegisterCommand("config_save", "Save configuration", cmd_config_save);
  cliRegisterCommand("config_schema", "Show schema history", cmd_config_schema_show);
  cliRegisterCommand("config_migrate", "Migrate configuration to new schema", cmd_config_migrate);
  cliRegisterCommand("config_rollback", "Rollback to previous schema", cmd_config_rollback);
  cliRegisterCommand("config_validate", "Validate configuration schema", cmd_config_validate);
}

// ============================================================================
// CONFIGURATION MAIN DISPATCHER (FIX: Defined here)
// ============================================================================

void cmd_config_main(int argc, char** argv) {
    if (argc < 2) { // Check if only the command name is present
        Serial.println("\n[CONFIG] === Configuration Management ===");
        Serial.println("[CONFIG] Usage: config [command] <parameter>");
        Serial.println("[CONFIG] Commands:");
        Serial.println("  show      - Show current run-time settings and NVS cache.");
        Serial.println("  save      - Force save current configuration cache to NVS.");
        Serial.println("  reset     - Reset ALL configuration settings to factory defaults.");
        Serial.println("  validate  - Run full consistency validation report on the config.");
        Serial.println("  schema    - Show schema version history and key metadata.");
        Serial.println("  migrate   - Automatically migrate configuration schema to current version.");
        Serial.println("  rollback <v>- Rollback schema to a specific version (e.g., rollback 0).");
        return;
    }

    if (strcmp(argv[1], "show") == 0) {
        cmd_config_show(argc, argv);
    } else if (strcmp(argv[1], "save") == 0) {
        cmd_config_save(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        cmd_config_reset(argc, argv);
    } else if (strcmp(argv[1], "validate") == 0) {
        cmd_config_validate(argc, argv);
    } else if (strcmp(argv[1], "schema") == 0) {
        cmd_config_schema_show(argc, argv);
    } else if (strcmp(argv[1], "migrate") == 0) {
        cmd_config_migrate(argc, argv);
    } else if (strcmp(argv[1], "rollback") == 0) {
        if (argc < 3) {
             Serial.println("[CONFIG] ERROR: Rollback requires a target version number (e.g., config rollback 0).");
             return;
        }
        cmd_config_rollback(argc, argv); 
    } else {
        Serial.printf("[CONFIG] Error: Unknown parameter '%s'. Use 'config' for help.\n", argv[1]);
    }
}


// ============================================================================
// COMMAND IMPLEMENTATIONS
// ============================================================================

void cmd_config_show(int argc, char** argv) {
  configUnifiedDiagnostics();
}

void cmd_config_reset(int argc, char** argv) {
  Serial.println("[CONFIG] Resetting ALL configuration to factory defaults...");
  configUnifiedReset();
  Serial.println("[CONFIG] ✅ Factory reset complete - all defaults saved to NVS");
}

void cmd_config_save(int argc, char** argv) {
  Serial.println("[CONFIG] Ensuring all configuration is saved to NVS...");
  configUnifiedSave();
  Serial.println("[CONFIG] ✅ All configuration verified and saved to NVS");
}

void cmd_config_schema_show(int argc, char** argv) {
  configShowSchemaHistory();
}

void cmd_config_migrate(int argc, char** argv) {
  configAutoMigrate();
}

void cmd_config_rollback(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("[CLI] Usage: config_rollback <version>");
    return;
  }
  
  // NOTE: Using argv[1] directly here because cmd_config_main ensures 
  // argv[1] is the parameter (if called via that dispatcher).
  uint8_t target_version = atoi(argv[1]);
  configRollbackToVersion(target_version);
}

void cmd_config_validate(int argc, char** argv) {
  configValidateSchema();
  configValidatorRun(VALIDATOR_LEVEL_COMPREHENSIVE);
  configValidatorPrintReport();
}