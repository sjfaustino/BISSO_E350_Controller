#include "cli.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "boot_validation.h"
#include "encoder_wj66.h"
#include "encoder_comm_stats.h"
#include "i2c_bus_recovery.h"
#include "task_manager.h"
#include "watchdog_manager.h"
#include "timeout_manager.h"
#include "memory_monitor.h"
#include "plc_iface.h"
#include "motion.h" 
#include "config_unified.h"
#include "config_schema_versioning.h"
#include "config_validator.h"
#include "safety.h"           
#include "firmware_version.h" 
#include "encoder_motion_integration.h"
#include "encoder_calibration.h" 
#include "system_utilities.h" // For axisCharToIndex
#include "input_validation.h" // For parseAndValidateInt
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

// Forward declarations for external diagnostic functions
extern uint32_t taskGetUptime();

// Forward declarations for specialized handlers
void debugEncodersHandler();
void debugAllHandler();
void debugConfigHandler();
void cmd_diag_scheduler_main(int argc, char** argv); // Dispatcher

// ============================================================================
// WDT / TASK CONSOLIDATED COMMAND HANDLERS (DEFINED HERE to resolve linker errors)
// ============================================================================

// Externs for WDT/Task utilities (actual definitions are in watchdog_manager.cpp and task_manager.cpp)
extern void watchdogShowStatus();
extern void watchdogShowTasks();
extern void watchdogShowStats();
extern void watchdogPrintDetailedReport();
extern void taskShowStats();
extern void taskShowAllTasks();
extern uint8_t taskGetCpuUsage();

void cmd_wdt_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[WDT] Usage: wdt [status | tasks | stats | report]");
        return;
    }

    if (strcmp(argv[1], "status") == 0) {
        watchdogShowStatus();
    } else if (strcmp(argv[1], "tasks") == 0) {
        watchdogShowTasks();
    } else if (strcmp(argv[1], "stats") == 0) {
        watchdogShowStats();
    } else if (strcmp(argv[1], "report") == 0) {
        watchdogPrintDetailedReport();
    } else {
        Serial.printf("[WDT] Error: Unknown parameter '%s'\n", argv[1]);
    }
}

void cmd_task_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[TASK] Usage: task [stats | list | cpu]");
        return;
    }

    if (strcmp(argv[1], "stats") == 0) {
        taskShowStats();
    } else if (strcmp(argv[1], "list") == 0) {
        taskShowAllTasks();
    } else if (strcmp(argv[1], "cpu") == 0) {
        Serial.printf("[TASK] Total CPU Usage: %u%%\n", taskGetCpuUsage());
    } else {
        Serial.printf("[TASK] Error: Unknown parameter '%s'\n", argv[1]);
    }
}

// ============================================================================
// WRAPPER / DISPATCHER IMPLEMENTATION
// ============================================================================

void cmd_faults_show(int argc, char** argv) { faultShowHistory(); }
void cmd_faults_stats(int argc, char** argv); 
void cmd_faults_clear(int argc, char** argv) { faultClearHistory(); }
void cmd_timeout_diag(int argc, char** argv) { timeoutShowDiagnostics(); }
void cmd_i2c_diag(int argc, char** argv) { i2cShowStats(); }
void cmd_i2c_recover(int argc, char** argv) { i2cRecoverBus(); }
void cmd_encoder_diag(int argc, char** argv) { encoderMotionDiagnostics(); }
void cmd_encoder_baud_detect(int argc, char** argv) { encoderDetectBaudRate(); }

// Dispatcher that routes calls based on the command name (argv[0])
void cmd_diag_scheduler_main(int argc, char** argv) {
    if (strcmp(argv[0], "wdt") == 0) {
        cmd_wdt_main(argc, argv);
    } else if (strcmp(argv[0], "task") == 0) {
        cmd_task_main(argc, argv);
    }
}

