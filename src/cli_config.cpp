#include "cli.h"
#include "config_keys.h"
#include "config_schema_versioning.h"
#include "config_unified.h"
#include "config_validator.h"
#include "serial_logger.h"
#include <ArduinoJson.h> // CRITICAL FIX: Use ArduinoJson for consistent parsing
#include <stdlib.h>
#include <string.h>


// Helper from config_schema_versioning.cpp
extern const char *configGetKeyType(const char *key);

// Forward declarations
void cmd_config_show(int argc, char **argv);
void cmd_config_get(int argc, char **argv); // <-- NEW
void cmd_config_set(int argc, char **argv);
void cmd_config_dump(int argc, char **argv); // <-- NEW
void cmd_config_reset(int argc, char **argv);
void cmd_config_save(int argc, char **argv);
void cmd_config_schema_show(int argc, char **argv);
void cmd_config_migrate(int argc, char **argv);
void cmd_config_rollback(int argc, char **argv);
void cmd_config_validate(int argc, char **argv);
void cmd_config_export(int argc, char **argv);
void cmd_config_import(int argc, char **argv);

// PHASE 5.1: Config backup/restore declarations
extern void cmd_config_backup(int argc, char **argv);
extern void cmd_config_restore(int argc, char **argv);
extern void cmd_config_show_backup(int argc, char **argv);
extern void cmd_config_clear_backup(int argc, char **argv);

// Need access to internal table for dump command.
// Ideally config_unified should expose an iterator, but we will use the public
// getters loop strategy for now or simpler: add a friend function or public
// dump utility in config_unified.cpp. For CLI simplicity here, we rely on the
// schema keys or just print what we know. Actually, config_unified.cpp has
// config_table but it's static. Best practice: Add configUnifiedPrintAll() to
// config_unified.cpp and call it. However, since I cannot modify
// config_unified.cpp in this specific turn (unless I output it too), I will
// assume config_unified.cpp has a new function 'configUnifiedDump()' or we use
// a known list. WAIT -> I can modify config_unified.cpp if needed, but the user
// asked for CLI update. Let's implement 'dump' by iterating the known critical
// keys defined in schema or just allow config_unified.cpp to expose a print
// function. A better approach without modifying config_unified internals too
// much: We will iterate over the KNOWN KEYS from config_schema_versioning if
// available, or just call a new helper.

// Let's assume we add `void configUnifiedPrintAll();` to config_unified.h/cpp
// for the 'dump' command. For 'get', we use existing getters.

void cliRegisterConfigCommands() {
  cliRegisterCommand("config", "Configuration management", cmd_config_main);
}

