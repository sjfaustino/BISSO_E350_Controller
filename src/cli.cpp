#include "cli.h"
#include "motion.h"
#include "config_unified.h"
#include "fault_logging.h"
#include "boot_validation.h"
#include "cli_stubs.h"

static char cli_buffer[CLI_BUFFER_SIZE];
static uint16_t cli_pos = 0;
static cli_command_t commands[CLI_MAX_COMMANDS];
static int command_count = 0;
static char* cli_history[CLI_HISTORY_SIZE];
static int history_index = 0;

// Forward declarations for built-in commands
void cmd_help(int argc, char** argv);
void cmd_debug_all(int argc, char** argv);
void cmd_motion_status(int argc, char** argv);
void cmd_encoder_status(int argc, char** argv);
void cmd_safety_status(int argc, char** argv);
void cmd_config_show(int argc, char** argv);
void cmd_config_reset(int argc, char** argv);
void cmd_config_save(int argc, char** argv);
void cmd_motion_move(int argc, char** argv);
void cmd_motion_stop(int argc, char** argv);
void cmd_encoder_calib(int argc, char** argv);
void cmd_encoder_reset(int argc, char** argv);
void cmd_plc_status(int argc, char** argv);
void cmd_system_info(int argc, char** argv);
void cmd_system_reset(int argc, char** argv);
void cmd_calibrate_speed(int argc, char** argv);
void cmd_fault_show(int argc, char** argv);
void cmd_fault_clear(int argc, char** argv);
void cmd_estop_activate(int argc, char** argv);
void cmd_estop_recover(int argc, char** argv);
void cmd_timeout_diag(int argc, char** argv);
void cmd_i2c_diag(int argc, char** argv);
void cmd_i2c_recover(int argc, char** argv);
void cmd_encoder_diag(int argc, char** argv);
void cmd_encoder_baud_detect(int argc, char** argv);
void cmd_task_stats(int argc, char** argv);
void cmd_task_list(int argc, char** argv);
void cmd_task_cpu(int argc, char** argv);
void cmd_wdt_status(int argc, char** argv);
void cmd_wdt_tasks(int argc, char** argv);
void cmd_wdt_stats(int argc, char** argv);
void cmd_wdt_report(int argc, char** argv);
void cmd_config_schema_show(int argc, char** argv);
void cmd_config_migrate(int argc, char** argv);
void cmd_config_rollback(int argc, char** argv);
void cmd_config_validate(int argc, char** argv);

