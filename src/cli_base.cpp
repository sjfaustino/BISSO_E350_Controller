/**
 * @file cli_base.cpp
 * @brief CLI Core with Safe Grbl Jogging & WCS
 * @details Fixed Linker Error: Added missing implementation of cliPrintHelp().
 */

#include "cli.h"
#include "serial_logger.h"
#include "psram_alloc.h"
#include "boot_validation.h"
#include "memory_monitor.h"
#include "task_manager.h"
#include "axis_utilities.h" 
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
#include "config_keys.h"
#include "config_unified.h"

// CLI State
static char cli_buffer[CLI_BUFFER_SIZE];
static char history_buffer[CLI_BUFFER_SIZE] = {0}; // Single command history
static uint16_t cli_pos = 0;
static uint8_t esc_state = 0; // 0=None, 1=ESC, 2=ESC [
static bool cli_echo_enabled = false;
static cli_command_t commands[CLI_MAX_COMMANDS];
static int command_count = 0;

// Helpers
extern void cmd_lcd_main(int argc, char** argv);
extern void cmd_jxk10_main(int argc, char** argv);
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
        logPrintln("error:8"); // Not Idle
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
            logPrintln("error:33"); // Invalid G-code target
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
                logPrintln("error:33"); // Invalid G-code target
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

    if (ok) logPrintln("ok");
    else logPrintln("error:3"); 
}

// --- CORE CLI ---

void cliInit() {
  logPrintln("\r\nGrbl 1.1h ['$' for help]"); 
  memset(cli_buffer, 0, sizeof(cli_buffer));
  cli_pos = 0;
  command_count = 0;
  
  // Load echo setting from NVS (default: off)
  cli_echo_enabled = (configGetInt(KEY_CLI_ECHO, 1) == 1);  // Default ON for usability
  
  cliRegisterCommand("help", "Show help", cmd_help);
  cliRegisterCommand("info", "System info", cmd_system_info);
  cliRegisterCommand("reboot", "Restart system", cmd_system_reset);
  cliRegisterCommand("reset", "System reset (reboot alias)", cmd_system_reset);
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
  cliRegisterSDCommands();
  cliRegisterRTCCommands();
  cliRegisterCommand("passwd", "Set password (web/ota)", cmd_passwd);
  cliRegisterCommand("auth", "Auth diagnostics & testing", cmd_auth);
  cliRegisterCommand("lcd", "LCD Display Control", cmd_lcd_main);
  cliRegisterCommand("jxk10", "JXK-10 Current Sensor", cmd_jxk10_main);
  
  gcodeParser.init();
}