// --- WDT/TASK CONSOLIDATION WRAPPERS (Now calls the main handlers directly) ---
// FIX: Cast string literals to char* to resolve ISO C++ warnings
void cmd_task_stats(int argc, char** argv) { char* args[] = {(char*)"task", (char*)"stats"}; cmd_task_main(2, args); }
void cmd_task_list(int argc, char** argv) { char* args[] = {(char*)"task", (char*)"list"}; cmd_task_main(2, args); }
void cmd_task_cpu(int argc, char** argv) { char* args[] = {(char*)"task", (char*)"cpu"}; cmd_task_main(2, args); }
void cmd_wdt_status(int argc, char** argv) { char* args[] = {(char*)"wdt", (char*)"status"}; cmd_wdt_main(2, args); }
void cmd_wdt_tasks(int argc, char** argv) { char* args[] = {(char*)"wdt", (char*)"tasks"}; cmd_wdt_main(2, args); }
void cmd_wdt_stats(int argc, char** argv) { char* args[] = {(char*)"wdt", (char*)"stats"}; cmd_wdt_main(2, args); }
void cmd_wdt_report(int argc, char** argv) { char* args[] = {(char*)"wdt", (char*)"report"}; cmd_wdt_main(2, args); }


// Aliases for config commands (extern definitions - implemented in cli_config.cpp)
extern void cmd_config_schema_show(int argc, char** argv);
extern void cmd_config_migrate(int argc, char** argv);
extern void cmd_config_rollback(int argc, char** argv);
extern void cmd_config_validate(int argc, char** argv);
extern void cmd_config_show(int argc, char** argv);
extern void cmd_config_reset(int argc, char** argv);
extern void cmd_config_save(int argc, char** argv);
extern void cmd_config_main(int argc, char** argv);


// ============================================================================
// CONFIGURATION DISPATCHER (Consolidated)
// ============================================================================

void cmd_config_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[CONFIG] === Configuration Management Command ===");
        Serial.println("[CONFIG] Usage: config [show | schema | migrate | rollback | validate | reset | save]");
        Serial.println("[CONFIG]   show: Show current run-time settings (aliased to 'config').");
        Serial.println("[CONFIG]   schema: Show schema history and key metadata.");
        Serial.println("[CONFIG]   migrate: Automatically migrate configuration schema.");
        Serial.println("[CONFIG]   rollback <v>: Rollback schema to specific version (e.g., rollback 0).");
        Serial.println("[CONFIG]   validate: Run full consistency validation report.");
        Serial.println("[CONFIG]   reset: Reset ALL configuration to factory defaults.");
        Serial.println("[CONFIG]   save: Force save current configuration to NVS.");
        return;
    }

    if (strcmp(argv[1], "show") == 0) {
        cmd_config_show(argc, argv);
    } else if (strcmp(argv[1], "schema") == 0) {
        cmd_config_schema_show(argc, argv);
    } else if (strcmp(argv[1], "migrate") == 0) {
        cmd_config_migrate(argc, argv);
    } else if (strcmp(argv[1], "rollback") == 0) {
        // Need at least 3 arguments: "config rollback V"
        if (argc < 3) {
             Serial.println("[CONFIG] ERROR: Rollback requires a target version number.");
             return;
        }
        cmd_config_rollback(argc, argv); // Handles version argument in argv[2]
    } else if (strcmp(argv[1], "validate") == 0) {
        cmd_config_validate(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        cmd_config_reset(argc, argv);
    } else if (strcmp(argv[1], "save") == 0) {
        cmd_config_save(argc, argv);
    } else {
        Serial.printf("[CONFIG] Error: Unknown parameter '%s'\n", argv[1]);
        Serial.println("[CONFIG] Use 'config' without parameters for help.");
    }
}


// ============================================================================
// FAULT STATS COMMAND IMPLEMENTATION
// ============================================================================

static const char* formatTimestamp(uint32_t timestamp_ms) {
    static char time_buffer[32];
    time_t t = timestamp_ms / 1000;
    struct tm *tm = localtime(&t);
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm);
    return time_buffer;
}

