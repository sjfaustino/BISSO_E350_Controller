/**
 * @file cli_base.cpp
 * @brief CLI Core with Safe Grbl Jogging & WCS (Gemini v3.5.25)
 * @details Fixed Linker Error: Added missing implementation of cliPrintHelp().
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
#include "auth_manager.h"
#include "safety.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

// CLI State
static char cli_buffer[CLI_BUFFER_SIZE];
static uint16_t cli_pos = 0;
static bool cli_echo_enabled = false;
static cli_command_t commands[CLI_MAX_COMMANDS];
static int command_count = 0;

// Helpers
void cmd_help(int argc, char** argv);
void cmd_system_info(int argc, char** argv);
void cmd_system_reset(int argc, char** argv);
void cmd_grbl_settings(int argc, char** argv);
void cmd_grbl_home(int argc, char** argv);
void cmd_grbl_state(int argc, char** argv); 
void cmd_echo(int argc, char** argv);

extern void bootRebootSystem();    

// --- SAFE JOGGING PARSER ---
void handle_jog_command(char* cmd) {
    if (motionIsMoving() || motionIsEmergencyStopped() || safetyIsAlarmed()) {
        Serial.println("error:8"); // Not Idle
        return;
    }

    bool use_relative = false;
    if (strstr(cmd, "G91")) use_relative = true;
    else if (strstr(cmd, "G90")) use_relative = false;

    // PHASE 5.10: Use strtof with error checking instead of atof
    float feed_mm_min = 0.0f;
    char* f_ptr = strchr(cmd, 'F');
    if (f_ptr) {
        char* endptr = NULL;
        float parsed = strtof(f_ptr + 1, &endptr);
        if (endptr != (f_ptr + 1) && !isnan(parsed) && !isinf(parsed)) {
            feed_mm_min = parsed;
        } else {
            Serial.println("error:33"); // Invalid G-code target
            return;
        }
    }

    if (feed_mm_min <= 0.1f) feed_mm_min = 100.0f;
    float feed_mm_s = feed_mm_min / 60.0f;

    float target[4] = {0};
    bool axis_present[4] = {false};
    char axes_char[] = "XYZA";

    float current_mpos[4] = {
        motionGetPositionMM(0), motionGetPositionMM(1),
        motionGetPositionMM(2), motionGetPositionMM(3)
    };

    // PHASE 5.10: Use strtof with error checking for axis values
    for(int i=0; i<4; i++) {
        char* ax_ptr = strchr(cmd, axes_char[i]);
        if (ax_ptr) {
            char* endptr = NULL;
            float parsed = strtof(ax_ptr + 1, &endptr);
            if (endptr != (ax_ptr + 1) && !isnan(parsed) && !isinf(parsed)) {
                target[i] = parsed;  // Work coordinate for specified axis
                axis_present[i] = true;
            } else {
                Serial.println("error:33"); // Invalid G-code target
                return;
            }
        } else {
            // PHASE 5.10: For non-specified axes, store work position (not machine)
            // This ensures target[] array uses consistent coordinate system
            target[i] = use_relative ? 0.0f : gcodeParser.getWorkPosition(i, current_mpos[i]);
        }
    }

    bool ok = false;
    if (use_relative) {
        ok = motionMoveRelative(target[0], target[1], target[2], target[3], feed_mm_s);
    } else {
        float wco[4];
        gcodeParser.getWCO(wco);
        
        float machine_target_x = axis_present[0] ? (target[0] + wco[0]) : current_mpos[0];
        float machine_target_y = axis_present[1] ? (target[1] + wco[1]) : current_mpos[1];
        float machine_target_z = axis_present[2] ? (target[2] + wco[2]) : current_mpos[2];
        float machine_target_a = axis_present[3] ? (target[3] + wco[3]) : current_mpos[3];
        
        ok = motionMoveAbsolute(machine_target_x, machine_target_y, machine_target_z, machine_target_a, feed_mm_s);
    }

    if (ok) Serial.println("ok");
    else Serial.println("error:3"); 
}

// --- CORE CLI ---

void cliInit() {
  Serial.println("\r\nGrbl 1.1h ['$' for help]"); 
  memset(cli_buffer, 0, sizeof(cli_buffer));
  cli_pos = 0;
  command_count = 0;
  
  cliRegisterCommand("help", "Show help", cmd_help);
  cliRegisterCommand("info", "System info", cmd_system_info);
  cliRegisterCommand("reset", "System reset", cmd_system_reset);
  cliRegisterCommand("$", "Grbl Settings", cmd_grbl_settings);
  cliRegisterCommand("$H", "Homing", cmd_grbl_home);
  cliRegisterCommand("$G", "Parser State", cmd_grbl_state);
  cliRegisterCommand("echo", "Echo on/off", cmd_echo);

  cliRegisterConfigCommands();
  cliRegisterMotionCommands();
  cliRegisterI2CCommands();
  cliRegisterDiagCommands();
  cliRegisterCalibCommands();
  cliRegisterWifiCommands(); 
  cliRegisterCommand("web_setpass", "Set Web UI password", cmd_web_setpass);
  
  gcodeParser.init();
}

void cliUpdate() {
  while (Serial.available() > 0) {
    char c = Serial.peek(); 

    // 1. Real-time Status (?)
    if (c == '?') {
        Serial.read();
        const char* state_str = "Idle";
        if (motionIsEmergencyStopped()) state_str = "Alarm";
        else if (safetyIsAlarmed()) state_str = "Hold:1";
        else if (motionIsMoving()) state_str = "Run";
        else if (motionGetState(0) == MOTION_HOMING_APPROACH_FAST) state_str = "Home";
        else if (motionGetState(0) == MOTION_PAUSED) state_str = "Hold:0";

        // PHASE 5.10: Use MOTION_BUFFER_SIZE instead of hardcoded 31
        // Grbl convention: report available planning buffer slots (capacity - 1 - used)
        int plan_slots = (MOTION_BUFFER_SIZE - 1) - motionBuffer.available();
        if (plan_slots < 0) plan_slots = 0;

        float mPos[4] = {
            motionGetPositionMM(0), motionGetPositionMM(1),
            motionGetPositionMM(2), motionGetPositionMM(3)
        };
        
        float wPos[4];
        for(int i=0; i<4; i++) wPos[i] = gcodeParser.getWorkPosition(i, mPos[i]);

        Serial.printf("<%s|MPos:%.3f,%.3f,%.3f,%.3f|WPos:%.3f,%.3f,%.3f,%.3f|Bf:%d,127|FS:%.0f,0>\r\n",
            state_str,
            mPos[0], mPos[1], mPos[2], mPos[3],
            wPos[0], wPos[1], wPos[2], wPos[3],
            plan_slots,
            motionPlanner.getFeedOverride() * 100.0f 
        );
        return; 
    }

    // 2. Real-time Overrides
    if (c == '!') { Serial.read(); motionPause(); return; }
    if (c == '~') { Serial.read(); motionResume(); return; }
    if (c == 0x18) { // Soft Reset
        Serial.read();
        motionEmergencyStop();
        Serial.print("\r\nGrbl 1.1h ['$' for help]\r\n");
        cli_pos = 0;
        return;
    }

    // 3. Command buffering
    c = Serial.read(); 
    if (c == '\n' || c == '\r') {
      if (cli_echo_enabled) Serial.println();
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        if (strncmp(cli_buffer, "$J=", 3) == 0) {
            handle_jog_command(cli_buffer + 3);
        } else {
            cliProcessCommand(cli_buffer);
        }
        cli_pos = 0;
      } else {
        Serial.println("ok"); 
      }
    } else if (c == '\b' || c == 0x7F) {
      if (cli_pos > 0) {
        cli_pos--;
        if (cli_echo_enabled) Serial.print("\b \b");
      }
    } else if (c >= 32 && c < 127 && cli_pos < CLI_BUFFER_SIZE - 1) {
      cli_buffer[cli_pos++] = c;
      if (cli_echo_enabled) Serial.write(c);
    }
  }
}

void cliProcessCommand(const char* cmd) {
  if (strlen(cmd) == 0) { Serial.println("ok"); return; }
  
  // G-Code
  if (cmd[0] == 'G' || cmd[0] == 'M' || cmd[0] == 'T') {
      if (gcodeParser.processCommand(cmd)) Serial.println("ok"); 
      else Serial.println("error:20");
      return;
  }

  // Settings ($100=val)
  // PHASE 5.10: Use strtol/strtof with error checking
  if (cmd[0] == '$' && isdigit(cmd[1])) {
      char* endptr = NULL;
      long id_long = strtol(cmd + 1, &endptr, 10);
      if (endptr == (cmd + 1) || id_long < 0 || id_long > 255) {
          Serial.println("error:3"); // Unsupported command
          return;
      }
      int id = (int)id_long;

      char* eq = strchr((char*)cmd, '=');
      if (eq) {
          char* endptr2 = NULL;
          float val = strtof(eq + 1, &endptr2);
          if (endptr2 == (eq + 1) || isnan(val) || isinf(val)) {
              Serial.println("error:33"); // Invalid G-code target
              return;
          }

          const char* key = NULL;
          switch(id) {
              case 100: key = KEY_PPM_X; break;
              case 101: key = KEY_PPM_Y; break;
              case 102: key = KEY_PPM_Z; break;
              case 103: key = KEY_PPM_A; break;
              case 110: key = KEY_SPEED_CAL_X; break;
              case 111: key = KEY_SPEED_CAL_Y; break;
              case 112: key = KEY_SPEED_CAL_Z; break;
              case 113: key = KEY_SPEED_CAL_A; break;
              case 120: key = KEY_DEFAULT_ACCEL; break;
              case 130: key = KEY_X_LIMIT_MAX; break;
              case 131: key = KEY_Y_LIMIT_MAX; break;
              case 132: key = KEY_Z_LIMIT_MAX; break;
          }
          if (key) { configSetFloat(key, val); Serial.println("ok"); }
          else Serial.println("error:3");
      }
      return;
  }

  // Internal CLI
  char cmd_copy[CLI_BUFFER_SIZE];
  strncpy(cmd_copy, cmd, CLI_BUFFER_SIZE - 1); cmd_copy[CLI_BUFFER_SIZE - 1] = '\0';
  char* argv[CLI_MAX_ARGS];
  int argc = 0;
  char* token = strtok(cmd_copy, " ");
  while (token && argc < CLI_MAX_ARGS) { argv[argc++] = token; token = strtok(NULL, " "); }
  
  if (argc == 0) { Serial.println("ok"); return; }
  
  for (int i = 0; i < command_count; i++) {
    if (strcmp(commands[i].command, argv[0]) == 0) {
      commands[i].handler(argc, argv);
      Serial.println("ok"); 
      return;
    }
  }
  Serial.println("error:1");
}

bool cliRegisterCommand(const char* name, const char* help, cli_handler_t handler) {
  if (command_count >= CLI_MAX_COMMANDS) return false;
  commands[command_count].command = name;
  commands[command_count].help = help;
  commands[command_count].handler = handler;
  command_count++;
  return true;
}

// FIX: Added missing implementation
void cliPrintHelp() {
  Serial.println("\n=== BISSO E350 CLI Help ===");
  Serial.println("Grbl Commands:");
  Serial.println("  $         - Show Grbl settings");
  Serial.println("  $H        - Run homing cycle");
  Serial.println("  $G        - Show parser state");
  Serial.println("  ?         - Real-time status report");
  Serial.println("  !         - Feed hold");
  Serial.println("  ~         - Cycle start / resume");
  Serial.println("  Ctrl-X    - Soft reset");
  
  Serial.println("\nSystem Commands:");
  for (int i = 0; i < command_count; i++) {
    Serial.printf("  %-12s - %s\n", commands[i].command, commands[i].help);
  }
  Serial.println("==========================\n");
}

// --- COMMANDS ---

void cmd_grbl_settings(int argc, char** argv) {
    Serial.printf("$100=%.3f\r\n", configGetFloat(KEY_PPM_X, 100.0));
    Serial.printf("$101=%.3f\r\n", configGetFloat(KEY_PPM_Y, 100.0));
    Serial.printf("$102=%.3f\r\n", configGetFloat(KEY_PPM_Z, 100.0));
    Serial.printf("$103=%.3f\r\n", configGetFloat(KEY_PPM_A, 100.0));
    Serial.printf("$110=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_X, 1000.0));
    Serial.printf("$111=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_Y, 1000.0));
    Serial.printf("$112=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_Z, 1000.0));
    Serial.printf("$113=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_A, 1000.0));
    Serial.printf("$120=%.3f\r\n", configGetFloat(KEY_DEFAULT_ACCEL, 100.0));
    Serial.printf("$130=%.3f\r\n", (float)configGetInt(KEY_X_LIMIT_MAX, 500000) / configGetFloat(KEY_PPM_X, 1.0));
    Serial.println("ok");
}

void cmd_grbl_home(int argc, char** argv) { motionHome(0); }

void cmd_grbl_state(int argc, char** argv) {
    char buf[64];
    gcodeParser.getParserState(buf, sizeof(buf));
    Serial.println(buf);
}

void cmd_system_info(int argc, char** argv) {
  char ver[32]; firmwareGetVersionString(ver, 32);
  Serial.printf("[VER:1.1h.Gemini:%s]\r\n", ver);
  Serial.println("ok");
}

void cmd_system_reset(int argc, char** argv) { bootRebootSystem(); }
void cmd_help(int argc, char** argv) { cliPrintHelp(); }

void cmd_echo(int argc, char** argv) {
  if (argc < 2) {
    Serial.printf("Echo is currently %s\n", cli_echo_enabled ? "ON" : "OFF");
    return;
  }
  
  if (strcmp(argv[1], "on") == 0) {
    cli_echo_enabled = true;
    Serial.println("Echo ENABLED");
  } else if (strcmp(argv[1], "off") == 0) {
    cli_echo_enabled = false;
    Serial.println("Echo DISABLED");
  } else {
    Serial.println("Usage: echo [on|off]");
  }
}