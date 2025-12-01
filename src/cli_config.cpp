#include "cli.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_schema_versioning.h"
#include "config_validator.h"

// ============================================================================
// FORWARD DECLARATIONS (from other files)
// ============================================================================

void cmd_config_show(int argc, char** argv);
void cmd_config_reset(int argc, char** argv);
void cmd_config_save(int argc, char** argv);
void cmd_config_schema_show(int argc, char** argv);
void cmd_config_migrate(int argc, char** argv);
void cmd_config_rollback(int argc, char** argv);
void cmd_config_validate(int argc, char** argv);

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterConfigCommands() {
  cliRegisterCommand("config", "Show configuration", cmd_config_show);
  cliRegisterCommand("config_reset", "Reset config to defaults", cmd_config_reset);
  cliRegisterCommand("config_save", "Save configuration", cmd_config_save);
  cliRegisterCommand("config_schema", "Show schema history", cmd_config_schema_show);
  cliRegisterCommand("config_migrate", "Migrate configuration to new schema", cmd_config_migrate);
  cliRegisterCommand("config_rollback", "Rollback to previous schema", cmd_config_rollback);
  cliRegisterCommand("config_validate", "Validate configuration schema", cmd_config_validate);
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
  
  uint8_t target_version = atoi(argv[1]);
  configRollbackToVersion(target_version);
}

void cmd_config_validate(int argc, char** argv) {
  configValidateSchema();
  configValidatorRun(VALIDATOR_LEVEL_COMPREHENSIVE);
  configValidatorPrintReport();
}