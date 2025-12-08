/**
 * @file cli_base.cpp
 * @brief CLI Core with Grbl 1.1 Compatibility Layer (Gemini v3.5.20)
 * @details Implements UGS/Grbl handshake, status reporting, and flow control.
 */

#include "cli.h"
#include "serial_logger.h"
#include "boot_validation.h"
#include "memory_monitor.h"
#include "task_manager.h"
#include "system_utilities.h" 
#include "firmware_version.h" 
#include "gcode_parser.h"
#include "motion.h"
#include "motion_state.h"
#include "motion_planner.h" // For getFeedOverride
#include "config_unified.h"
#include "config_keys.h"
#include "safety.h"
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
void cmd_grbl_settings(int argc, char** argv); // <-- NEW: $$ Handler

extern void bootShowStatus();      
extern void bootRebootSystem();    
extern uint32_t taskGetUptime();   

// ============================================================================
// CORE CLI FUNCTIONS
// ============================================================================

void cliInit() {
  // GRBL HANDSHAKE: This specific string tells UGS we are ready.
  Serial.println("\r\nGrbl 1.1h ['$' for help]");
  
  memset(cli_buffer, 0, sizeof(cli_buffer));
  memset(cli_history, 0, sizeof(cli_history));
  cli_pos = 0;
  command_count = 0;
  
  // Register Core Commands
  cliRegisterCommand("help", "Show help", cmd_help);
  cliRegisterCommand("info", "System information", cmd_system_info);
  cliRegisterCommand("reset", "System reset", cmd_system_reset);
  
  // Register Grbl Settings Command
  cliRegisterCommand("$", "Grbl Settings Report", cmd_grbl_settings);
  
  // Register Modules
  cliRegisterConfigCommands();
  cliRegisterMotionCommands();
  cliRegisterDiagCommands();
  cliRegisterCalibCommands();
  cliRegisterWifiCommands(); 
  
  // Initialize G-Code Engine
  gcodeParser.init();
}

void cliCleanup() {
  history_index = 0;
  memset(cli_history, 0, sizeof(cli_history));
  logInfo("CLI: History cleared.");
}

void cliUpdate() {
  while (Serial.available() > 0) {
    char c = Serial.peek(); // Peek to check for real-time commands

    // --- 1. Real-time Grbl Status Report (?) ---
    if (c == '?') {
        Serial.read(); // Consume
        
        // Map internal state to Grbl state strings
        const char* state_str = "Idle";
        if (motionIsEmergencyStopped()) state_str = "Alarm";
        else if (safetyIsAlarmed()) state_str = "Hold"; // or Alarm depending on severity
        else if (motionIsMoving()) state_str = "Run";
        else if (motionGetState(0) == MOTION_HOMING_APPROACH_FAST) state_str = "Home";

        // Format: <State|MPos:x,y,z|FS:feed,0>
        // Note: UGS expects MPos (Machine Position) or WPos (Work Position). 
        // We report MPos based on raw encoder values converted to mm.
        Serial.printf("<%s|MPos:%.3f,%.3f,%.3f|FS:%.0f,0>\r\n",
            state_str,
            motionGetPositionMM(0),
            motionGetPositionMM(1),
            motionGetPositionMM(2),
            // Grbl standard is 3 axis, add A if your UGS profile supports it:
            // ,motionGetPositionMM(3) 
            motionPlanner.getFeedOverride() * 100.0f // Current Feed (approx)
        );
        return; // Done handling ?
    }

    // --- 2. Soft Reset (Ctrl+X) ---
    if (c == 0x18) { 
        Serial.read();
        motionEmergencyStop();
        Serial.print("\r\nGrbl 1.1h ['$' for help]\r\n");
        // Clear buffer
        cli_pos = 0;
        memset(cli_buffer, 0, sizeof(cli_buffer));
        return;
    }

    // --- 3. Standard Input Processing ---
    c = Serial.read(); // Actually consume now

    if (c == '\n' || c == '\r') {
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        // Echo not usually needed for UGS, but good for humans
        // Serial.println(); 
        cliProcessCommand(cli_buffer);
        cli_pos = 0;
      } else {
        // Empty line (Enter key) -> Grbl expects 'ok'
        Serial.println("ok"); 
      }
      // cliPrintPrompt(); // Disable prompt for machine-readable mode
    } else if (c == '\b' || c == 0x7F) {
      if (cli_pos > 0) {
        cli_pos--;
        Serial.write('\b'); Serial.write(' '); Serial.write('\b');
      }
    } else if (c >= 32 && c < 127 && cli_pos < CLI_BUFFER_SIZE - 1) {
      cli_buffer[cli_pos++] = c;
      // Serial.write(c); // Local echo
    }
  }
}