void cmd_faults_stats(int argc, char** argv) {
    fault_stats_t stats = faultGetStats();
    uint32_t current_uptime_ms = millis(); 

    Serial.println("[FAULT] ╔════════════════════════════════════════╗");
    Serial.println("[FAULT] ║        Faults Statistics               ║");
    Serial.println("[FAULT] ╚════════════════════════════════════════╝");
    
    Serial.printf("[FAULT] Total Faults: %lu\n", stats.total_faults);
    Serial.println("[FAULT] ----------------------------------------");
    Serial.printf("[FAULT] Encoder Faults: %lu\n", stats.encoder_faults);
    Serial.printf("[FAULT] Motion Faults: %lu\n", stats.motion_faults);
    Serial.printf("[FAULT] Safety Faults: %lu\n", stats.safety_faults);
    Serial.printf("[FAULT] PLC/I2C Faults: %lu\n", stats.plc_faults);
    Serial.printf("[FAULT] Config/Calibration Faults: %lu\n", stats.config_faults);
    Serial.printf("[FAULT] System/WDT Faults: %lu\n", stats.system_faults);
    Serial.println("[FAULT]");
    
    if (stats.total_faults > 0) {
        Serial.printf("[FAULT] Most Recent: %s\n", formatTimestamp(stats.last_fault_time_ms));
        Serial.printf("[FAULT] First Recorded: %s\n", formatTimestamp(stats.first_fault_time_ms));
        
        uint32_t uptime_since_last_ms = current_uptime_ms - stats.last_fault_time_ms;
        uint32_t hours = uptime_since_last_ms / 3600000;
        uint32_t minutes = (uptime_since_last_ms % 3600000) / 60000;
        uint32_t seconds = (uptime_since_last_ms % 60000) / 1000;

        Serial.printf("[FAULT] Uptime Since Last Fault: %lu hrs, %lu min, %lu sec\n", hours, minutes, seconds);
    } else {
        Serial.println("[FAULT] No permanent faults recorded in NVS.");
    }
}


// ============================================================================
// DEBUG COMMAND DISPATCHER AND HANDLERS (Omitted for brevity)
// ============================================================================

void cmd_debug_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[CLI] Usage: debug [all | encoders | config]");
        return;
    }

    if (strcmp(argv[1], "all") == 0) {
        debugAllHandler();
    } else if (strcmp(argv[1], "encoders") == 0) {
        debugEncodersHandler();
    } else if (strcmp(argv[1], "config") == 0) {
        debugConfigHandler();
    } else {
        Serial.printf("[CLI] Error: Unknown debug target '%s'\n", argv[1]);
        Serial.println("[CLI] Try: debug all | debug encoders | debug config");
    }
}

void debugEncodersHandler() {
    extern int32_t motionGetPosition(uint8_t axis);
    
    Serial.println("\n[CLI] ╔════════════════════════════════════════╗");
    Serial.println("[CLI] ║     ENCODER LIVE DATA (1 Hz)           ║");
    Serial.println("[CLI] ╚════════════════════════════════════════╝");
    Serial.println("[CLI] Press CTRL+C in the terminal to stop");
    
    uint32_t last_output_ms = 0;
    
    Serial.println("[CLI] ═════════════════════════════════════════════════");
    
    for (int i = 0; i < 15; i++) { 
        if (millis() - last_output_ms >= 1000) { // 1 Hz update rate
            
            Serial.printf("[CLI] %s", "Axis | Position | Velocity | Age | Status\n");
            
            for (uint8_t axis = 0; axis < MOTION_AXES; axis++) {
                int32_t pos = motionGetPosition(axis);
                uint32_t age = wj66GetAxisAge(axis);
                bool stale = wj66IsStale(axis);
                
                // Velocity is simplified to 0.0 vel in this version for CLI display
                Serial.printf("[CLI] Encoder %u: pos=%-6ld vel=0.0 mm/s age=%-4lu ms %s\n",
                    axis, pos, age, stale ? "❌ STALE" : "✓ OK");
            }
            Serial.println("[CLI] ---------------------------------------------");
            last_output_ms = millis();
        }
        delay(100); 
    }
    
    Serial.println("[CLI] ═════════════════════════════════════════════════");
}

