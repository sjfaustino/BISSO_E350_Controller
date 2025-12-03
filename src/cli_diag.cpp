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
#include "system_utilities.h" 
#include "input_validation.h" 
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

// Forward declarations
extern uint32_t taskGetUptime();
extern void cmd_config_main(int argc, char** argv); // Defined in cli_config.cpp

// Local handlers
void debugEncodersHandler();
void debugAllHandler();
void debugConfigHandler();
void cmd_diag_scheduler_main(int argc, char** argv);

// ============================================================================
// DEBUG MAIN DISPATCHER
// ============================================================================
void cmd_debug_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[DEBUG] === System Diagnostics Utility ===");
        Serial.println("[DEBUG] Usage: debug [target]");
        Serial.println("[DEBUG] Targets:");
        Serial.println("  all      - Dump all core subsystems.");
        Serial.println("  encoders - Display real-time encoder positions.");
        Serial.println("  config   - Display current configuration.");
        return;
    }

    if (strcmp(argv[1], "all") == 0) debugAllHandler();
    else if (strcmp(argv[1], "encoders") == 0) debugEncodersHandler();
    else if (strcmp(argv[1], "config") == 0) debugConfigHandler();
    else Serial.printf("[CLI] Error: Unknown debug target '%s'.\n", argv[1]);
}

// ============================================================================
// WDT / TASK CONSOLIDATED HANDLERS (Defined here to fix linker)
// ============================================================================
extern void watchdogShowStatus();
extern void watchdogShowTasks();
extern void watchdogShowStats();
extern void watchdogPrintDetailedReport();
extern void taskShowStats();
extern void taskShowAllTasks();
extern uint8_t taskGetCpuUsage();

void cmd_wdt_main(int argc, char** argv) {
    if (argc < 2) { 
        Serial.println("\n[WDT] === Watchdog Timer Diagnostics ===");
        Serial.println("[WDT] Usage: wdt [parameter]");
        Serial.println("  status  - Show current WDT status.");
        Serial.println("  tasks   - List monitored tasks.");
        Serial.println("  stats   - Display WDT statistics.");
        Serial.println("  report  - Display detailed report.");
        return;
    }
    if (strcmp(argv[1], "status") == 0) watchdogShowStatus();
    else if (strcmp(argv[1], "tasks") == 0) watchdogShowTasks();
    else if (strcmp(argv[1], "stats") == 0) watchdogShowStats();
    else if (strcmp(argv[1], "report") == 0) watchdogPrintDetailedReport();
    else Serial.printf("[WDT] Error: Unknown parameter '%s'.\n", argv[1]);
}

void cmd_task_main(int argc, char** argv) {
    if (argc < 2) { 
        Serial.println("\n[TASK] === FreeRTOS Task Monitoring ===");
        Serial.println("[TASK] Usage: task [parameter]");
        Serial.println("  stats   - Display run time stats.");
        Serial.println("  list    - List running tasks & stack.");
        Serial.println("  cpu     - Display CPU usage %.");
        return;
    }
    if (strcmp(argv[1], "stats") == 0) taskShowStats();
    else if (strcmp(argv[1], "list") == 0) taskShowAllTasks();
    else if (strcmp(argv[1], "cpu") == 0) Serial.printf("[TASK] Total CPU Usage: %u%%\n", taskGetCpuUsage());
    else Serial.printf("[TASK] Error: Unknown parameter '%s'.\n", argv[1]);
}

// ============================================================================
// FAULT HANDLERS
// ============================================================================
static const char* formatTimestamp(uint32_t timestamp_ms) {
    static char time_buffer[32];
    time_t t = timestamp_ms / 1000;
    struct tm *tm = localtime(&t);
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm);
    return time_buffer;
}

void cmd_faults_show(int argc, char** argv) { faultShowHistory(); }
void cmd_faults_clear(int argc, char** argv) { faultClearHistory(); }

void cmd_faults_stats(int argc, char** argv) {
    fault_stats_t stats = faultGetStats();
    Serial.println("\n[FAULT] === Fault Statistics ===");
    Serial.printf("Total: %lu | Encoder: %lu | Motion: %lu | Safety: %lu\n", 
                  stats.total_faults, stats.encoder_faults, stats.motion_faults, stats.safety_faults);
    if (stats.total_faults > 0) {
        Serial.printf("Last Fault: %s\n", formatTimestamp(stats.last_fault_time_ms));
    } else {
        Serial.println("No faults recorded.");
    }
}

void cmd_faults_main(int argc, char** argv) {
    if (argc < 2) { 
        Serial.println("\n[FAULTS] === Fault Log Management ===");
        Serial.println("[FAULTS] Usage: faults [show | stats | clear]");
        return;
    }
    if (strcmp(argv[1], "show") == 0) cmd_faults_show(argc, argv);
    else if (strcmp(argv[1], "stats") == 0) cmd_faults_stats(argc, argv);
    else if (strcmp(argv[1], "clear") == 0) cmd_faults_clear(argc, argv);
    else Serial.printf("[FAULTS] Error: Unknown parameter '%s'.\n", argv[1]);
}