void cliUpdate() {
  while (CLI_SERIAL.available() > 0) {
    char c = CLI_SERIAL.peek(); 

    // 1. Real-time Status (?)
    if (c == '?') {
        CLI_SERIAL.read();
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

        if (serialLoggerLock()) {
            logPrintf("<%s|MPos:%.3f,%.3f,%.3f,%.3f|WPos:%.3f,%.3f,%.3f,%.3f|Bf:%d,127|FS:%.0f,0>\r\n",
                state_str,
                mPos[0], mPos[1], mPos[2], mPos[3],
                wPos[0], wPos[1], wPos[2], wPos[3],
                plan_slots,
                motionPlanner.getFeedOverride() * 100.0f 
            );
            serialLoggerUnlock();
        }
        return; 
    }

    // 2. Real-time Overrides
    if (c == '!') { CLI_SERIAL.read(); motionPause(); return; }
    if (c == '~') { CLI_SERIAL.read(); motionResume(); return; }
    if (c == 0x18) { // Soft Reset
        CLI_SERIAL.read();
        motionEmergencyStop();
        logPrintln("\r\nGrbl 1.1h ['$' for help]");
        cli_pos = 0;
        return;
    }

    // 3. Command buffering
    c = CLI_SERIAL.read(); 
    
    // --- ESCAPE SEQUENCE HANDLING (History) ---
    if (esc_state == 0 && c == 0x1B) { // ESC
        esc_state = 1;
        return;
    } else if (esc_state == 1 && c == 0x5B) { // [
        esc_state = 2;
        return;
    } else if (esc_state == 2) {
        if (c == 0x41) { // Up Arrow (ESC[A)
            if (strlen(history_buffer) > 0) {
                // Clear current line on terminal
                if (cli_echo_enabled) {
                    if (serialLoggerLock()) {
                        for (int i = 0; i < cli_pos; i++) CLI_SERIAL.print("\b \b");
                        serialLoggerUnlock();
                    }
                }
                
                // Copy history to current buffer
                strncpy(cli_buffer, history_buffer, CLI_BUFFER_SIZE - 1);
                cli_pos = strlen(cli_buffer);
                
                // Echo the recalled command
                if (cli_echo_enabled) {
                    if (serialLoggerLock()) {
                        CLI_SERIAL.print(cli_buffer);
                        serialLoggerUnlock();
                    }
                }
            }
        }
        esc_state = 0;
        return;
    }
    esc_state = 0; // Reset if any other char comes during sequence
    
    static bool last_was_eol = false;
    static char last_eol_char = 0;

    if (c == '\n' || c == '\r') {
      if (last_was_eol && c != last_eol_char) {
        last_was_eol = false; // Reset for next time
        return; // Skip the second part of a CRLF or LFCR
      }
      last_was_eol = true;
      last_eol_char = c;

      if (cli_echo_enabled) {
        if (serialLoggerLock()) {
            CLI_SERIAL.println();
            serialLoggerUnlock();
        }
      }
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        
        // Save to history before processing (if it's not empty and different)
        if (strlen(cli_buffer) > 0 && strcmp(cli_buffer, history_buffer) != 0) {
            strncpy(history_buffer, cli_buffer, CLI_BUFFER_SIZE - 1);
        }

        if (strncmp(cli_buffer, "$J=", 3) == 0) {
            handle_jog_command(cli_buffer + 3);
        } else {
            cliProcessCommand(cli_buffer);
        }
        cli_pos = 0;
      } else {
        logPrintln("ok"); 
      }
    } else if (c == '\b' || c == 0x7F) {
      last_was_eol = false;
      if (cli_pos > 0) {
        cli_pos--;
        if (cli_echo_enabled) {
            if (serialLoggerLock()) {
                CLI_SERIAL.print("\b \b");
                serialLoggerUnlock();
            }
        }
      }
    } else if (c == '\t') {
      // C4: TAB COMPLETION
      last_was_eol = false;
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        
        // Find matching commands
        int match_count = 0;
        int last_match = -1;
        const char* common_prefix = nullptr;
        size_t common_len = 0;
        
        for (int i = 0; i < command_count; i++) {
          if (strncasecmp(commands[i].command, cli_buffer, cli_pos) == 0) {
            match_count++;
            last_match = i;
            
            // Track common prefix among matches
            if (common_prefix == nullptr) {
              common_prefix = commands[i].command;
              common_len = strlen(common_prefix);
            } else {
              // Find common length
              size_t j = cli_pos;
              while (j < common_len && commands[i].command[j] && 
                     tolower(common_prefix[j]) == tolower(commands[i].command[j])) {
                j++;
              }
              common_len = j;
            }
          }
        }
        
        if (match_count == 1) {
          // Single match - complete it by printing the suffix only
          // (backspace chars don't work reliably on USB CDC serial)
          const char* cmd = commands[last_match].command;
          size_t cmd_len = strlen(cmd);
          
          if (cli_echo_enabled) {
            if (serialLoggerLock()) {
                // Print only the remaining characters after what's typed
                CLI_SERIAL.print(cmd + cli_pos);
                CLI_SERIAL.print(' ');
                serialLoggerUnlock();
            }
          }
          
          strcpy(cli_buffer, cmd);
          cli_pos = cmd_len;
          cli_buffer[cli_pos++] = ' ';
          cli_buffer[cli_pos] = '\0';
        } else if (match_count > 1) {
          // Multiple matches - complete common prefix and show options
          if (common_len > (size_t)cli_pos) {
            // Extend to common prefix — print suffix only
            if (cli_echo_enabled) {
              if (serialLoggerLock()) {
                // Print only the new characters beyond what's typed
                for (size_t i = cli_pos; i < common_len; i++) {
                  CLI_SERIAL.print(common_prefix[i]);
                }
                serialLoggerUnlock();
              }
            }
            strncpy(cli_buffer, common_prefix, common_len);
            cli_buffer[common_len] = '\0';
            cli_pos = common_len;
          } else {
            // Show all matches
            if (serialLoggerLock()) {
                CLI_SERIAL.println();
                for (int i = 0; i < command_count; i++) {
                  if (strncasecmp(commands[i].command, cli_buffer, cli_pos) == 0) {
                    CLI_SERIAL.print(commands[i].command);
                    CLI_SERIAL.print("  ");
                  }
                }
                CLI_SERIAL.println();
                CLI_SERIAL.print("> ");  // Simple prompt
                if (cli_echo_enabled) CLI_SERIAL.print(cli_buffer);
                serialLoggerUnlock();
            }
          }
        }
        // No matches - do nothing (beep could be added)
      }
    } else if (c >= 32 && c < 127 && cli_pos < CLI_BUFFER_SIZE - 1) {
      last_was_eol = false;
      cli_buffer[cli_pos++] = c;
      if (cli_echo_enabled) {
        if (serialLoggerLock()) {
            CLI_SERIAL.write(c);
            serialLoggerUnlock();
        }
      }
    }
  }
}

