#include "cli.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_schema_versioning.h"
#include "config_validator.h"
#include <stdlib.h> 
#include <string.h> 

// Helper from config_schema_versioning.cpp
extern const char* configGetKeyType(const char* key);

// Forward declarations
void cmd_config_show(int argc, char** argv);
void cmd_config_set(int argc, char** argv); 
void cmd_config_reset(int argc, char** argv);
void cmd_config_save(int argc, char** argv);
void cmd_config_schema_show(int argc, char** argv);
void cmd_config_migrate(int argc, char** argv);
void cmd_config_rollback(int argc, char** argv);
void cmd_config_validate(int argc, char** argv);

void cliRegisterConfigCommands() {
  cliRegisterCommand("config", "Configuration management", cmd_config_main);
}

void cmd_config_main(int argc, char** argv) {
    if (argc < 2) { 
        Serial.println("\n[CONFIG] === Configuration Management ===");
        Serial.println("[CONFIG] Usage: config [command] <parameter>");
        Serial.println("[CONFIG] Commands:");
        Serial.println("  show      - Show current run-time settings and NVS cache.");
        Serial.println("  set       - Set value: config set <key> <val>");
        Serial.println("  save      - Force save current configuration cache to NVS.");
        Serial.println("  reset     - Reset ALL configuration settings to factory defaults.");
        Serial.println("  validate  - Run full consistency validation report on the config.");
        Serial.println("  schema    - Show schema version history and key metadata.");
        Serial.println("  migrate   - Automatically migrate configuration schema to current version.");
        Serial.println("  rollback <v>- Rollback schema to a specific version (e.g., rollback 0).");
        return;
    }

    if (strcmp(argv[1], "show") == 0) cmd_config_show(argc, argv);
    else if (strcmp(argv[1], "set") == 0) cmd_config_set(argc, argv);
    else if (strcmp(argv[1], "save") == 0) cmd_config_save(argc, argv);
    else if (strcmp(argv[1], "reset") == 0) cmd_config_reset(argc, argv);
    else if (strcmp(argv[1], "validate") == 0) cmd_config_validate(argc, argv);
    else if (strcmp(argv[1], "schema") == 0) cmd_config_schema_show(argc, argv);
    else if (strcmp(argv[1], "migrate") == 0) cmd_config_migrate(argc, argv);
    else if (strcmp(argv[1], "rollback") == 0) {
        if (argc < 3) {
             Serial.println("[CONFIG] [ERR] Usage: config rollback <version>");
             return;
        }
        cmd_config_rollback(argc, argv); 
    } else {
        Serial.printf("[CONFIG] Error: Unknown parameter '%s'.\n", argv[1]);
    }
}

void cmd_config_set(int argc, char** argv) {
    if (argc < 4) {
        Serial.println("[CONFIG] Usage: config set <key> <value>");
        Serial.println("[CONFIG] Example: config set motion_approach_mode 1");
        return;
    }
    
    const char* key = argv[2];
    const char* value_str = argv[3];
    
    const char* type = configGetKeyType(key);
    
    if (type == NULL) {
        Serial.printf("[CONFIG] [ERR] Unknown key: '%s'\n", key);
        return;
    }
    
    if (strcmp(type, "int32") == 0) {
        int32_t val = atol(value_str);
        configSetInt(key, val);
        Serial.printf("[CONFIG] [OK] Set %s = %ld\n", key, (long)val);
    } 
    else if (strcmp(type, "float") == 0) {
        float val = atof(value_str);
        configSetFloat(key, val);
        Serial.printf("[CONFIG] [OK] Set %s = %.3f\n", key, val);
    } 
    else if (strcmp(type, "string") == 0) {
        configSetString(key, value_str);
        Serial.printf("[CONFIG] [OK] Set %s = \"%s\"\n", key, value_str);
    } 
    else {
        Serial.printf("[CONFIG] [ERR] Unsupported type for key '%s'\n", key);
    }
}

void cmd_config_show(int argc, char** argv) {
  configUnifiedDiagnostics();
}

void cmd_config_reset(int argc, char** argv) {
  Serial.println("[CONFIG] Resetting ALL configuration to factory defaults...");
  configUnifiedReset();
  Serial.println("[CONFIG] [OK] Factory reset complete.");
}

void cmd_config_save(int argc, char** argv) {
  Serial.println("[CONFIG] Saving configuration to NVS...");
  configUnifiedSave();
  Serial.println("[CONFIG] [OK] Saved.");
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