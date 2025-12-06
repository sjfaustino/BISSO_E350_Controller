#include "cli.h"
#include "serial_logger.h"
#include "boot_validation.h"
#include "memory_monitor.h"
#include "task_manager.h"
#include "system_utilities.h" 
#include "firmware_version.h" 
#include "gcode_parser.h" // <-- NEW: G-Code integration
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

// ============================================================================
// CLI STATE DEFINITIONS
// ============================================================================

static char cli_buffer[CLI_BUFFER_SIZE];
static uint16_t cli_pos = 0;
static cli_command_t commands[CLI_MAX_COMMANDS];
static int command_count = 0;

static char cli_history[CLI_HISTORY_SIZE][CLI_BUFFER_SIZE];
static int history_index = 0;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void cmd_help(int argc, char** argv);
void cmd_system_info(int argc, char** argv);
void cmd_system_reset(int argc, char** argv);

extern void bootShowStatus();      
extern void bootRebootSystem();    
extern uint32_t taskGetUptime();   

// ============================================================================
// CORE CLI FUNCTIONS
// ============================================================================

void cliInit() {
  Serial.println("[CLI] Initializing...");
  memset(cli_buffer, 0, sizeof(cli_buffer));
  memset(cli_history, 0, sizeof(cli_history));
  cli_pos = 0;
  command_count = 0;
  
  // Register Core Commands
  cliRegisterCommand("help", "Show help", cmd_help);
  cliRegisterCommand("info", "System information", cmd_system_info);
  cliRegisterCommand("reset", "System reset", cmd_system_reset);
  
  // Register Modules
  cliRegisterConfigCommands();
  cliRegisterMotionCommands();
  cliRegisterDiagCommands();
  cliRegisterCalibCommands();
  cliRegisterWifiCommands(); 
  
  // Initialize G-Code Engine
  gcodeParser.init();

  Serial.printf("[CLI] [OK] Ready (%d commands)\n", command_count);
  cliPrintPrompt();
}

void cliCleanup() {
  history_index = 0;
  memset(cli_history, 0, sizeof(cli_history));
  logInfo("CLI: History cleared.");
}

void cliUpdate() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        Serial.println(); 
        cliProcessCommand(cli_buffer);
        cli_pos = 0;
      } else {
        Serial.println();
      }
      cliPrintPrompt();
    } else if (c == '\b' || c == 0x7F) {
      if (cli_pos > 0) {
        cli_pos--;
        Serial.write('\b');
        Serial.write(' ');
        Serial.write('\b');
      }
    } else if (c >= 32 && c < 127 && cli_pos < CLI_BUFFER_SIZE - 1) {
      cli_buffer[cli_pos++] = c;
      Serial.write(c);
    }
  }
}

