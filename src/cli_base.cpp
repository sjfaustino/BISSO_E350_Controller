#include "cli.h"
#include "serial_logger.h"
#include "boot_validation.h"
#include "memory_monitor.h"
#include "task_manager.h"
#include "system_utilities.h" 
#include "firmware_version.h" 
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

// ============================================================================
// CLI STATE DEFINITIONS
// ============================================================================

// Local state variables use the canonical definitions from cli.h
static char cli_buffer[CLI_BUFFER_SIZE];
static uint16_t cli_pos = 0;
static cli_command_t commands[CLI_MAX_COMMANDS];
static int command_count = 0;

// FIX: Changed from char*[] (pointers to heap) to static 2D array (BSS/Data segment)
// This eliminates malloc() entirely from the CLI history logic.
static char cli_history[CLI_HISTORY_SIZE][CLI_BUFFER_SIZE]; 
static int history_index = 0;

// ============================================================================
// FORWARD DECLARATIONS (for local commands)
// ============================================================================

void cmd_help(int argc, char** argv);
void cmd_system_info(int argc, char** argv);
void cmd_system_reset(int argc, char** argv);

// External functions needed
extern void bootShowStatus();      
extern void bootRebootSystem();    
extern uint32_t taskGetUptime();   

// ============================================================================
// CORE CLI FUNCTIONS
// ============================================================================

void cliInit() {
  Serial.println("[CLI] Command Line Interface initializing...");
  memset(cli_buffer, 0, sizeof(cli_buffer));
  
  // FIX: Clear static history buffer
  memset(cli_history, 0, sizeof(cli_history));
  
  cli_pos = 0;
  command_count = 0;
  
  // Register core commands
  cliRegisterCommand("help", "Show help", cmd_help);
  cliRegisterCommand("info", "System information", cmd_system_info);
  cliRegisterCommand("reset", "System reset", cmd_system_reset);
  
  // Register commands from modular files 
  cliRegisterConfigCommands();
  cliRegisterMotionCommands();
  cliRegisterDiagCommands();
  cliRegisterCalibCommands();
  cliRegisterWifiCommands(); 
  
  Serial.print("[CLI] Registered ");
  Serial.print(command_count);
  Serial.println(" commands");
  cliPrintPrompt();
}

void cliCleanup() {
  // FIX: No free() required for static arrays.
  // Just reset the index and clear memory to be safe.
  history_index = 0;
  memset(cli_history, 0, sizeof(cli_history));
  logInfo("CLI: History cleared (Static buffer reset)");
}

void cliUpdate() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        Serial.println(); // Echo newline
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
  
  // History management (FIX: Static Allocation)
  if (history_index < CLI_HISTORY_SIZE) {
    // Store command in static array instead of malloc
    // Using strncpy to prevent buffer overflows
    strncpy(cli_history[history_index], cmd, CLI_BUFFER_SIZE - 1);
    cli_history[history_index][CLI_BUFFER_SIZE - 1] = '\0'; // Ensure null-termination
    history_index++;
  } else {
    // Optional: Implement ring buffer logic here if desired later
    // For now, we just stop recording when full, as per original logic logic
  }
  
  // Custom multi-word command parsing logic to handle "calibrate speed X ..."
  
  // 1. Check for "calibrate ppmm end"
  if (argc >= 3 && strcmp(argv[0], "calibrate") == 0 && strcmp(argv[1], "ppmm") == 0 && strcmp(argv[2], "end") == 0) {
      for (int i = 0; i < command_count; i++) {
        if (strcmp(commands[i].command, "calibrate ppmm end") == 0) {
          commands[i].handler(argc, argv);
          return;
        }
      }
  }
  
  // 2. Check for "calibrate speed X reset" and "calibrate ppmm X reset" (4 words total)
  if (argc >= 4 && strcmp(argv[0], "calibrate") == 0 && (strcmp(argv[1], "speed") == 0 || strcmp(argv[1], "ppmm") == 0) && strcmp(argv[3], "reset") == 0) {
      
      // We must validate the axis argument before building the command string match
      if (axisCharToIndex(argv[2]) == 255) { 
          Serial.print("[CLI] Error: Invalid axis argument: "); Serial.println(argv[2]);
          return;
      }
      
      char full_cmd[32];
      // Reconstruct the registered command string format
      snprintf(full_cmd, sizeof(full_cmd), "calibrate %s X reset", argv[1]);
      
      for (int i = 0; i < command_count; i++) {
        if (strcmp(commands[i].command, full_cmd) == 0) {
          commands[i].handler(argc, argv);
          return;
        }
      }
  }
  
  // 3. Check for 3-word commands like "calibrate speed" (followed by axis and distance/profile)
  if (argc >= 3 && strcmp(argv[0], "calibrate") == 0 && (strcmp(argv[1], "speed") == 0 || strcmp(argv[1], "ppmm") == 0)) {
      
      if (axisCharToIndex(argv[2]) == 255) { 
          Serial.print("[CLI] Error: Invalid axis argument: "); Serial.println(argv[2]);
          return;
      }
      
      if (strcmp(argv[1], "speed") == 0) {
          for (int i = 0; i < command_count; i++) {
            if (strcmp(commands[i].command, "calibrate speed") == 0) {
              commands[i].handler(argc, argv);
              return;
            }
          }
      } else { // ppmm
          for (int i = 0; i < command_count; i++) {
            if (strcmp(commands[i].command, "calibrate ppmm") == 0) {
              commands[i].handler(argc, argv);
              return;
            }
          }
      }
  }
  
  // 4. Default parsing (for single-word commands like 'move', 'info', 'wifi', 'i2c')
  for (int i = 0; i < command_count; i++) {
    if (strcmp(commands[i].command, argv[0]) == 0) {
      commands[i].handler(argc, argv);
      return;
    }
  }
  
  Serial.print("[CLI] Unknown command: ");
  Serial.println(argv[0]);
  Serial.println("[CLI] Type 'help' for available commands");
}

bool cliRegisterCommand(const char* name, const char* help, cli_handler_t handler) {
  if (command_count >= CLI_MAX_COMMANDS) {
    return false;
  }
  
  commands[command_count].command = name;
  commands[command_count].help = help;
  commands[command_count].handler = handler;
  command_count++;
  
  return true;
}

void cliPrintHelp() {
  Serial.println("\n=== BISSO v4.2 Commands ===\n");
  for (int i = 0; i < command_count; i++) {
    // Basic formatting for readability
    Serial.print("  ");
    Serial.print(commands[i].command);
    
    // Align help text
    int cmd_len = strlen(commands[i].command);
    if (cmd_len < 20) {
        for(int s=0; s < (20 - cmd_len); s++) Serial.print(" ");
    } else {
        Serial.print(" ");
    }
    
    Serial.print("- ");
    Serial.println(commands[i].help);
  }
  Serial.println("");
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
  
  Serial.println("\n=== System Information ===");
  
  Serial.print("Firmware: ");
  Serial.println(version_str); 
  
  Serial.print("Platform: ESP32-S3 (KC868-A16)\n");
  Serial.print("Uptime:   ");
  Serial.print(taskGetUptime()); 
  Serial.println(" seconds\n");
  
  bootShowStatus();
}

void cmd_system_reset(int argc, char** argv) {
  Serial.println("[CLI] System reset requested (software restart)...");
  bootRebootSystem(); 
}

void cmd_help(int argc, char** argv) {
  cliPrintHelp();
}