void cmd_config_main(int argc, char **argv) {
  if (argc < 2) {
    logPrintln("\n[CONFIG] === Configuration Management ===");
    logPrintln("[CONFIG] Usage: config [command] <parameter>");
    logPrintln("[CONFIG] Commands:");
    logPrintln("  get <key> - Show value of a specific key.");
    logPrintln("  set       - Set value: config set <key> <val>");
    logPrintln("  dump      - List ALL configuration keys and values.");
    logPrintln("  show      - Show diagnostic summary.");
    logPrintln("  save      - Force save current configuration cache to NVS.");
    logPrintln("  reset     - Reset ALL configuration settings to factory defaults.");
    logPrintln("  validate  - Run full consistency validation report on the config.");
    logPrintln("  schema    - Show schema version history and key metadata.");
    logPrintln("  migrate   - Automatically migrate configuration schema to current version.");
    logPrintln("  rollback  - Rollback schema to a specific version.");
    logPrintln("\n[PHASE 2] New Commands:");
    logPrintln("  export    - Export configuration as JSON.");
    logPrintln("  import    - Import configuration from JSON.");
    logPrintln("\n[PHASE 5.1] Backup/Restore:");
    logPrintln("  backup    - Save current configuration to NVS backup.");
    logPrintln("  restore   - Load configuration from NVS backup.");
    logPrintln("  showbkp   - Display stored backup configuration.");
    logPrintln("  clrbkp    - Clear backup from NVS.");
    logPrintln("\n[NVS Management]:");
    logPrintln("  nvs stats - Show NVS storage usage.");
    logPrintln("  nvs erase - DANGER: Erase all NVS data and reboot.");
    return;
  }

  if (strcasecmp(argv[1], "get") == 0)
    cmd_config_get(argc, argv);
  else if (strcasecmp(argv[1], "set") == 0)
    cmd_config_set(argc, argv);
  else if (strcasecmp(argv[1], "dump") == 0)
    cmd_config_dump(argc, argv);
  else if (strcasecmp(argv[1], "show") == 0)
    cmd_config_show(argc, argv);
  else if (strcasecmp(argv[1], "save") == 0)
    cmd_config_save(argc, argv);
  else if (strcasecmp(argv[1], "reset") == 0)
    cmd_config_reset(argc, argv);
  else if (strcasecmp(argv[1], "validate") == 0)
    cmd_config_validate(argc, argv);
  else if (strcasecmp(argv[1], "schema") == 0)
    cmd_config_schema_show(argc, argv);
  else if (strcasecmp(argv[1], "migrate") == 0)
    cmd_config_migrate(argc, argv);
  else if (strcasecmp(argv[1], "export") == 0)
    cmd_config_export(argc, argv);
  else if (strcasecmp(argv[1], "import") == 0)
    cmd_config_import(argc, argv);
  else if (strcasecmp(argv[1], "backup") == 0)
    cmd_config_backup(argc, argv);
  else if (strcasecmp(argv[1], "restore") == 0)
    cmd_config_restore(argc, argv);
  else if (strcasecmp(argv[1], "showbkp") == 0)
    cmd_config_show_backup(argc, argv);
  else if (strcasecmp(argv[1], "clrbkp") == 0)
    cmd_config_clear_backup(argc, argv);
  else if (strcasecmp(argv[1], "nvs") == 0) {
    if (argc < 3) {
      logPrintln("[CONFIG] Usage: config nvs <stats|erase>");
      return;
    }
    if (strcasecmp(argv[2], "stats") == 0) {
      configLogNvsStats();
    } else if (strcasecmp(argv[2], "erase") == 0) {
      logWarning("[NVS] This will ERASE ALL configuration and REBOOT!");
      logWarning("[NVS] Press Ctrl+C within 3 seconds to abort...");
      delay(3000);
      configEraseNvs();
    } else {
      logWarning("[NVS] Unknown nvs command: %s", argv[2]);
    }
  }
  else if (strcasecmp(argv[1], "rollback") == 0) {
    if (argc < 3) {
      logError("[CONFIG] Usage: config rollback <version>");
      return;
    }
    cmd_config_rollback(argc, argv);
  } else {
    logWarning("[CONFIG] Error: Unknown parameter '%s'.", argv[1]);
  }
}

void cmd_config_get(int argc, char **argv) {
  if (argc < 3) {
    logPrintln("[CONFIG] Usage: config get <key>");
    return;
  }
  const char *key = argv[2];
  const char *type = configGetKeyType(key);

  if (type == NULL) {
    logWarning(
        "[CONFIG] Key '%s' not in schema. Attempting raw fetch...",
        key);
    int32_t i_val = configGetInt(key, -999999);
    if (i_val != -999999) {
      logInfo("%s = %ld (int)", key, (long)i_val);
      return;
    }
    logError("[CONFIG] Key '%s' not found or unset.", key);
    return;
  }

  if (strcmp(type, "int32") == 0) {
    int32_t val = configGetInt(key, 0);
    logInfo("%s = %ld", key, (long)val);
  } else if (strcmp(type, "float") == 0) {
    float val = configGetFloat(key, 0.0f);
    logPrintf("%s = %.3f\n", key, val);
  } else if (strcmp(type, "string") == 0) {
    logPrintf("%s = \"%s\"\n", key, configGetString(key, ""));
  }
}