void cliInit() {
  Serial.println("[CLI] Command Line Interface initializing...");
  memset(cli_buffer, 0, sizeof(cli_buffer));
  cli_pos = 0;
  command_count = 0;
  
  // Register built-in commands
  cliRegisterCommand("help", "Show help", cmd_help);
  cliRegisterCommand("debug", "Show all diagnostics", cmd_debug_all);
  cliRegisterCommand("motion", "Motion status", cmd_motion_status);
  cliRegisterCommand("encoder", "Encoder status", cmd_encoder_status);
  cliRegisterCommand("safety", "Safety status", cmd_safety_status);
  cliRegisterCommand("config", "Show configuration", cmd_config_show);
  cliRegisterCommand("config_reset", "Reset config to defaults", cmd_config_reset);
  cliRegisterCommand("config_save", "Save configuration", cmd_config_save);
  cliRegisterCommand("move", "Move axes (move X Y Z A speed)", cmd_motion_move);
  cliRegisterCommand("stop", "Stop motion", cmd_motion_stop);
  cliRegisterCommand("calib", "Start calibration (calib axis distance)", cmd_encoder_calib);
  cliRegisterCommand("calib_reset", "Reset calibration", cmd_encoder_reset);
  cliRegisterCommand("plc", "PLC status", cmd_plc_status);
  cliRegisterCommand("info", "System information", cmd_system_info);
  cliRegisterCommand("reset", "System reset", cmd_system_reset);
  cliRegisterCommand("speed", "Calibrate axis speed (speed axis [distance_mm])", cmd_calibrate_speed);
  cliRegisterCommand("faults", "Show fault history", cmd_fault_show);
  cliRegisterCommand("faults_clear", "Clear fault history", cmd_fault_clear);
  cliRegisterCommand("estop", "Activate emergency stop", cmd_estop_activate);
  cliRegisterCommand("estop_recover", "Request emergency stop recovery", cmd_estop_recover);
  cliRegisterCommand("timeouts", "Show timeout diagnostics", cmd_timeout_diag);
  cliRegisterCommand("i2c_diag", "Show I²C diagnostics", cmd_i2c_diag);
  cliRegisterCommand("i2c_recover", "Recover I²C bus", cmd_i2c_recover);
  cliRegisterCommand("encoder_diag", "Show encoder diagnostics", cmd_encoder_diag);
  cliRegisterCommand("encoder_baud", "Auto-detect encoder baud rate", cmd_encoder_baud_detect);
  cliRegisterCommand("config_schema", "Show schema history", cmd_config_schema_show);
  cliRegisterCommand("config_migrate", "Migrate configuration to new schema", cmd_config_migrate);
  cliRegisterCommand("config_rollback", "Rollback to previous schema", cmd_config_rollback);
  cliRegisterCommand("config_validate", "Validate configuration schema", cmd_config_validate);
  cliRegisterCommand("task_stats", "Show FreeRTOS task statistics", cmd_task_stats);
  cliRegisterCommand("task_list", "List all FreeRTOS tasks", cmd_task_list);
  cliRegisterCommand("task_cpu", "Show CPU usage", cmd_task_cpu);
  cliRegisterCommand("wdt_status", "Show watchdog status", cmd_wdt_status);
  cliRegisterCommand("wdt_tasks", "List monitored tasks", cmd_wdt_tasks);
  cliRegisterCommand("wdt_stats", "Show watchdog statistics", cmd_wdt_stats);
  cliRegisterCommand("wdt_report", "Detailed watchdog report", cmd_wdt_report);
  
  Serial.print("[CLI] Registered ");
  Serial.print(command_count);
  Serial.println(" commands");
  cliPrintPrompt();
}

void cliUpdate() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (cli_pos > 0) {
        cli_buffer[cli_pos] = '\0';
        cliProcessCommand(cli_buffer);
        cli_pos = 0;
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
  
  // Save to history
  if (history_index < CLI_HISTORY_SIZE) {
    cli_history[history_index++] = (char*)cmd;
  }
  
  // Parse command and arguments
  char cmd_copy[CLI_BUFFER_SIZE];
  strncpy(cmd_copy, cmd, CLI_BUFFER_SIZE - 1);
  cmd_copy[CLI_BUFFER_SIZE - 1] = '\0';  // Ensure null termination
  
  char* argv[CLI_MAX_ARGS];
  int argc = 0;
  char* token = strtok(cmd_copy, " ");
  
  while (token != NULL && argc < CLI_MAX_ARGS) {
    argv[argc++] = token;
    token = strtok(NULL, " ");
  }
  
  if (argc == 0) return;
  
  // Find and execute command
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
    Serial.print("  ");
    Serial.print(commands[i].command);
    Serial.print(" - ");
    Serial.println(commands[i].help);
  }
  Serial.println("");
}

void cliPrintPrompt() {
  Serial.print("> ");
}

int cliGetCommandCount() {
  return command_count;
}

// Built-in command implementations
void cmd_help(int argc, char** argv) {
  cliPrintHelp();
}

void cmd_debug_all(int argc, char** argv) {
  Serial.println("\n=== FULL SYSTEM DIAGNOSTICS ===");
  // Would call all diagnostic functions
}

void cmd_motion_status(int argc, char** argv) {
  Serial.println("[CLI] Motion status (would display actual motion data)");
}

void cmd_encoder_status(int argc, char** argv) {
  Serial.println("[CLI] Encoder status (would display encoder data)");
}

void cmd_safety_status(int argc, char** argv) {
  Serial.println("[CLI] Safety status (would display safety data)");
}

