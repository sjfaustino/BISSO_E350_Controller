/**
 * @file cli_base.cpp
 * @brief CLI Core with Full Grbl 1.1 Compatibility (Gemini v3.5.22)
 * @details Implements UGS handshake, Real-time Overrides (!/~/?), Jogging ($J), and standard CLI.
 * @author Sergio Faustino
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
#include "motion_planner.h" 
#include "motion_buffer.h" 
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
void cmd_grbl_settings(int argc, char** argv);
void cmd_grbl_home(int argc, char** argv);
void cmd_grbl_state(int argc, char** argv);

extern void bootShowStatus();      
extern void bootRebootSystem();    
extern uint32_t taskGetUptime();   

// ============================================================================
// CORE CLI FUNCTIONS
// ============================================================================

void cliInit() {
  // GRBL HANDSHAKE: Required for UGS to recognize the controller
  Serial.println("\r\nGrbl 1.1h ['$' for help]");
  
  memset(cli_buffer, 0, sizeof(cli_buffer));
  memset(cli_history, 0, sizeof(cli_history));
  cli_pos = 0;
  command_count = 0;
  
  // Register Core Commands
  cliRegisterCommand("help", "Show help", cmd_help);
  cliRegisterCommand("info", "System information", cmd_system_info);
  cliRegisterCommand("reset", "System reset", cmd_system_reset);
  
  // Register Grbl Compatibility Commands
  cliRegisterCommand("$", "Grbl Settings Report", cmd_grbl_settings);
  cliRegisterCommand("$H", "Run Homing Cycle", cmd_grbl_home);
  cliRegisterCommand("$G", "Show Parser State", cmd_grbl_state);
  
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
    char c = Serial.peek(); // Peek to check for real-time commands without consuming

    // --- 1. Real-time Status Report (?) ---
    if (c == '?') {
        Serial.read(); // Consume character
        
        const char* state_str = "Idle";
        if (motionIsEmergencyStopped()) state_str = "Alarm";
        else if (safetyIsAlarmed()) state_str = "Hold:1"; // Hold due to error
        else if (motionIsMoving()) state_str = "Run";
        else if (motionGetState(0) == MOTION_HOMING_APPROACH_FAST) state_str = "Home";
        // Detect PAUSED state specifically
        else if (motionGetState(0) == MOTION_PAUSED) state_str = "Hold:0"; // User Hold

        // Calculate Plan Buffer (Available Slots)
        // Helps UGS stream data smoothly ('Character Counting')
        int plan_slots = 31 - motionBuffer.available(); 
        if (plan_slots < 0) plan_slots = 0;

        float mPos[4] = {
            motionGetPositionMM(0), motionGetPositionMM(1),
            motionGetPositionMM(2), motionGetPositionMM(3)
        };
        
        // Calculate Work Pos (WPos = MPos - WCO)
        float wPos[4];
        for(int i=0; i<4; i++) wPos[i] = gcodeParser.getWorkPosition(i, mPos[i]);

        // FORMAT: <State|MPos:x,y,z,a|WPos:x,y,z,a|Bf:plan,rx|FS:feed,0>
        Serial.printf("<%s|MPos:%.3f,%.3f,%.3f,%.3f|WPos:%.3f,%.3f,%.3f,%.3f|Bf:%d,127|FS:%.0f,0>\r\n",
            state_str,
            mPos[0], mPos[1], mPos[2], mPos[3],
            wPos[0], wPos[1], wPos[2], wPos[3],
            plan_slots,
            motionPlanner.getFeedOverride() * 100.0f 
        );
        return; // Done handling '?'
    }

    // --- 2. Feed Hold (!) ---
    if (c == '!') {
        Serial.read();
        motionPause();
        // No text response required by Grbl spec
        return;
    }

    // --- 3. Cycle Start (~) ---
    if (c == '~') {
        Serial.read();
        motionResume();
        return;
    }

    // --- 4. Soft Reset (Ctrl+X) ---
    if (c == 0x18) { 
        Serial.read();
        motionEmergencyStop();
        Serial.print("\r\nGrbl 1.1h ['$' for help]\r\n");
        // Clear input buffer
        cli_pos = 0;
        memset(cli_buffer, 0, sizeof(cli_buffer));
        return;
    }

    // --- 5. Standard Input Processing ---
    c = Serial.read(); // Actually consume now

    if (c == '\n' || c == '\r') {
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        cliProcessCommand(cli_buffer);
        cli_pos = 0;
      } else {
        // Empty line (Enter key) -> Keep-alive
        Serial.println("ok"); 
      }
    } else if (c == '\b' || c == 0x7F) {
      if (cli_pos > 0) {
        cli_pos--;
        Serial.write('\b'); Serial.write(' '); Serial.write('\b');
      }
    } else if (c >= 32 && c < 127 && cli_pos < CLI_BUFFER_SIZE - 1) {
      cli_buffer[cli_pos++] = c;
    }
  }
}

void cliProcessCommand(const char* cmd) {
  if (strlen(cmd) == 0) {
      Serial.println("ok");
      return;
  }
  
  // --- 1. HANDLE JOGGING ($J=...) ---
  // Format: $J=G91 X10 F500
  if (strncmp(cmd, "$J=", 3) == 0) {
      if (motionIsMoving() && !motionIsStalled(0)) {
          Serial.println("error:8"); // Idle state required
          return;
      }
      // Pass stripped string to G-Code parser
      // Note: UGS sends G91 inside the jog string usually
      if (gcodeParser.processCommand(cmd + 3)) {
          Serial.println("ok");
      } else {
          Serial.println("error:3"); // Invalid statement
      }
      return;
  }

  // --- 2. G-Code Auto-Detection ---
  // If line starts with G, M, or T
  if (cmd[0] == 'G' || cmd[0] == 'M' || cmd[0] == 'T') {
      if (gcodeParser.processCommand(cmd)) {
          Serial.println("ok"); 
      } else {
          Serial.println("error:20"); // Unsupported command
      }
      return;
  }

  // --- 3. Grbl Settings Write ($100=val) ---
  if (cmd[0] == '$' && isdigit(cmd[1])) {
      int setting_id = atoi(cmd + 1);
      char* eq = strchr((char*)cmd, '=');
      if (eq) {
          float val = atof(eq + 1);
          const char* key = NULL;
          
          // Map Grbl IDs to Internal Keys
          switch(setting_id) {
              case 100: key = KEY_PPM_X; break;
              case 101: key = KEY_PPM_Y; break;
              case 102: key = KEY_PPM_Z; break;
              case 103: key = KEY_PPM_A; break;
              case 110: key = KEY_SPEED_CAL_X; break;
              case 111: key = KEY_SPEED_CAL_Y; break;
              case 112: key = KEY_SPEED_CAL_Z; break;
              case 113: key = KEY_SPEED_CAL_A; break;
              case 120: key = KEY_DEFAULT_ACCEL; break; 
              case 121: key = KEY_DEFAULT_ACCEL; break;
              case 122: key = KEY_DEFAULT_ACCEL; break;
              case 130: key = KEY_X_LIMIT_MAX; break; // Approximated
              case 131: key = KEY_Y_LIMIT_MAX; break;
              case 132: key = KEY_Z_LIMIT_MAX; break;
          }
          
          if (key) {
              configSetFloat(key, val);
              Serial.println("ok");
          } else {
              Serial.println("error:3"); // Invalid value/ID
          }
      }
      return;
  }

  // --- 4. Internal CLI Dispatch ---
  // Copy command to preserve const char*
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
  
  bool handled = false;
  for (int i = 0; i < command_count; i++) {
    if (strcmp(commands[i].command, argv[0]) == 0) {
      commands[i].handler(argc, argv);
      handled = true;
      // Even internal commands return 'ok' to satisfy Grbl stream protocol
      Serial.println("ok"); 
      break;
    }
  }
  
  if (!handled) {
      Serial.println("error:1"); // Expected G-code
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
  // Silent in Grbl mode
}

// ============================================================================
// COMMAND IMPLEMENTATIONS
// ============================================================================

void cmd_grbl_settings(int argc, char** argv) {
    // Report config using Grbl $ID format
    Serial.printf("$100=%.3f\r\n", configGetFloat(KEY_PPM_X, 100.0));
    Serial.printf("$101=%.3f\r\n", configGetFloat(KEY_PPM_Y, 100.0));
    Serial.printf("$102=%.3f\r\n", configGetFloat(KEY_PPM_Z, 100.0));
    Serial.printf("$103=%.3f\r\n", configGetFloat(KEY_PPM_A, 100.0));
    Serial.printf("$110=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_X, 1000.0));
    Serial.printf("$111=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_Y, 1000.0));
    Serial.printf("$112=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_Z, 1000.0));
    Serial.printf("$113=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_A, 1000.0));
    Serial.printf("$120=%.3f\r\n", configGetFloat(KEY_DEFAULT_ACCEL, 100.0));
    Serial.printf("$121=%.3f\r\n", configGetFloat(KEY_DEFAULT_ACCEL, 100.0));
    Serial.printf("$122=%.3f\r\n", configGetFloat(KEY_DEFAULT_ACCEL, 100.0));
    // Calc max travel in mm from raw steps
    Serial.printf("$130=%.3f\r\n", (float)configGetInt(KEY_X_LIMIT_MAX, 500000) / configGetFloat(KEY_PPM_X, 1.0));
    Serial.printf("$131=%.3f\r\n", (float)configGetInt(KEY_Y_LIMIT_MAX, 500000) / configGetFloat(KEY_PPM_Y, 1.0));
    Serial.printf("$132=%.3f\r\n", (float)configGetInt(KEY_Z_LIMIT_MAX, 500000) / configGetFloat(KEY_PPM_Z, 1.0));
    Serial.println("ok");
}

void cmd_grbl_home(int argc, char** argv) {
    motionHome(0); // Default to X or Sequence (TODO: Full Sequence)
    // 'ok' sent by dispatcher
}

void cmd_grbl_state(int argc, char** argv) {
    char buf[64];
    gcodeParser.getParserState(buf, sizeof(buf));
    Serial.println(buf);
    // 'ok' sent by dispatcher
}

void cmd_system_info(int argc, char** argv) {
  char version_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(version_str, sizeof(version_str));
  Serial.printf("[VER:1.1h.Gemini:%s]\r\n", version_str);
  // 'ok' sent by dispatcher
}

void cmd_system_reset(int argc, char** argv) {
  bootRebootSystem(); 
}

void cmd_help(int argc, char** argv) {
  cliPrintHelp();
}