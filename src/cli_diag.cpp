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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h> // For localtime
#include <math.h>

// Forward declarations for external diagnostic functions
extern uint32_t taskGetUptime();

// Forward declarations for specialized handlers
void debugEncodersHandler();
void debugAllHandler();
void debugConfigHandler();

// ============================================================================
// MISSING DIAGNOSTIC HANDLERS IMPLEMENTATION
// ============================================================================

void cmd_fault_show(int argc, char** argv) { faultShowHistory(); }
void cmd_faults_stats(int argc, char** argv); // NEW PROTOTYPE
void cmd_fault_clear(int argc, char** argv) { faultClearHistory(); }
void cmd_timeout_diag(int argc, char** argv) { timeoutShowDiagnostics(); }
void cmd_i2c_diag(int argc, char** argv) { i2cShowStats(); }
void cmd_i2c_recover(int argc, char** argv) { i2cRecoverBus(); }
void cmd_encoder_diag(int argc, char** argv) { encoderMotionDiagnostics(); }
void cmd_encoder_baud_detect(int argc, char** argv) { encoderDetectBaudRate(); }

void cmd_task_stats(int argc, char** argv) { taskShowStats(); }
void cmd_task_list(int argc, char** argv) { taskShowAllTasks(); }
void cmd_task_cpu(int argc, char** argv) { 
    Serial.printf("[CLI] CPU Usage: %u%%\n", taskGetCpuUsage()); 
}

void cmd_wdt_status(int argc, char** argv) { watchdogShowStatus(); }
void cmd_wdt_tasks(int argc, char** argv) { watchdogShowTasks(); }
void cmd_wdt_stats(int argc, char** argv) { watchdogShowStats(); }
void cmd_wdt_report(int argc, char** argv) { watchdogPrintDetailedReport(); }

// Aliases for config commands (extern definitions)
extern void cmd_config_schema_show(int argc, char** argv);
extern void cmd_config_migrate(int argc, char** argv);
extern void cmd_config_rollback(int argc, char** argv);
extern void cmd_config_validate(int argc, char** argv);
extern void cmd_config_show(int argc, char** argv);
extern void cmd_config_reset(int argc, char** argv);
extern void cmd_config_save(int argc, char** argv);


// ============================================================================
// FAULT STATS COMMAND IMPLEMENTATION
// ============================================================================

// Helper to format milliseconds to readable time string (HH:MM:SS format)
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
// DEBUG COMMAND DISPATCHER AND HANDLERS (Implementation bodies omitted for brevity)
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
    
    // Get actual calibrated scales, using fallback if 0.0f
    float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : DEFAULT_SCALE;
    float scale_y = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : DEFAULT_SCALE;
    float scale_z = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : DEFAULT_SCALE;
    float scale_a = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : DEFAULT_SCALE;
    
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
// REGISTRATION
// ============================================================================

void cliRegisterDiagCommands() {
    // --- New Debug Dispatcher ---
    cliRegisterCommand("debug", "Show detailed system diagnostics (debug [all|encoders|config])", cmd_debug_main); 
    
    // --- Individual Diagnostics (Registered Commands) ---
    cliRegisterCommand("faults_show", "Show fault history", cmd_fault_show);
    cliRegisterCommand("faults_stats", "Show categorized fault statistics and timeline.", cmd_faults_stats); 
    cliRegisterCommand("faults_clear", "Clear fault history", cmd_fault_clear);
    cliRegisterCommand("timeouts", "Show timeout diagnostics", cmd_timeout_diag);
    cliRegisterCommand("i2c_diag", "Show I²C diagnostics", cmd_i2c_diag);
    cliRegisterCommand("i2c_recover", "Recover I²C bus", cmd_i2c_recover);
    cliRegisterCommand("encoder_diag", "Show encoder diagnostics", cmd_encoder_diag);
    cliRegisterCommand("encoder_baud", "Auto-detect encoder baud rate", cmd_encoder_baud_detect);
    // Aliases for config commands (extern declarations and registrations)
    extern void cmd_config_schema_show(int argc, char** argv);
    extern void cmd_config_migrate(int argc, char** argv);
    extern void cmd_config_rollback(int argc, char** argv);
    extern void cmd_config_validate(int argc, char** argv);
    extern void cmd_config_show(int argc, char** argv);
    extern void cmd_config_reset(int argc, char** argv);
    extern void cmd_config_save(int argc, char** argv);

    cliRegisterCommand("config_schema", "Show schema history", cmd_config_schema_show);
    cliRegisterCommand("config_migrate", "Migrate configuration to new schema", cmd_config_migrate);
    cliRegisterCommand("config_rollback", "Rollback to previous schema", cmd_config_rollback);
    cliRegisterCommand("config_validate", "Validate configuration schema", cmd_config_validate);
    // Aliases for task/WDT commands (defined/implemented in this file)
    cliRegisterCommand("task_stats", "Show FreeRTOS task statistics", cmd_task_stats);
    cliRegisterCommand("task_list", "List all FreeRTOS tasks", cmd_task_list);
    cliRegisterCommand("task_cpu", "Show CPU usage", cmd_task_cpu);
    cliRegisterCommand("wdt_status", "Show watchdog status", cmd_wdt_status);
    cliRegisterCommand("wdt_tasks", "List monitored tasks", cmd_wdt_tasks);
    cliRegisterCommand("wdt_stats", "Show watchdog statistics", cmd_wdt_stats);
    cliRegisterCommand("wdt_report", "Detailed watchdog report", cmd_wdt_report);
}