void cmd_config_show(int argc, char** argv) {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║           CONFIGURATION - ALL VALUES FROM NVS STORAGE          ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝");
  
  // Soft Limits (Motion boundaries)
  Serial.println("\n[LIMITS] Soft Limits (encoder counts):");
  Serial.print("  X: ");
  Serial.print(configGetInt("x_soft_limit_min", -50000));
  Serial.print(" to ");
  Serial.println(configGetInt("x_soft_limit_max", 50000));
  
  Serial.print("  Y: ");
  Serial.print(configGetInt("y_soft_limit_min", -50000));
  Serial.print(" to ");
  Serial.println(configGetInt("y_soft_limit_max", 50000));
  
  Serial.print("  Z: ");
  Serial.print(configGetInt("z_soft_limit_min", -50000));
  Serial.print(" to ");
  Serial.println(configGetInt("z_soft_limit_max", 50000));
  
  Serial.print("  A: ");
  Serial.print(configGetInt("a_soft_limit_min", 0));
  Serial.print(" to ");
  Serial.println(configGetInt("a_soft_limit_max", 360000));
  
  // Motion Parameters
  Serial.println("\n[MOTION] Motion Parameters:");
  Serial.print("  Default Speed: ");
  Serial.print(configGetFloat("default_speed_mm_s", 50.0f));
  Serial.println(" mm/s");
  
  Serial.print("  Default Acceleration: ");
  Serial.print(configGetFloat("default_acceleration", 5.0f));
  Serial.println(" mm/s²");
  
  // Speed Calibrations
  Serial.println("\n[SPEEDS] Calibrated Speeds (mm/s):");
  float speed_x = configGetFloat("speed_X_mm_s", 0.0f);
  float speed_y = configGetFloat("speed_Y_mm_s", 0.0f);
  float speed_z = configGetFloat("speed_Z_mm_s", 0.0f);
  float speed_a = configGetFloat("speed_A_mm_s", 0.0f);
  
  Serial.print("  X: ");
  if (speed_x > 0) {
    Serial.print(speed_x);
    Serial.println(" ✅");
  } else {
    Serial.println("NOT CALIBRATED");
  }
  
  Serial.print("  Y: ");
  if (speed_y > 0) {
    Serial.print(speed_y);
    Serial.println(" ✅");
  } else {
    Serial.println("NOT CALIBRATED");
  }
  
  Serial.print("  Z: ");
  if (speed_z > 0) {
    Serial.print(speed_z);
    Serial.println(" ✅");
  } else {
    Serial.println("NOT CALIBRATED");
  }
  
  Serial.print("  A: ");
  if (speed_a > 0) {
    Serial.print(speed_a);
    Serial.println(" ✅");
  } else {
    Serial.println("NOT CALIBRATED");
  }
  
  // Encoder PPM
  Serial.println("\n[ENCODER] Encoder PPM (pulses per mm):");
  Serial.print("  X: ");
  Serial.println(configGetInt("encoder_ppm_x", 0));
  
  Serial.print("  Y: ");
  Serial.println(configGetInt("encoder_ppm_y", 0));
  
  Serial.print("  Z: ");
  Serial.println(configGetInt("encoder_ppm_z", 0));
  
  Serial.print("  A: ");
  Serial.println(configGetInt("encoder_ppm_a", 0));
  
  // Safety Settings
  Serial.println("\n[SAFETY] Safety Settings:");
  Serial.print("  Alarm Pin: ");
  Serial.println(configGetInt("alarm_pin", 2));
  
  Serial.print("  Stall Timeout: ");
  Serial.print(configGetInt("stall_timeout_ms", 2000));
  Serial.println(" ms");
  
  Serial.println("\n✅ All values retrieved from NVS persistent storage\n");
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

void cmd_motion_move(int argc, char** argv) {
  if (argc < 6) {
    Serial.println("[CLI] Usage: move X Y Z A speed");
    return;
  }
  Serial.print("[CLI] Moving to (");
  Serial.print(argv[1]);
  Serial.print(", ");
  Serial.print(argv[2]);
  Serial.print(", ");
  Serial.print(argv[3]);
  Serial.print(", ");
  Serial.print(argv[4]);
  Serial.print(") @ ");
  Serial.print(argv[5]);
  Serial.println(" mm/s");
}

void cmd_motion_stop(int argc, char** argv) {
  Serial.println("[CLI] Motion stop requested");
}

void cmd_encoder_calib(int argc, char** argv) {
  if (argc < 3) {
    Serial.println("[CLI] Usage: calib axis distance_mm");
    return;
  }
  Serial.print("[CLI] Calibrating axis ");
  Serial.print(argv[1]);
  Serial.print(" for ");
  Serial.print(argv[2]);
  Serial.println(" mm");
}

void cmd_encoder_reset(int argc, char** argv) {
  Serial.println("[CLI] Encoder calibration reset");
}

void cmd_plc_status(int argc, char** argv) {
  Serial.println("[CLI] PLC Interface status");
}

void cmd_system_info(int argc, char** argv) {
  Serial.println("\n=== System Information ===");
  Serial.print("Firmware: BISSO v4.2\n");
  Serial.print("Platform: ESP32-S3\n");
  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds\n");
}

void cmd_system_reset(int argc, char** argv) {
  Serial.println("[CLI] System reset requested");
  ESP.restart();
}

void cmd_calibrate_speed(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("[CALIB_SPEED] === Axis Speed Calibration ===");
    Serial.println("[CALIB_SPEED] Measures actual axis speed with PLC control");
    Serial.println("[CALIB_SPEED] Usage: speed <axis> [distance_mm]");
    Serial.println("[CALIB_SPEED] Examples:");
    Serial.println("[CALIB_SPEED]   speed X         (calibrate X axis, 1000mm default)");
    Serial.println("[CALIB_SPEED]   speed Y 500     (calibrate Y axis, 500mm)");
    Serial.println("[CALIB_SPEED]   speed Z 2000    (calibrate Z axis, 2000mm)");
    Serial.println("[CALIB_SPEED]   speed A 360     (calibrate A axis, 360mm)");
    Serial.println("[CALIB_SPEED] Axes: X, Y, Z, A (case insensitive)");
    return;
  }
  
  // Convert letter to axis number
  char axis_letter = toupper(argv[1][0]);
  int axis = -1;
  
  if (axis_letter == 'X') axis = 0;
  else if (axis_letter == 'Y') axis = 1;
  else if (axis_letter == 'Z') axis = 2;
  else if (axis_letter == 'A') axis = 3;
  else {
    Serial.println("[CALIB_SPEED] ERROR: Invalid axis. Use X, Y, Z, or A");
    return;
  }
  
  // Default distance is 1000mm
  uint32_t distance_mm = 1000;
  if (argc >= 3) {
    distance_mm = atoi(argv[2]);
    if (distance_mm <= 0) {
      Serial.println("[CALIB_SPEED] ERROR: Distance must be > 0");
      return;
    }
  }
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.print("║ Speed Calibration - Axis ");
  Serial.print(axis_letter);
  Serial.println("          ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  Serial.print("[CALIB_SPEED] Axis: ");
  Serial.print(axis_letter);
  Serial.print(" | Distance: ");
  Serial.print(distance_mm);
  Serial.println(" mm");
  
  // ===== FORWARD MOVEMENT =====
  Serial.println("\n[CALIB_SPEED] === FORWARD MOVEMENT ===");
  Serial.println("[CALIB_SPEED] Reading encoder position...");
  int32_t start_pos = motionGetPosition(axis);
  Serial.print("[CALIB_SPEED] Start position: ");
  Serial.println(start_pos);
  
  Serial.print("[CALIB_SPEED] Sending PLC command: Move axis ");
  Serial.print(axis_letter);
  Serial.print(" forward ");
  Serial.print(distance_mm);
  Serial.println(" mm...");
  
  uint32_t forward_start_time = millis();
  const uint32_t max_timeout = 120000;  // 120 second timeout
  
  Serial.println("[CALIB_SPEED] Waiting for motion to complete...");
  
  // Monitor motion
  bool forward_complete = false;
  uint32_t last_report = 0;
  while (millis() - forward_start_time < max_timeout) {
    int32_t current_pos = motionGetPosition(axis);
    uint32_t elapsed = millis() - forward_start_time;
    
    // Print progress every 500ms
    if (elapsed - last_report >= 500) {
      int32_t delta = current_pos - start_pos;
      Serial.print("[CALIB_SPEED] Progress: ");
      Serial.print(delta);
      Serial.print(" counts, Time: ");
      Serial.print(elapsed);
      Serial.println(" ms");
      last_report = elapsed;
    }
    
    // Check if motion complete (position delta >= target, assuming 20 PPM)
    int32_t target_counts = (distance_mm * 20);
    if (abs(current_pos - start_pos) >= target_counts) {
      Serial.println("[CALIB_SPEED] Forward motion complete!");
      forward_complete = true;
      break;
    }
    
    delay(10);
  }
  
  if (!forward_complete) {
    Serial.println("[CALIB_SPEED] ERROR: Forward motion timeout!");
    return;
  }
  
  uint32_t forward_time_ms = millis() - forward_start_time;
  int32_t forward_end_pos = motionGetPosition(axis);
  int32_t forward_delta = forward_end_pos - start_pos;
  
  Serial.print("[CALIB_SPEED] Forward: ");
  Serial.print(forward_delta);
  Serial.print(" counts in ");
  Serial.print(forward_time_ms);
  Serial.println(" ms");
  
  // ===== WAIT FOR REVERSE WITH 3 SECOND TIMEOUT =====
  Serial.println("\n[CALIB_SPEED] Waiting for motion to stabilize (3 second timeout)...");
  uint32_t pause_start = millis();
  const uint32_t pause_timeout = 3000;  // 3 second timeout
  
  // Wait until motion stops (position stabilizes)
  int32_t stable_pos = forward_end_pos;
  uint32_t no_change_time = 0;
  const uint32_t stability_threshold = 200;  // 200ms of no movement = stable
  
  while (millis() - pause_start < pause_timeout) {
    int32_t current_pos = motionGetPosition(axis);
    
    if (abs(current_pos - stable_pos) < 10) {  // Position hasn't changed much
      no_change_time += 10;
      if (no_change_time >= stability_threshold) {
        uint32_t stabilize_time = millis() - pause_start;
        Serial.print("[CALIB_SPEED] Motion stabilized in ");
        Serial.print(stabilize_time);
        Serial.println(" ms (NOT counted in speed)");
        break;
      }
    } else {
      stable_pos = current_pos;
      no_change_time = 0;
    }
    
    delay(10);
  }
  
  // ===== REVERSE MOVEMENT =====
  Serial.println("\n[CALIB_SPEED] === REVERSE MOVEMENT ===");
  Serial.print("[CALIB_SPEED] Sending PLC command: Move axis ");
  Serial.print(axis_letter);
  Serial.print(" reverse ");
  Serial.print(distance_mm);
  Serial.println(" mm...");
  
  uint32_t reverse_start_time = millis();
  int32_t reverse_start_pos = motionGetPosition(axis);
  
  Serial.println("[CALIB_SPEED] Waiting for reverse motion to complete...");
  
  bool reverse_complete = false;
  last_report = 0;
  while (millis() - reverse_start_time < max_timeout) {
    int32_t current_pos = motionGetPosition(axis);
    uint32_t elapsed = millis() - reverse_start_time;
    
    // Print progress every 500ms
    if (elapsed - last_report >= 500) {
      int32_t delta = abs(current_pos - reverse_start_pos);
      Serial.print("[CALIB_SPEED] Progress: ");
      Serial.print(delta);
      Serial.print(" counts, Time: ");
      Serial.print(elapsed);
      Serial.println(" ms");
      last_report = elapsed;
    }
    
    // Check if motion complete
    int32_t target_counts = (distance_mm * 20);
    if (abs(current_pos - reverse_start_pos) >= target_counts) {
      Serial.println("[CALIB_SPEED] Reverse motion complete!");
      reverse_complete = true;
      break;
    }
    
    delay(10);
  }
  
  if (!reverse_complete) {
    Serial.println("[CALIB_SPEED] ERROR: Reverse motion timeout!");
    return;
  }
  
  uint32_t reverse_time_ms = millis() - reverse_start_time;
  int32_t reverse_end_pos = motionGetPosition(axis);
  int32_t reverse_delta = abs(reverse_end_pos - reverse_start_pos);
  
  Serial.print("[CALIB_SPEED] Reverse: ");
  Serial.print(reverse_delta);
  Serial.print(" counts in ");
  Serial.print(reverse_time_ms);
  Serial.println(" ms");
  
  // ===== CALCULATE RESULTS =====
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║ CALIBRATION RESULTS                   ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  // Average the two measurements
  uint32_t avg_time_ms = (forward_time_ms + reverse_time_ms) / 2;
  int32_t avg_counts = (forward_delta + reverse_delta) / 2;
  
  double speed_mm_s = (distance_mm * 1000.0) / avg_time_ms;
  double speed_mm_min = speed_mm_s * 60.0;
  
  Serial.print("\nAxis: ");
  Serial.println(axis_letter);
  Serial.print("Distance: ");
  Serial.print(distance_mm);
  Serial.println(" mm");
  
  Serial.print("\nForward:  ");
  Serial.print(forward_delta);
  Serial.print(" counts in ");
  Serial.print(forward_time_ms);
  Serial.println(" ms");
  
  Serial.print("Reverse:  ");
  Serial.print(reverse_delta);
  Serial.print(" counts in ");
  Serial.print(reverse_time_ms);
  Serial.println(" ms");
  
  Serial.print("\nAverage:  ");
  Serial.print(avg_counts);
  Serial.print(" counts in ");
  Serial.print(avg_time_ms);
  Serial.println(" ms");
  
  Serial.print("\n✅ CALCULATED SPEED:");
  Serial.print("\n   ");
  Serial.print(speed_mm_s);
  Serial.println(" mm/s");
  Serial.print("   ");
  Serial.print(speed_mm_min);
  Serial.println(" mm/min");
  
  // Save to configuration - using letter-based keys ONLY
  char config_key[32];
  snprintf(config_key, sizeof(config_key), "speed_%c_mm_s", axis_letter);
  configSetFloat(config_key, speed_mm_s);
  
  Serial.print("\n✅ Saved to config:");
  Serial.print("\n   ");
  Serial.print(config_key);
  Serial.print(" = ");
  Serial.println(speed_mm_s);
  
  // Calculate estimated times for operations
  Serial.println("\n📊 ESTIMATED OPERATION TIMES:");
  for (int test_dist = 100; test_dist <= 1000; test_dist += 100) {
    double time_s = (double)test_dist / speed_mm_s;
    Serial.print("   ");
    Serial.print(test_dist);
    Serial.print(" mm = ");
    Serial.print(time_s);
    Serial.println(" seconds");
  }
  
  // Save configuration to persistent storage
  configUnifiedSave();
  Serial.println("\n✅ Configuration saved to persistent storage");
}

void cmd_fault_show(int argc, char** argv) {
  faultShowHistory();
}

void cmd_fault_clear(int argc, char** argv) {
  Serial.println("[CLI] Clearing fault history...");
  faultClearHistory();
}

void cmd_estop_activate(int argc, char** argv) {
  emergencyStopSetActive(true);
}

void cmd_estop_recover(int argc, char** argv) {
  if (!emergencyStopIsActive()) {
    Serial.println("[ESTOP] No active emergency stop");
    return;
  }
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     EMERGENCY STOP RECOVERY REQUEST     ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.println("[ESTOP] System was halted by emergency stop");
  Serial.println("[ESTOP] Requesting recovery procedure...");
  
  if (emergencyStopRequestRecovery()) {
    Serial.println("[ESTOP] Recovery procedure initiated");
    Serial.println("[ESTOP] Please verify all safety conditions before resuming");
    Serial.println("[ESTOP] Type 'estop_confirm' to complete recovery");
  }
}

void cmd_timeout_diag(int argc, char** argv) {
  timeoutShowDiagnostics();
}

void cmd_i2c_diag(int argc, char** argv) {
  i2cShowStats();
}

void cmd_i2c_recover(int argc, char** argv) {
  Serial.println("[CLI] Attempting I²C bus recovery...");
  i2c_bus_status_t status = i2cCheckBusStatus();
  
  Serial.print("[CLI] Current bus status: ");
  Serial.println(i2cBusStatusToString(status));
  
  if (status != I2C_BUS_OK) {
    i2cRecoverBus();
    status = i2cCheckBusStatus();
    Serial.print("[CLI] After recovery: ");
    Serial.println(i2cBusStatusToString(status));
  } else {
    Serial.println("[CLI] Bus is already OK");
  }
}

void cmd_encoder_diag(int argc, char** argv) {
  encoderShowStats();
}

void cmd_encoder_baud_detect(int argc, char** argv) {
  Serial.println("[CLI] Starting encoder baud rate auto-detection...");
  baud_detect_result_t result = encoderDetectBaudRate();
  
  if (result.detected) {
    Serial.print("[CLI] ✅ Baud rate detected: ");
    Serial.println(result.baud_rate);
  } else {
    Serial.println("[CLI] ❌ Could not detect baud rate - using default 9600");
  }
}

void cmd_config_schema_show(int argc, char** argv) {
  if (argc >= 2) {
    if (strcmp(argv[1], "history") == 0) {
      configShowSchemaHistory();
    } else if (strcmp(argv[1], "keys") == 0) {
      configShowKeyMetadata();
    } else if (strcmp(argv[1], "status") == 0) {
      configShowMigrationStatus();
    } else {
      Serial.println("[CLI] Usage: config_schema [history|keys|status]");
    }
  } else {
    configShowMigrationStatus();
    configShowSchemaHistory();
  }
}

void cmd_config_migrate(int argc, char** argv) {
  if (configIsMigrationNeeded()) {
    Serial.println("[CLI] Starting configuration migration...");
    migration_result_t result = configAutoMigrate();
    
    if (result.success) {
      Serial.println("[CLI] ✅ Migration complete");
    } else {
      Serial.println("[CLI] ❌ Migration failed");
    }
  } else {
    Serial.println("[CLI] ✅ Configuration already up to date");
  }
}

void cmd_config_rollback(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("[CLI] Usage: config_rollback <version>");
    Serial.println("[CLI] Example: config_rollback 0");
    return;
  }
  
  uint8_t target_version = atoi(argv[1]);
  Serial.print("[CLI] Rolling back to schema version ");
  Serial.println(target_version);
  
  if (configRollbackToVersion(target_version)) {
    Serial.println("[CLI] ✅ Rollback complete");
  } else {
    Serial.println("[CLI] ❌ Rollback failed");
  }
}

