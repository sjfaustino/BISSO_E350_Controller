/**
 * @file cli_diag.cpp
 * @brief Diagnostic CLI commands implementation
 * @project Gemini v3.3.1
 * @details Fully updated to match Modular Architecture (v3.x)
 */

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
#include "plc_iface.h"        // <-- THIS MUST MATCH THE FILE ABOVE
#include "motion.h" 
#include "config_unified.h"
#include "config_manager.h"
#include "config_schema_versioning.h"
#include "config_validator.h"
#include "safety.h"           
#include "firmware_version.h" 
#include "encoder_motion_integration.h"
#include "encoder_calibration.h" 
#include "system_utilities.h" 
#include "input_validation.h" 
#include "board_inputs.h"     
#include "system_constants.h" 
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

// Forward declarations
extern uint32_t taskGetUptime();
extern void cmd_config_main(int argc, char** argv);

// Local handlers
void debugEncodersHandler();
void debugAllHandler();
void debugConfigHandler();
void cmd_diag_scheduler_main(int argc, char** argv);

// ============================================================================
// SELF-TEST COMMAND IMPLEMENTATION
// ============================================================================
void cmd_selftest(int argc, char** argv) {
    Serial.println("\n=== SYSTEM SELF-TEST SEQUENCE ===");
    bool overall_pass = true;

    // 1. I2C Bus Validation
    Serial.println("[TEST] 1. Checking I2C Devices...");

    // Ensure these constants match plc_iface.h
    const uint8_t addresses[] = {
        ADDR_I73_INPUT,
        ADDR_Q73_OUTPUT,
        BOARD_INPUT_I2C_ADDR
    };
    const char* names[] = {
        "I73 INPUT (0x21)",
        "Q73 OUTPUT (0x22)",
        "BOARD_INPUTS (0x24)"
    };

    // Check 3 devices
    for(int i=0; i<3; i++) {
        uint8_t dummy;
        // Simple read to ping the device
        i2c_result_t res = i2cReadWithRetry(addresses[i], &dummy, 1);

        if(res == I2C_RESULT_OK) {
            Serial.printf("  [PASS] %s: OK\n", names[i]);
        } else {
            Serial.printf("  [FAIL] %s: ERROR (%s)\n", names[i], i2cResultToString(res));
            overall_pass = false;
        }
    }

    // 2. Encoder Validation
    Serial.println("[TEST] 2. Checking Encoder Communication...");
    uint32_t age = wj66GetAxisAge(0); // Check Axis 0 (X)
    encoder_status_t enc_status = wj66GetStatus();
    
    if(age < 500 && enc_status == ENCODER_OK) {
        Serial.printf("  [PASS] Encoder Link OK (Last update: %lu ms ago)\n", (unsigned long)age);
    } else {
        Serial.printf("  [FAIL] Encoder Timeout/Error (Age: %lu ms, Status: %d)\n", (unsigned long)age, enc_status);
        overall_pass = false;
    }

    // 3. Configuration Integrity
    Serial.println("[TEST] 3. Checking Configuration...");
    if(configValidate(false)) {
        Serial.println("  [PASS] Config Schema Valid");
    } else {
        Serial.println("  [FAIL] Config Schema Invalid");
        overall_pass = false;
    }

    // 4. Memory Health
    Serial.println("[TEST] 4. Checking System Resources...");
    memoryMonitorUpdate();
    uint32_t free_heap = memoryMonitorGetFreeHeap();
    if(free_heap > MEMORY_CRITICAL_THRESHOLD_BYTES) {
        Serial.printf("  [PASS] Heap OK (%lu bytes free)\n", (unsigned long)free_heap);
    } else {
        Serial.printf("  [FAIL] Low Memory (%lu bytes < %d)\n", (unsigned long)free_heap, MEMORY_CRITICAL_THRESHOLD_BYTES);
        overall_pass = false;
    }

    Serial.println("---------------------------------");
    Serial.println(overall_pass ? "[RESULT] SELF-TEST PASSED" : "[RESULT] SELF-TEST FAILED");
}

// ============================================================================
// DEBUG MAIN DISPATCHER
// ============================================================================
void cmd_debug_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[DEBUG] Usage: debug [all | encoders | config]");
        return;
    }

    if (strcmp(argv[1], "all") == 0) debugAllHandler();
    else if (strcmp(argv[1], "encoders") == 0) debugEncodersHandler();
    else if (strcmp(argv[1], "config") == 0) debugConfigHandler();
    else Serial.printf("[CLI] Unknown target '%s'\n", argv[1]);
}

// ============================================================================
// WDT / TASK HANDLERS
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
        Serial.println("[WDT] Usage: wdt [status | tasks | stats | report]");
        return;
    }
    if (strcmp(argv[1], "status") == 0) watchdogShowStatus();
    else if (strcmp(argv[1], "tasks") == 0) watchdogShowTasks();
    else if (strcmp(argv[1], "stats") == 0) watchdogShowStats();
    else if (strcmp(argv[1], "report") == 0) watchdogPrintDetailedReport();
}