void debugConfigHandler() {
    Serial.println("\n[CLI] ╔════════════════════════════════════════════════════╗");
    Serial.println("[CLI] ║        CONFIGURATION VALUES (NVS Cache)        ║");
    Serial.println("[CLI] ╚════════════════════════════════════════════════════╝");
    
    Serial.println("[CLI] Current Configuration:");
    Serial.println("[CLI] ═══════════════════════════════════════════════════");
    
    // --- Calibration Setup ---
    const float DEFAULT_SCALE = (float)MOTION_POSITION_SCALE_FACTOR; 
    const float DEFAULT_SCALE_DEG = (float)MOTION_POSITION_SCALE_FACTOR_DEG;
    
    // Get actual calibrated scales, using fallback if 0.0f
    float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : DEFAULT_SCALE;
    float scale_y = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : DEFAULT_SCALE;
    float scale_z = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : DEFAULT_SCALE;
    float scale_a = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : DEFAULT_SCALE_DEG;
    
    // --- Soft Limits (Conversion to mm/degrees) ---
    Serial.println("[CLI] Soft Limits (Units: mm / degrees):");
    
    // X Axis (mm)
    Serial.printf("[CLI]   X: %.1f mm to %.1f mm\n", 
                  (float)configGetInt("x_soft_limit_min", 0) / scale_x, 
                  (float)configGetInt("x_soft_limit_max", 0) / scale_x);
    
    // Y Axis (mm)
    Serial.printf("[CLI]   Y: %.1f mm to %.1f mm\n", 
                  (float)configGetInt("y_soft_limit_min", 0) / scale_y, 
                  (float)configGetInt("y_soft_limit_max", 0) / scale_y);
    
    // Z Axis (mm)
    Serial.printf("[CLI]   Z: %.1f mm to %.1f mm\n", 
                  (float)configGetInt("z_soft_limit_min", 0) / scale_z, 
                  (float)configGetInt("z_soft_limit_max", 0) / scale_z);

    // A Axis (degrees)
    Serial.printf("[CLI]   A: %.1f deg to %.1f deg\n", 
                  (float)configGetInt("a_soft_limit_min", 0) / scale_a, 
                  (float)configGetInt("a_soft_limit_max", 0) / scale_a);

    
    // Speed Calibration
    Serial.println("[CLI] Speed Calibration (mm/min):");
    Serial.printf("[CLI]   SLOW: %.1f mm/min\n", configGetFloat("speed_X_mm_s", 0.0f) * 60.0f);
    Serial.printf("[CLI]   MEDIUM: %.1f mm/min\n", configGetFloat("speed_Y_mm_s", 0.0f) * 60.0f);
    Serial.printf("[CLI]   FAST: %.1f mm/min\n", configGetFloat("speed_Z_mm_s", 0.0f) * 60.0f);
    
    // Encoder PPM
    Serial.println("[CLI] Encoder PPM (pulses per mm):");
    Serial.printf("[CLI]   X: %ld\n", configGetInt("encoder_ppm_x", 0));
    Serial.printf("[CLI]   Y: %ld\n", configGetInt("encoder_ppm_y", 0));
    Serial.printf("[CLI]   Z: %ld\n", configGetInt("encoder_ppm_z", 0));
    
    // Timing and Safety Thresholds
    Serial.println("[CLI] Timing & Tolerances:");
    Serial.printf("[CLI]   Stall Timeout: %lu ms\n", configGetInt("stall_timeout_ms", 2000));
    Serial.printf("[CLI]   Encoder Timeout: %lu ms\n", (uint32_t)WJ66_TIMEOUT_MS);
    
    Serial.println("[CLI] ═══════════════════════════════════════════════════");
}

void debugAllHandler() {
    extern const char* motionStateToString(motion_state_t state);
    
    char version_str[FIRMWARE_VERSION_STRING_LEN];
    firmwareGetVersionString(version_str, sizeof(version_str));
    
    Serial.println("\n[CLI] ╔════════════════════════════════════════════════════╗");
    Serial.printf("[CLI] ║     %s SYSTEM DIAGNOSTICS DUMP         ║\n", version_str);
    Serial.println("[CLI] ╚════════════════════════════════════════════════════╝");

    // 1. SYSTEM INFORMATION
    Serial.println("[CLI] ┌─ SYSTEM INFORMATION ─────────────────────────────┐");
    Serial.printf("[CLI] │ Firmware Version: %s\n", version_str);
    Serial.printf("[CLI] │ Uptime: %lu seconds\n", taskGetUptime());
    Serial.printf("[CLI] │ System State: %s\n", motionStateToString(motionGetState(0)));
    
    // 2. MEMORY STATUS
    memoryMonitorUpdate();
    memory_stats_t* mem_stats = memoryMonitorGetStats();
    Serial.println("[CLI] ┌─ MEMORY STATUS ──────────────────────────────────┐");
    Serial.printf("[CLI] │ Free RAM: %lu KB (%u%% used)\n", mem_stats->current_free / 1024, memoryMonitorGetUsagePercent());
    Serial.printf("[CLI] │ Min Free RAM: %lu KB\n", mem_stats->minimum_free / 1024);
    Serial.printf("[CLI] │ Largest Block: %lu bytes\n", mem_stats->largest_block);

    // 3. ENCODER STATUS
    Serial.println("[CLI] ┌─ ENCODER STATUS ─────────────────────────────────┐");
    wj66Diagnostics(); 

    // 4. MOTION CONTROL
    Serial.println("[CLI] ┌─ MOTION CONTROL ─────────────────────────────────┐");
    motionDiagnostics(); 

    // 5. SAFETY SYSTEMS
    Serial.println("[CLI] ┌─ SAFETY SYSTEMS ─────────────────────────────────┐");
    safetyDiagnostics(); 

    // 6. PLC COMMUNICATION
    Serial.println("[CLI] ┌─ PLC COMMUNICATION ──────────────────────────────┐");
    plcDiagnostics(); 
    i2cShowStats(); 

    // 7. CONFIGURATION
    Serial.println("[CLI] ┌─ CONFIGURATION ──────────────────────────────────┐");
    configUnifiedDiagnostics(); 
    
    // 8. WATCHDOG & TASKS
    Serial.println("[CLI] ┌─ WATCHDOG & TASKS ───────────────────────────────┐");
    watchdogShowStatus();
    taskShowStats();

    Serial.println("[CLI] ┌─ OVERALL SYSTEM ─────────────────────────────────┐");
    Serial.println("[CLI] │ Status: READY FOR OPERATION");
    Serial.println("[CLI] ╚════════════════════════════════════════════════════╝");
}