void cliProcessCommand(const char* cmd) {
  if (strlen(cmd) == 0) {
      Serial.println("ok");
      return;
  }
  
  // --- G-Code / Grbl Auto-Detection ---
  // If line starts with 'G', 'M', or '$' (and isn't just '$' which is handled by CLI table), handle it.
  bool is_gcode = (cmd[0] == 'G' || cmd[0] == 'M');
  bool is_setting = (cmd[0] == '$' && isdigit(cmd[1])); // e.g., $100=400

  if (is_gcode) {
      if (gcodeParser.processCommand(cmd)) {
          Serial.println("ok"); // CRITICAL: Acknowledge execution
      } else {
          Serial.println("error:20"); // Unsupported command
      }
      return;
  }

  // Handle Grbl Setting writes (e.g. $100=400.5)
  if (is_setting) {
      // Simple parser for $100=val
      int setting_id = atoi(cmd + 1);
      char* eq = strchr((char*)cmd, '=');
      if (eq) {
          float val = atof(eq + 1);
          // Map Grbl ID to Internal Key
          const char* key = NULL;
          switch(setting_id) {
              case 100: key = KEY_PPM_X; break;
              case 101: key = KEY_PPM_Y; break;
              case 102: key = KEY_PPM_Z; break;
              case 110: key = KEY_SPEED_CAL_X; break;
              case 111: key = KEY_SPEED_CAL_Y; break;
              case 112: key = KEY_SPEED_CAL_Z; break;
              // Add more mappings as needed
          }
          
          if (key) {
              configSetFloat(key, val);
              Serial.println("ok");
          } else {
              Serial.println("error:3"); // Invalid Value/ID
          }
      }
      return;
  }

  // --- Standard CLI Commands ---
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
  
  if (argc == 0) { Serial.println("ok"); return; }
  
  // Execute Internal Command
  bool handled = false;
  for (int i = 0; i < command_count; i++) {
    if (strcmp(commands[i].command, argv[0]) == 0) {
      commands[i].handler(argc, argv);
      handled = true;
      // For Grbl compatibility, even internal commands should probably return 'ok'
      // if executed successfully, but UGS ignores lines starting with [ brackets usually.
      // We print 'ok' just in case UGS sent a custom command.
      Serial.println("ok"); 
      break;
    }
  }
  
  if (!handled) {
      // If it wasn't G-Code and wasn't a CLI command -> Error
      Serial.println("error:1"); // G-code Command Expected
  }
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
  Serial.println("[HLP:$$ $# $G $I $N $x=val $Nx=line Gcode]");
}

void cliPrintPrompt() {
  // No prompt in Grbl mode usually, creates noise
}

// ============================================================================
// COMMAND IMPLEMENTATIONS
// ============================================================================

// Implementation of '$$' command to satisfy UGS settings view
void cmd_grbl_settings(int argc, char** argv) {
    // Map Gemini Config to Grbl Standards
    Serial.printf("$100=%.3f\r\n", configGetFloat(KEY_PPM_X, 100.0)); // X steps/mm
    Serial.printf("$101=%.3f\r\n", configGetFloat(KEY_PPM_Y, 100.0)); // Y steps/mm
    Serial.printf("$102=%.3f\r\n", configGetFloat(KEY_PPM_Z, 100.0)); // Z steps/mm
    Serial.printf("$110=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_X, 1000.0)); // X Max rate
    Serial.printf("$111=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_Y, 1000.0)); // Y Max rate
    Serial.printf("$112=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_Z, 1000.0)); // Z Max rate
    Serial.printf("$120=%.3f\r\n", configGetFloat(KEY_DEFAULT_ACCEL, 100.0)); // X Accel
    Serial.printf("$121=%.3f\r\n", configGetFloat(KEY_DEFAULT_ACCEL, 100.0)); // Y Accel
    Serial.printf("$122=%.3f\r\n", configGetFloat(KEY_DEFAULT_ACCEL, 100.0)); // Z Accel
    Serial.printf("$130=%.3f\r\n", (float)configGetInt(KEY_X_LIMIT_MAX, 500000) / configGetFloat(KEY_PPM_X, 1.0)); // X Max travel
    Serial.printf("$131=%.3f\r\n", (float)configGetInt(KEY_Y_LIMIT_MAX, 500000) / configGetFloat(KEY_PPM_Y, 1.0)); // Y Max travel
    Serial.printf("$132=%.3f\r\n", (float)configGetInt(KEY_Z_LIMIT_MAX, 500000) / configGetFloat(KEY_PPM_Z, 1.0)); // Z Max travel
    Serial.println("ok");
}

void cmd_system_info(int argc, char** argv) {
  char version_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(version_str, sizeof(version_str));
  Serial.printf("[VER:1.1h.Gemini:%s]\r\n", version_str);
  Serial.println("ok");
}

void cmd_system_reset(int argc, char** argv) {
  bootRebootSystem(); 
}

void cmd_help(int argc, char** argv) {
  cliPrintHelp();
}