void cmd_task_main(int argc, char** argv) {
    if (argc < 2) { 
        Serial.println("[TASK] Usage: task [stats | list | cpu]");
        return;
    }
    if (strcmp(argv[1], "stats") == 0) taskShowStats();
    else if (strcmp(argv[1], "list") == 0) taskShowAllTasks();
    else if (strcmp(argv[1], "cpu") == 0) Serial.printf("[TASK] CPU: %u%%\n", taskGetCpuUsage());
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

void cmd_faults_stats(int argc, char** argv) {
    fault_stats_t stats = faultGetStats();
    Serial.println("\n[FAULT] === Statistics ===");
    Serial.printf("Total: %lu\n", (unsigned long)stats.total_faults);
    if (stats.total_faults > 0) {
        Serial.printf("Last: %s\n", formatTimestamp(stats.last_fault_time_ms));
    }
}

void cmd_faults_main(int argc, char** argv) {
    if (argc < 2) { 
        Serial.println("[FAULTS] Usage: faults [show | stats | clear]");
        return;
    }
    if (strcmp(argv[1], "show") == 0) faultShowHistory();
    else if (strcmp(argv[1], "stats") == 0) cmd_faults_stats(argc, argv);
    else if (strcmp(argv[1], "clear") == 0) faultClearHistory();
}

// ============================================================================
// INDIVIDUAL DIAGNOSTICS
// ============================================================================
void cmd_timeout_diag(int argc, char** argv) { timeoutShowDiagnostics(); }
void cmd_encoder_diag(int argc, char** argv) { encoderMotionDiagnostics(); }
void cmd_encoder_baud_detect(int argc, char** argv) { encoderDetectBaudRate(); }

// ============================================================================
// ENCODER BAUD SET
// ============================================================================
extern bool encoderSetBaudRate(uint32_t baud_rate);

void cmd_encoder_set_baud(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("[CLI] Usage: encoder_baud_set <baud_rate>");
    return;
  }
  
  int32_t new_baud_rate_i32 = 0;
  if (!parseAndValidateInt(argv[1], &new_baud_rate_i32, 1200, 115200)) {
    Serial.println("[CLI] Invalid baud rate (1200-115200).");
    return;
  }
  
  if (encoderSetBaudRate((uint32_t)new_baud_rate_i32)) {
    Serial.printf("[CLI] [OK] Encoder baud set to %ld.\n", (long)new_baud_rate_i32);
  } else {
    Serial.println("[CLI] [ERR] Failed to set baud rate.");
  }
}

void cmd_encoder_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[ENCODER] Usage: encoder [diag | baud]");
        return;
    }
    if (strcmp(argv[1], "diag") == 0) cmd_encoder_diag(argc, argv);
    else if (strcmp(argv[1], "baud") == 0) cmd_encoder_baud_detect(argc, argv);
}

// Note: I2C commands have been moved to cli_i2c.cpp for better organization

// ============================================================================
// SCHEDULER DISPATCHER
// ============================================================================
void cmd_diag_scheduler_main(int argc, char** argv) {
    if (strcmp(argv[0], "wdt") == 0) cmd_wdt_main(argc, argv);
    else if (strcmp(argv[0], "task") == 0) cmd_task_main(argc, argv);
}

// ============================================================================
// DEBUG HANDLERS
// ============================================================================
void debugEncodersHandler() {
    Serial.println("[DEBUG] -- Encoder Status --");
    wj66Diagnostics();
}

void debugConfigHandler() {
    Serial.println("[DEBUG] -- Config Status --");
    configUnifiedDiagnostics();
}

void debugAllHandler() {
    Serial.println("\n[DEBUG] === FULL SYSTEM DUMP ===");
    char ver[FIRMWARE_VERSION_STRING_LEN]; 
    firmwareGetVersionString(ver, sizeof(ver));
    Serial.printf("Firmware: %s | Uptime: %lu s\n", ver, (unsigned long)taskGetUptime());
    
    debugEncodersHandler();
    motionDiagnostics();
    safetyDiagnostics();
    
    // UPDATED: Use new diagnostics function
    elboDiagnostics();
    
    configUnifiedDiagnostics();
    watchdogShowStatus();
    taskShowStats();
    Serial.println("[DEBUG] === END DUMP ===");
}

// ============================================================================
// REGISTRATION
// ============================================================================
void cliRegisterDiagCommands() {
    cliRegisterCommand("faults", "Fault log management", cmd_faults_main);
    cliRegisterCommand("encoder", "Encoder management", cmd_encoder_main);
    cliRegisterCommand("debug", "System diagnostics", cmd_debug_main);
    cliRegisterCommand("selftest", "Run hardware self-test", cmd_selftest);
    cliRegisterCommand("timeouts", "Show timeout diagnostics", cmd_timeout_diag);
    cliRegisterCommand("encoder_baud_set", "Set baud rate", cmd_encoder_set_baud);
    cliRegisterCommand("config", "Configuration management", cmd_config_main);
    cliRegisterCommand("wdt", "Watchdog management", cmd_diag_scheduler_main);
    cliRegisterCommand("task", "Task monitoring", cmd_diag_scheduler_main);
}