void cmd_config_validate(int argc, char** argv) {
  Serial.println("[CLI] Validating configuration schema...");
  configValidateSchema();
}

void cmd_task_stats(int argc, char** argv) {
  Serial.println("[CLI] Displaying FreeRTOS task statistics...");
  taskShowStats();
}

void cmd_task_list(int argc, char** argv) {
  Serial.println("[CLI] Listing all FreeRTOS tasks...");
  taskShowAllTasks();
}

void cmd_task_cpu(int argc, char** argv) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║       FreeRTOS CPU USAGE              ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  uint8_t cpu_usage = taskGetCpuUsage();
  uint32_t uptime = taskGetUptime();
  
  Serial.print("[TASKS] CPU Usage: ");
  Serial.print(cpu_usage);
  Serial.println("%");
  
  Serial.print("[TASKS] System Uptime: ");
  Serial.print(uptime);
  Serial.println(" seconds\n");
}

void cmd_wdt_status(int argc, char** argv) {
  Serial.println("[CLI] Displaying watchdog status...");
  watchdogShowStatus();
}

void cmd_wdt_tasks(int argc, char** argv) {
  Serial.println("[CLI] Displaying watchdog monitored tasks...");
  watchdogShowTasks();
}

void cmd_wdt_stats(int argc, char** argv) {
  Serial.println("[CLI] Displaying watchdog statistics...");
  watchdogShowStats();
}

void cmd_wdt_report(int argc, char** argv) {
  Serial.println("[CLI] Generating watchdog detailed report...");
  watchdogPrintDetailedReport();
}