// ============================================================================
// INDIVIDUAL DIAGNOSTICS
// ============================================================================
void cmd_timeout_diag(int argc, char** argv) { timeoutShowDiagnostics(); }
void cmd_encoder_diag(int argc, char** argv) { encoderMotionDiagnostics(); }
void cmd_encoder_baud_detect(int argc, char** argv) { encoderDetectBaudRate(); }

// ============================================================================
// ENCODER MAIN
// ============================================================================
extern bool encoderSetBaudRate(uint32_t baud_rate);
extern bool parseAndValidateInt(const char* str, int32_t* value, int32_t min, int32_t max);

void cmd_encoder_set_baud(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("[CLI] Usage: encoder_baud_set <baud_rate>");
    return;
  }
  int32_t new_baud_rate_i32 = 0;
  if (!parseAndValidateInt(argv[1], &new_baud_rate_i32, 1200, 115200)) {
    Serial.println("[CLI] [ERR] Invalid baud rate.");
    return;
  }
  if (encoderSetBaudRate((uint32_t)new_baud_rate_i32)) {
    Serial.printf("[CLI] [OK] Encoder baud rate set to %ld.\n", new_baud_rate_i32);
  } else {
    Serial.printf("[CLI] [ERR] Failed to set encoder baud rate.\n");
  }
}

void cmd_encoder_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[ENCODER] === WJ66 Management ===");
        Serial.println("Usage: encoder [diag | baud]");
        return;
    }
    if (strcmp(argv[1], "diag") == 0) cmd_encoder_diag(argc, argv);
    else if (strcmp(argv[1], "baud") == 0) cmd_encoder_baud_detect(argc, argv);
    else Serial.printf("[ENCODER] Error: Unknown parameter '%s'.\n", argv[1]);
}

// ============================================================================
// I2C MAIN
// ============================================================================
void cmd_i2c_main(int argc, char** argv) {
    extern void i2cShowStats();
    extern void i2cRecoverBus();
    if (argc < 2) { 
        Serial.println("\n[I2C] === I2C Bus Management ===");
        Serial.println("Usage: i2c [diag | recover]");
        return;
    }
    if (strcmp(argv[1], "diag") == 0) i2cShowStats();
    else if (strcmp(argv[1], "recover") == 0) i2cRecoverBus();
    else Serial.printf("[I2C] Error: Unknown parameter '%s'.\n", argv[1]);
}

// ============================================================================
// SCHEDULER DISPATCHER
// ============================================================================
void cmd_diag_scheduler_main(int argc, char** argv) {
    if (strcmp(argv[0], "wdt") == 0) cmd_wdt_main(argc, argv);
    else if (strcmp(argv[0], "task") == 0) cmd_task_main(argc, argv);
}

// ============================================================================
// DEBUG IMPLEMENTATIONS
// ============================================================================
void debugEncodersHandler() {
    extern int32_t motionGetPosition(uint8_t axis);
    Serial.println("[DEBUG] -- Encoder Status --");
    wj66Diagnostics();
}

void debugConfigHandler() {
    Serial.println("[DEBUG] -- Config Status --");
    configUnifiedDiagnostics();
}

void debugAllHandler() {
    Serial.println("[DEBUG] -- System Dump --");
    char ver[32]; firmwareGetVersionString(ver, 32);
    Serial.printf("Firmware: %s | Uptime: %lu s\n", ver, taskGetUptime());
    debugEncodersHandler();
    motionDiagnostics();
    safetyDiagnostics();
    plcDiagnostics();
    configUnifiedDiagnostics();
    watchdogShowStatus();
    taskShowStats();
}

// ============================================================================
// REGISTRATION
// ============================================================================
void cliRegisterDiagCommands() {
    cliRegisterCommand("faults", "Fault log management", cmd_faults_main);
    cliRegisterCommand("i2c", "I2C diagnostics", cmd_i2c_main);
    cliRegisterCommand("encoder", "Encoder management", cmd_encoder_main);
    cliRegisterCommand("debug", "System diagnostics", cmd_debug_main);
    cliRegisterCommand("timeouts", "Show timeout diagnostics", cmd_timeout_diag);
    cliRegisterCommand("encoder_baud_set", "Set baud rate", cmd_encoder_set_baud);
    cliRegisterCommand("config", "Configuration management", cmd_config_main); 
    cliRegisterCommand("wdt", "Watchdog management", cmd_diag_scheduler_main);
    cliRegisterCommand("task", "Task monitoring", cmd_diag_scheduler_main);
}