void cliProcessCommand(const char* cmd) {
  if (strlen(cmd) == 0) { logPrintln("ok"); return; }
  
  // Internal CLI (Check registered commands first)
  char cmd_copy[CLI_BUFFER_SIZE];
  strncpy(cmd_copy, cmd, CLI_BUFFER_SIZE - 1); cmd_copy[CLI_BUFFER_SIZE - 1] = '\0';
  // Parse arguments with quote support for SSIDs/passwords with spaces
  // Example: wifi connect "Armor 25T" mypassword
  char* argv[CLI_MAX_ARGS];
  int argc = 0;
  char* p = cmd_copy;

  while (*p && argc < CLI_MAX_ARGS) {
    // Skip leading spaces
    while (*p == ' ') p++;
    if (!*p) break;
    
    if (*p == '"') {
      // Quoted argument - find closing quote
      p++;  // Skip opening quote
      argv[argc++] = p;
      while (*p && *p != '"') p++;
      if (*p == '"') *p++ = '\0';  // Terminate at closing quote
    } else {
      // Unquoted argument - find next space
      argv[argc++] = p;
      while (*p && *p != ' ') p++;
      if (*p) *p++ = '\0';  // Terminate at space
    }
  }

  
  if (argc > 0) {
    for (int i = 0; i < command_count; i++) {
      if (strcasecmp(commands[i].command, argv[0]) == 0) {
        commands[i].handler(argc, argv);
        logPrintln("ok"); 
        return;
      }
    }
  }

  // G-Code (Fallback if not a registered CLI command)
  char first_char = toupper(cmd[0]);
  if (first_char == 'G' || first_char == 'M' || first_char == 'T') {
      if (gcodeParser.processCommand(cmd)) logPrintln("ok"); 
      else logPrintln("error:20");
      return;
  }

  // Settings ($100=val)
  // PHASE 5.10: Use strtol/strtof with error checking
  if (cmd[0] == '$' && isdigit(cmd[1])) {
      char* endptr = NULL;
      long id_long = strtol(cmd + 1, &endptr, 10);
      if (endptr == (cmd + 1) || id_long < 0 || id_long > 255) {
          logPrintln("error:3"); // Unsupported command
          return;
      }
      int id = (int)id_long;

      char* eq = strchr((char*)cmd, '=');
      if (eq) {
          char* endptr2 = NULL;
          float val = strtof(eq + 1, &endptr2);
          if (endptr2 == (eq + 1) || isnan(val) || isinf(val)) {
              logPrintln("error:33"); // Invalid G-code target
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
          if (key) { configSetFloat(key, val); logPrintln("ok"); }
          else logPrintln("error:3");
      }
      return;
  }

  logPrintln("error:1");
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
  // Build entire help output into one buffer, print once — no interleaving possible
  // Allocate from PSRAM (help output can be ~2KB with 50+ commands)
  const size_t BUF_SIZE = 3072;
  char* buf = (char*)psramMalloc(BUF_SIZE);
  if (!buf) return;
  int pos = 0;
  
  pos += snprintf(buf + pos, BUF_SIZE - pos,
    "\r\n=== BISSO E350 CLI Help ===\r\n"
    "Grbl Commands:\r\n"
    "  $         - Show Grbl settings\r\n"
    "  $H        - Run homing cycle\r\n"
    "  $G        - Show parser state\r\n"
    "  ?         - Real-time status report\r\n"
    "  !         - Feed hold\r\n"
    "  ~         - Cycle start / resume\r\n"
    "  Ctrl-X    - Soft reset\r\n");
  
  // Sort commands alphabetically
  qsort(commands, command_count, sizeof(cli_command_t), [](const void* a, const void* b) -> int {
      return strcasecmp(((cli_command_t*)a)->command, ((cli_command_t*)b)->command);
  });

  pos += snprintf(buf + pos, BUF_SIZE - pos, "\r\nSystem Commands:\r\n");
  for (int i = 0; i < command_count && pos < (int)(BUF_SIZE - 80); i++) {
    pos += snprintf(buf + pos, BUF_SIZE - pos, "  %-12s - %s\r\n", commands[i].command, commands[i].help);
  }
  pos += snprintf(buf + pos, BUF_SIZE - pos, "==========================\r\n");

  // Single atomic print — acquire mutex, output, flush, release
  if (serialLoggerLock()) {
    Serial.print(buf);
    Serial.flush();
    serialLoggerUnlock();
  }
  psramFree(buf);
}

// --- COMMANDS ---

void cmd_grbl_settings(int argc, char** argv) {
    if (!serialLoggerLock()) return;
    
    logPrintf("$100=%.3f\r\n", configGetFloat(KEY_PPM_X, 100.0));
    logPrintf("$101=%.3f\r\n", configGetFloat(KEY_PPM_Y, 100.0));
    logPrintf("$102=%.3f\r\n", configGetFloat(KEY_PPM_Z, 100.0));
    logPrintf("$103=%.3f\r\n", configGetFloat(KEY_PPM_A, 100.0));
    logPrintf("$110=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_X, 1000.0));
    logPrintf("$111=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_Y, 1000.0));
    logPrintf("$112=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_Z, 1000.0));
    logPrintf("$113=%.3f\r\n", configGetFloat(KEY_SPEED_CAL_A, 1000.0));
    logPrintf("$120=%.3f\r\n", configGetFloat(KEY_DEFAULT_ACCEL, 100.0));
    logPrintf("$130=%.3f\r\n", (float)configGetInt(KEY_X_LIMIT_MAX, 500000) / configGetFloat(KEY_PPM_X, 1.0));
    
    serialLoggerUnlock();
}

void cmd_grbl_home(int argc, char** argv) { motionHome(0); }

void cmd_grbl_state(int argc, char** argv) {
    char buf[64];
    gcodeParser.getParserState(buf, sizeof(buf));
    logPrintln(buf);
}


void cmd_system_info(int argc, char** argv) {
  char ver[32]; firmwareGetVersionString(ver, 32);
  logPrintf("[VER:1.1h.PosiPro:%s]\r\n", ver);
  
  // Show key hardware configuration
  uint32_t i2c_speed = configGetInt(KEY_I2C_SPEED, 100000);
  logPrintf("[I2C: %lu Hz (%s Mode)]\r\n", 
            (unsigned long)i2c_speed, 
            i2c_speed >= 400000 ? "Fast" : "Standard");
  logPrintf("[RS485: %d baud | Encoder: %d baud]\r\n",
            configGetInt(KEY_RS485_BAUD, 9600),
            configGetInt(KEY_ENC_BAUD, 9600));
  logPrintf("[Echo: %s | OTA Check: %s]\r\n",
            configGetInt(KEY_CLI_ECHO, 1) ? "ON" : "OFF",
            configGetInt(KEY_OTA_CHECK_EN, 0) ? "ON" : "OFF");
}

void cmd_system_reset(int argc, char** argv) { bootRebootSystem(); }
void cmd_help(int argc, char** argv) { cliPrintHelp(); }

void cmd_echo(int argc, char** argv) {
  if (argc < 2) {
    logInfo("Echo is currently %s", cli_echo_enabled ? "ON" : "OFF");
    CLI_USAGE("echo", "[on|off] [save]");
    return;
  }
  
  bool save_to_nvs = false;
  bool new_state = cli_echo_enabled;
  
  // Parse arguments
  for (int i = 1; i < argc; i++) {
    if (strcasecmp(argv[i], "on") == 0) {
      new_state = true;
    } else if (strcasecmp(argv[i], "off") == 0) {
      new_state = false;
    } else if (strcasecmp(argv[i], "save") == 0) {
      save_to_nvs = true;
    }
  }
  
  cli_echo_enabled = new_state;
  
  if (save_to_nvs) {
    configSetInt(KEY_CLI_ECHO, cli_echo_enabled ? 1 : 0);
    configUnifiedSave();
    logInfo("Echo %s (saved to NVS)", cli_echo_enabled ? "ENABLED" : "DISABLED");
  } else {
    logInfo("Echo %s", cli_echo_enabled ? "ENABLED" : "DISABLED");
  }
}

// ============================================================================
// TABLE-DRIVEN SUBCOMMAND DISPATCH (P1: DRY Improvement)
// ============================================================================
bool cliDispatchSubcommand(const char* prefix, int argc, char** argv,
                           const cli_subcommand_t* table, size_t table_size,
                           int arg_index) {
    bool has_prefix = (prefix && strlen(prefix) > 0);

    // Check if argument exists
    if (argc <= arg_index) {
        if (has_prefix) {
            logPrintf("%s Usage: %s [", prefix, argv[0]);
        } else {
            logPrintf("Usage: %s [", argv[0]);
        }
        for (size_t i = 0; i < table_size; i++) {
            logPrintf("%s%s", table[i].name, (i < table_size - 1) ? " | " : "");
        }
        logPrintln("]");
        
        // Print available subcommands with help
        for (size_t i = 0; i < table_size; i++) {
            logPrintf("  %-12s %s\n", table[i].name, table[i].help);
        }
        return false;
    }
    
    // Find matching subcommand
    for (size_t i = 0; i < table_size; i++) {
        if (strcasecmp(argv[arg_index], table[i].name) == 0) {
            table[i].handler(argc, argv);
            return true;
        }
    }
    
    // Not found
    if (has_prefix) {
        logWarning("%s Unknown subcommand: %s", prefix, argv[arg_index]);
    } else {
        logWarning("Unknown subcommand: %s", argv[arg_index]);
    }
    return false;
}

// ============================================================================
// TABLE RENDERING HELPERS (P3 KISS Improvement)
// ============================================================================

void cliPrintTableDivider(int w1, int w2, int w3, int w4, int w5) {
    // PHASE 16 FIX: Build entire line in buffer, then output atomically
    char line[256];
    int pos = 0;
    
    auto drawLine = [&](int width) {
        line[pos++] = '+';
        for (int i = 0; i < width + 2 && pos < 254; i++) line[pos++] = '-';
    };
    
    drawLine(w1);
    drawLine(w2);
    drawLine(w3);
    if (w4 > 0) drawLine(w4);
    if (w5 > 0) drawLine(w5);
    line[pos++] = '+';
    line[pos] = '\0';
    
    logDirectPrintln(line);
}

void cliPrintTableHeader(int w1, int w2, int w3, int w4, int w5) {
    cliPrintTableDivider(w1, w2, w3, w4, w5);
}

void cliPrintTableFooter(int w1, int w2, int w3, int w4, int w5) {
    cliPrintTableDivider(w1, w2, w3, w4, w5);
}

void cliPrintTableRow(const char* c1, const char* c2, const char* c3, 
                      int w1, int w2, int w3,
                      const char* c4, int w4,
                      const char* c5, int w5) {
    // PHASE 16 FIX: Build entire row in buffer, then output atomically
    char line[256];
    int pos = 0;
    
    pos += snprintf(line + pos, sizeof(line) - pos, "| %-*s ", w1, c1 ? c1 : "");
    pos += snprintf(line + pos, sizeof(line) - pos, "| %-*s ", w2, c2 ? c2 : "");
    pos += snprintf(line + pos, sizeof(line) - pos, "| %-*s ", w3, c3 ? c3 : "");
    if (w4 > 0) pos += snprintf(line + pos, sizeof(line) - pos, "| %-*s ", w4, c4 ? c4 : "");
    if (w5 > 0) pos += snprintf(line + pos, sizeof(line) - pos, "| %-*s ", w5, c5 ? c5 : "");
    pos += snprintf(line + pos, sizeof(line) - pos, "|");
    
    logDirectPrintln(line);
}