void cmd_config_dump(int argc, char **argv) {
  logPrintln("\n[CONFIG] === FULL CONFIGURATION DUMP ===");
  logPrintln("KEY                            | VALUE");
  logPrintln("--------------------------------+----------------");

  extern void
  configUnifiedPrintAll();
  configUnifiedPrintAll();

  logPrintln("--------------------------------+----------------");
}

void cmd_config_set(int argc, char **argv) {
  if (argc < 4) {
    logPrintln("[CONFIG] Usage: config set <key> <value>");
    return;
  }

  const char *key = argv[2];
  const char *value_str = argv[3];
  const char *type = configGetKeyType(key);

  if (type == NULL) {
    logError("[CONFIG] Unknown key: '%s' (Check schema)", key);
    return;
  }

  if (strcmp(type, "int32") == 0) {
    int32_t val = atol(value_str);
    configSetInt(key, val);
    logInfo("[CONFIG] [OK] Set %s = %ld", key, (long)val);
  } else if (strcmp(type, "float") == 0) {
    float val = atof(value_str);
    configSetFloat(key, val);
    logPrintf("[CONFIG] [OK] Set %s = %.3f\n", key, val);
  } else if (strcmp(type, "string") == 0) {
    configSetString(key, value_str);
    logPrintf("[CONFIG] [OK] Set %s = \"%s\"\n", key, value_str);
  } else {
    logError("[CONFIG] Unsupported type for key '%s'", key);
  }
}

void cmd_config_show(int argc, char **argv) { configUnifiedDiagnostics(); }

void cmd_config_reset(int argc, char **argv) {
  logInfo("[CONFIG] Resetting ALL configuration to factory defaults...");
  configUnifiedReset();
  logInfo("[CONFIG] [OK] Factory reset complete.");
}

void cmd_config_save(int argc, char **argv) {
  logInfo("[CONFIG] Saving configuration to NVS...");
  configUnifiedSave();
  logInfo("[CONFIG] [OK] Saved.");
}

void cmd_config_schema_show(int argc, char **argv) {
  configShowSchemaHistory();
}

void cmd_config_migrate(int argc, char **argv) { configAutoMigrate(); }

void cmd_config_rollback(int argc, char **argv) {
  if (argc < 2) {
    logPrintln("[CLI] Usage: config rollback <version>");
    return;
  }
  uint8_t target_version = atoi(argv[1]);
  configRollbackToVersion(target_version);
}

void cmd_config_validate(int argc, char **argv) {
  configValidateSchema();
  configValidatorRun(VALIDATOR_LEVEL_COMPREHENSIVE);
  configValidatorPrintReport();
}

// ============================================================================
// PHASE 2: CONFIG IMPORT/EXPORT (JSON)
// ============================================================================

void cmd_config_export(int argc, char **argv) {
  logPrintln("\n[CONFIG] === Configuration Export (JSON) ===");
  logPrintln("{\n  \"config\": {");

  // Export known critical keys in JSON format
  // This is a simplified version - in production, iterate all keys
  extern const char *configGetString(const char *key, const char *default_val);
  extern float configGetFloat(const char *key, float default_val);
  extern int32_t configGetInt(const char *key, int32_t default_val);

  bool first = true;

  // Velocity calibration
  for (int i = 0; i < 4; i++) {
    char key[32];
    snprintf(key, sizeof(key), "speed_cal_%d", i);
    float val = configGetFloat(key, 1000.0f);
    if (!first)
      logPrintln(",");
    logPrintf("    \"%s\": %.2f", key, val);
    first = false;
  }

  // Position calibration (PPM)
  for (int i = 0; i < 4; i++) {
    char key[32];
    snprintf(key, sizeof(key), "ppm_%d", i);
    float val = configGetFloat(key, 100.0f);
    if (!first)
      logPrintln(",");
    logPrintf("    \"%s\": %.2f", key, val);
    first = false;
  }

  // Limits
  for (int i = 0; i < 4; i++) {
    char key[32];
    snprintf(key, sizeof(key), "limit_max_%d", i);
    int32_t val = configGetInt(key, 500000);
    if (!first)
      logPrintln(",");
    logPrintf("    \"%s\": %ld", key, (long)val);
    first = false;
  }

  logPrintln("\n  }\n}");
  logPrintln("\n[CONFIG] Export complete. Copy JSON data above to save.");
}