// ============================================================================
// NEW ENCODER BAUD RATE SETTER
// ============================================================================
extern bool parseAndValidateInt(const char* str, int32_t* value, int32_t min, int32_t max);
extern bool encoderSetBaudRate(uint32_t baud_rate);

void cmd_encoder_set_baud(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("[CLI] Usage: encoder_baud_set <baud_rate>");
    Serial.println("[CLI] Example: encoder_baud_set 38400");
    return;
  }
  
  int32_t new_baud_rate_i32 = 0;
  
  // Standard valid baud rate range
  const int32_t MIN_BAUD = 1200;
  const int32_t MAX_BAUD = 115200;

  // 1. Validate input 
  if (!parseAndValidateInt(argv[1], &new_baud_rate_i32, MIN_BAUD, MAX_BAUD)) {
    Serial.printf("[CLI] ERROR: Invalid baud rate. Must be integer between %ld and %ld.\n", MIN_BAUD, MAX_BAUD);
    return;
  }

  uint32_t new_baud_rate = (uint32_t)new_baud_rate_i32;

  // 2. Perform the set operation
  if (encoderSetBaudRate(new_baud_rate)) {
    Serial.printf("[CLI] ✅ Encoder baud rate set to %lu. Encoder communication re-initialized.\n", new_baud_rate);
  } else {
    Serial.printf("[CLI] ❌ Failed to set encoder baud rate to %lu.\n", new_baud_rate);
  }
}


// ============================================================================
// REGISTRATION (FIXED & CONSOLIDATED)
// ============================================================================

void cliRegisterDiagCommands() {
    // --- New Debug Dispatcher ---
    cliRegisterCommand("debug", "Show detailed system diagnostics (debug [all|encoders|config])", cmd_debug_main); 
    
    // --- Individual Diagnostics (Registered Commands) ---
    cliRegisterCommand("faults_show", "Show fault history", cmd_faults_show); 
    cliRegisterCommand("faults_stats", "Show categorized fault statistics and timeline.", cmd_faults_stats); 
    cliRegisterCommand("faults_clear", "Clear fault history", cmd_faults_clear); 
    
    cliRegisterCommand("timeouts", "Show timeout diagnostics", cmd_timeout_diag);
    cliRegisterCommand("i2c_diag", "Show I²C diagnostics", cmd_i2c_diag);
    cliRegisterCommand("i2c_recover", "Recover I²C bus", cmd_i2c_recover);
    cliRegisterCommand("encoder_diag", "Show encoder diagnostics", cmd_encoder_diag);
    cliRegisterCommand("encoder_baud", "Auto-detect encoder baud rate", cmd_encoder_baud_detect);
    cliRegisterCommand("encoder_baud_set", "Manually set encoder baud rate (e.g., encoder_baud_set 9600)", cmd_encoder_set_baud);
    
    // --- CONFIGURATION CONSOLIDATION ---
    extern void cmd_config_main(int argc, char** argv);
    cliRegisterCommand("config", "Configuration schema management and validation.", cmd_config_main); 
    
    // --- WDT CONSOLIDATION ---
    // The main handlers are defined in this file (cmd_wdt_main, cmd_task_main)
    cliRegisterCommand("wdt", "Watchdog Timer management and diagnostics.", cmd_diag_scheduler_main);
    cliRegisterCommand("task", "FreeRTOS task monitoring and statistics.", cmd_diag_scheduler_main);
}