void cliProcessCommand(const char* cmd) {
  if (strlen(cmd) == 0) return;
  
  // --- G-Code Auto-Detection ---
  // If line starts with 'G' or 'M' followed by a digit, send to Parser
  // Example: "G1 X100" or "M2"
  if ((cmd[0] == 'G' || cmd[0] == 'M') && isdigit(cmd[1])) {
      if (gcodeParser.processCommand(cmd)) {
          // Command successfully handled by G-Code engine
          return; 
      }
      // If parser returns false (e.g. unknown code), we could fall through
      // or just print an error. For now, fall through to CLI to see if 
      // there is a CLI command named "G99" (unlikely but safe).
  }
  // ----------------------------------

  char cmd_copy[CLI_BUFFER_SIZE];
  strncpy(cmd_copy, cmd, CLI_BUFFER_SIZE - 1);
  cmd_copy[CLI_BUFFER_SIZE - 1] = '\0';
  
  char* argv[CLI_MAX_ARGS];
  int argc = 0;
  char* token = strtok(cmd_copy, " ");
  
  while (token != NULL && argc < CLI_MAX_ARGS) {
    argv[argc++] = token;
    token = strtok(NULL, " ");
  }
  
  if (argc == 0) return;
  
  if (history_index < CLI_HISTORY_SIZE) {
    strncpy(cli_history[history_index], cmd, CLI_BUFFER_SIZE - 1);
    cli_history[history_index][CLI_BUFFER_SIZE - 1] = '\0'; 
    history_index++;
  }
  
  // --- Custom Parsing for Multi-Word Commands ---
  
  // 1. "calibrate ppmm end"
  if (argc >= 3 && strcmp(argv[0], "calibrate") == 0 && strcmp(argv[1], "ppmm") == 0 && strcmp(argv[2], "end") == 0) {
      for (int i = 0; i < command_count; i++) {
        if (strcmp(commands[i].command, "calibrate ppmm end") == 0) {
          commands[i].handler(argc, argv);
          return;
        }
      }
  }
  
  // 2. "calibrate speed X reset"
  if (argc >= 4 && strcmp(argv[0], "calibrate") == 0 && (strcmp(argv[1], "speed") == 0 || strcmp(argv[1], "ppmm") == 0) && strcmp(argv[3], "reset") == 0) {
      if (axisCharToIndex(argv[2]) == 255) { 
          Serial.printf("[CLI] [ERR] Invalid axis: %s\n", argv[2]);
          return;
      }
      char full_cmd[32];
      snprintf(full_cmd, sizeof(full_cmd), "calibrate %s X reset", argv[1]);
      for (int i = 0; i < command_count; i++) {
        if (strcmp(commands[i].command, full_cmd) == 0) {
          commands[i].handler(argc, argv);
          return;
        }
      }
  }
  
  // 3. "calibrate speed" / "calibrate ppmm"
  if (argc >= 3 && strcmp(argv[0], "calibrate") == 0 && (strcmp(argv[1], "speed") == 0 || strcmp(argv[1], "ppmm") == 0)) {
      if (axisCharToIndex(argv[2]) == 255) { 
          Serial.printf("[CLI] [ERR] Invalid axis: %s\n", argv[2]);
          return;
      }
      if (strcmp(argv[1], "speed") == 0) {
          for (int i = 0; i < command_count; i++) {
            if (strcmp(commands[i].command, "calibrate speed") == 0) {
              commands[i].handler(argc, argv);
              return;
            }
          }
      } else { 
          for (int i = 0; i < command_count; i++) {
            if (strcmp(commands[i].command, "calibrate ppmm") == 0) {
              commands[i].handler(argc, argv);
              return;
            }
          }
      }
  }
  
  // 4. Default Parsing
  for (int i = 0; i < command_count; i++) {
    if (strcmp(commands[i].command, argv[0]) == 0) {
      commands[i].handler(argc, argv);
      return;
    }
  }
  
  Serial.printf("[CLI] Unknown command: '%s'. Type 'help'.\n", argv[0]);
}

bool cliRegisterCommand(const char* name, const char* help, cli_handler_t handler) {
  if (command_count >= CLI_MAX_COMMANDS) return false;
  commands[command_count].command = name;
  commands[command_count].help = help;
  commands[command_count].handler = handler;
  command_count++;
  return true;
}

void cliPrintHelp() {
  char ver_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(ver_str, sizeof(ver_str));

  Serial.printf("\n=== %s Commands ===\n", ver_str);
  for (int i = 0; i < command_count; i++) {
    Serial.print("  ");
    Serial.print(commands[i].command);
    int cmd_len = strlen(commands[i].command);
    int padding = (cmd_len < 20) ? (20 - cmd_len) : 1;
    for(int s=0; s < padding; s++) Serial.print(" ");
    Serial.print("- ");
    Serial.println(commands[i].help);
  }
  Serial.println();
}

void cliPrintPrompt() {
  Serial.print("> ");
}

// ============================================================================
// LOCAL COMMAND IMPLEMENTATIONS
// ============================================================================

void cmd_system_info(int argc, char** argv) {
  char version_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(version_str, sizeof(version_str));
  
  Serial.println("\n=== SYSTEM INFORMATION ===");
  Serial.printf("Firmware:  %s\n", version_str);
  Serial.printf("Platform:  ESP32-S3 (KC868-A16)\n");
  Serial.printf("Uptime:    %lu seconds\n", (unsigned long)taskGetUptime());
  
  bootShowStatus();
}

void cmd_system_reset(int argc, char** argv) {
  Serial.println("[CLI] System reset requested...");
  bootRebootSystem(); 
}

void cmd_help(int argc, char** argv) {
  cliPrintHelp();
}