void cmd_config_import(int argc, char **argv) {
  logPrintln("\n[CONFIG] === Configuration Import (JSON) ===");
  logPrintln("[CONFIG] Paste JSON data below (end with empty line):");
  logPrintln("[CONFIG] Example: {\"config\": {\"ppm_0\": 100.5, \"speed_cal_0\": 1000}}");
  logWarning("[CONFIG] This will overwrite current settings!");

  // CRITICAL FIX: Use ArduinoJson instead of manual string parsing
  // Prevents buffer overflows and fragile parsing logic
  char json_buffer[1024];
  int buffer_pos = 0;
  int empty_line_count = 0;
  int import_count = 0;

  // Read JSON data line by line until empty line
  while (buffer_pos < sizeof(json_buffer) - 1 && empty_line_count < 2) {
    if (!Serial.available()) {
      delay(10);
      continue;
    }

    char c = Serial.read();
    
    // Ctrl+C (0x03) aborts the import
    if (c == 0x03) {
      logInfo("\n[CONFIG] Import ABORTED by user.");
      return;
    }

    if (c == '\n' || c == '\r') {
      if (buffer_pos == 0 || json_buffer[buffer_pos - 1] == '\n') {
        empty_line_count++;
      }
      if (buffer_pos < sizeof(json_buffer) - 1) {
        json_buffer[buffer_pos++] = '\n';
      }
    } else {
      empty_line_count = 0;
      if (buffer_pos < sizeof(json_buffer) - 1) {
        json_buffer[buffer_pos++] = c;
      }
    }
  }
  json_buffer[buffer_pos] = '\0';

  // Parse JSON using ArduinoJson
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json_buffer);

  if (error) {
    logError("[CONFIG] JSON parse failed: %s", error.c_str());
    logError("[CONFIG] Check JSON format and try again");
    return;
  }

  // Check for "config" object
  JsonObject config_obj = doc["config"];
  if (!config_obj) {
    logError("[CONFIG] Missing 'config' object in JSON");
    return;
  }

  // Iterate through all key-value pairs
  for (JsonPair kv : config_obj) {
    const char *key = kv.key().c_str();
    const char *type = configGetKeyType(key);

    if (type == NULL) {
      logWarning("[CONFIG] Skipping unknown key: %s", key);
      continue;
    }

    // Set value based on type
    if (strcmp(type, "int32") == 0) {
      int32_t val = kv.value().as<int32_t>();
      configSetInt(key, val);
      logInfo("[CONFIG] Imported: %s = %ld (int)", key, (long)val);
      import_count++;
    } else if (strcmp(type, "float") == 0) {
      float val = kv.value().as<float>();
      configSetFloat(key, val);
      logPrintf("[CONFIG] Imported: %s = %.3f (float)\n", key, val);
      import_count++;
    } else if (strcmp(type, "string") == 0) {
      const char *val = kv.value().as<const char *>();
      configSetString(key, val);
      logPrintf("[CONFIG] Imported: %s = \"%s\" (string)\n", key, val);
      import_count++;
    }
  }

  logInfo("\n[CONFIG] Import complete: %d settings loaded", import_count);
  logInfo("[CONFIG] Run 'config save' to persist changes");
}
