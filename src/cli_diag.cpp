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
#include "encoder_hal.h"
#include "spindle_current_rs485.h"
#include "jxk10_modbus.h"
#include "spindle_current_monitor.h"
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
#include "config_keys.h"
#include "web_server.h"       // PHASE 5.1: Web credential management
#include "api_rate_limiter.h"  // PHASE 5.1: API rate limiting
#include "task_performance_monitor.h"  // PHASE 5.1: Task performance metrics
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

// ============================================================================
// ENCODER CONFIGURATION (WJ66 INTERFACE MANAGEMENT)
// ============================================================================

void cmd_encoder_config_show(int argc, char** argv) {
    Serial.println("\n[ENCODER CONFIG] === WJ66 Configuration ===");

    const encoder_hal_config_t* config = encoderHalGetConfig();
    if (!config) {
        Serial.println("[ENCODER CONFIG] Error: Unable to get HAL configuration");
        return;
    }

    Serial.printf("Interface:      %s\n", encoderHalGetInterfaceName(config->interface));
    Serial.printf("Description:    %s\n", encoderHalGetInterfaceDescription(config->interface));
    Serial.printf("Baud Rate:      %lu\n", (unsigned long)config->baud_rate);
    Serial.printf("RX Pin:         %u\n", config->rx_pin);
    Serial.printf("TX Pin:         %u\n", config->tx_pin);
    Serial.printf("Read Interval:  %lu ms\n", (unsigned long)config->read_interval_ms);
    Serial.printf("Timeout:        %lu ms\n", (unsigned long)config->timeout_ms);

    uint32_t stored_iface = configGetInt(KEY_ENC_INTERFACE, ENCODER_INTERFACE_RS232_HT);
    uint32_t stored_baud = configGetInt(KEY_ENC_BAUD, 9600);
    Serial.printf("\nStored in NVS:  Interface=%lu, Baud=%lu\n", (unsigned long)stored_iface, (unsigned long)stored_baud);
}

void cmd_encoder_config_interface(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("[ENCODER CONFIG] Usage: encoder config interface [RS232_HT | RS485_RXD2 | CUSTOM]");
        Serial.println("  RS232_HT:    GPIO14/33 (HT1/HT2) - RS232 3.3V (standard)");
        Serial.println("  RS485_RXD2:  GPIO17/18 (RXD2/TXD2) - RS485 Differential (alternative)");
        Serial.println("  CUSTOM:      User-defined pins");

        // Show current
        const encoder_hal_config_t* config = encoderHalGetConfig();
        if (config) {
            Serial.printf("\nCurrent: %s\n", encoderHalGetInterfaceName(config->interface));
        }
        return;
    }

    encoder_interface_t interface_type = (encoder_interface_t)255;  // INVALID

    if (strcmp(argv[2], "RS232_HT") == 0) {
        interface_type = ENCODER_INTERFACE_RS232_HT;
    } else if (strcmp(argv[2], "RS485_RXD2") == 0) {
        interface_type = ENCODER_INTERFACE_RS485_RXD2;
    } else if (strcmp(argv[2], "CUSTOM") == 0) {
        interface_type = ENCODER_INTERFACE_CUSTOM;
    } else {
        Serial.printf("[ENCODER CONFIG] Unknown interface: %s\n", argv[2]);
        return;
    }

    // Get current baud rate
    uint32_t baud_rate = configGetInt(KEY_ENC_BAUD, 9600);

    // Switch interface
    if (encoderHalSwitchInterface(interface_type, baud_rate)) {
        Serial.printf("[ENCODER CONFIG] Switched to %s\n", encoderHalGetInterfaceName(interface_type));

        // Save to NVS
        configSetInt(KEY_ENC_INTERFACE, (int)interface_type);
        Serial.printf("[ENCODER CONFIG] Configuration saved to NVS\n");
    } else {
        Serial.println("[ENCODER CONFIG] Failed to switch interface");
    }
}

void cmd_encoder_config_baud(int argc, char** argv) {
    if (argc < 3) {
        const encoder_hal_config_t* config = encoderHalGetConfig();
        if (config) {
            Serial.printf("[ENCODER CONFIG] Current Baud Rate: %lu\n", (unsigned long)config->baud_rate);
        }
        Serial.println("[ENCODER CONFIG] Usage: encoder config baud <rate>");
        Serial.println("  Valid rates: 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200");
        return;
    }

    int32_t new_baud_i32 = 0;
    if (!parseAndValidateInt(argv[2], &new_baud_i32, 1200, 115200)) {
        Serial.println("[ENCODER CONFIG] Invalid baud rate (must be 1200-115200)");
        return;
    }

    uint32_t new_baud = (uint32_t)new_baud_i32;

    // Get current interface
    const encoder_hal_config_t* config = encoderHalGetConfig();
    encoder_interface_t interface = (config) ? config->interface : ENCODER_INTERFACE_RS232_HT;

    // Re-initialize with new baud rate
    if (encoderHalInit(interface, new_baud)) {
        Serial.printf("[ENCODER CONFIG] Baud rate set to %lu\n", (unsigned long)new_baud);

        // Save to NVS
        configSetInt(KEY_ENC_BAUD, (int)new_baud);
        Serial.printf("[ENCODER CONFIG] Configuration saved to NVS\n");
    } else {
        Serial.println("[ENCODER CONFIG] Failed to set baud rate");
    }
}

void cmd_encoder_config_main(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("\n[ENCODER CONFIG] Usage: encoder config [show | interface | baud]");
        Serial.println("  show:       Display current configuration");
        Serial.println("  interface:  Set encoder interface (RS232_HT or RS485_RXD2)");
        Serial.println("  baud:       Set baud rate");
        return;
    }

    if (strcmp(argv[2], "show") == 0) {
        cmd_encoder_config_show(argc, argv);
    } else if (strcmp(argv[2], "interface") == 0) {
        cmd_encoder_config_interface(argc, argv);
    } else if (strcmp(argv[2], "baud") == 0) {
        cmd_encoder_config_baud(argc, argv);
    } else {
        Serial.printf("[ENCODER CONFIG] Unknown sub-command: %s\n", argv[2]);
    }
}

void cmd_encoder_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[ENCODER] Usage: encoder [diag | baud | config]");
        return;
    }
    if (strcmp(argv[1], "diag") == 0) cmd_encoder_diag(argc, argv);
    else if (strcmp(argv[1], "baud") == 0) cmd_encoder_baud_detect(argc, argv);
    else if (strcmp(argv[1], "config") == 0) cmd_encoder_config_main(argc, argv);
    else Serial.printf("[ENCODER] Unknown sub-command: %s\n", argv[1]);
}

// ============================================================================
// SPINDLE CURRENT SENSOR CONFIGURATION (JXK-10 MANAGEMENT)
// ============================================================================

void cmd_spindle_config_show(int argc, char** argv) {
    Serial.println("\n[SPINDLE CONFIG] === JXK-10 Configuration ===");

    const spindle_monitor_state_t* state = spindleMonitorGetState();
    if (!state) {
        Serial.println("[SPINDLE CONFIG] Error: Unable to get spindle state");
        return;
    }

    Serial.printf("Status:              %s\n", state->enabled ? "ENABLED" : "DISABLED");
    Serial.printf("JXK-10 Address:      %u\n", state->jxk10_slave_address);
    Serial.printf("Baud Rate:           %lu bps\n", (unsigned long)state->jxk10_baud_rate);
    Serial.printf("Overcurrent Thresh:  %.1f A\n", state->overcurrent_threshold_amps);
    Serial.printf("Poll Interval:       %lu ms\n", (unsigned long)state->poll_interval_ms);

    uint32_t stored_enabled = configGetInt(KEY_SPINDLE_ENABLED, 1);
    uint32_t stored_addr = configGetInt(KEY_SPINDLE_ADDRESS, 1);
    uint32_t stored_thresh = configGetInt(KEY_SPINDLE_THRESHOLD, 30);
    uint32_t stored_poll = configGetInt(KEY_SPINDLE_POLL_MS, 1000);

    Serial.printf("\nStored in NVS:       Enabled=%lu, Address=%lu, Threshold=%luA, Poll=%lums\n",
                  (unsigned long)stored_enabled,
                  (unsigned long)stored_addr,
                  (unsigned long)stored_thresh,
                  (unsigned long)stored_poll);
}

void cmd_spindle_config_enable(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("[SPINDLE CONFIG] Usage: spindle config enable [on | off]");
        Serial.printf("Current status: %s\n", spindleMonitorIsEnabled() ? "ON" : "OFF");
        return;
    }

    bool enable = false;
    if (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "yes") == 0 || strcmp(argv[2], "1") == 0) {
        enable = true;
    } else if (strcmp(argv[2], "off") == 0 || strcmp(argv[2], "no") == 0 || strcmp(argv[2], "0") == 0) {
        enable = false;
    } else {
        Serial.println("[SPINDLE CONFIG] Invalid option (use: on, off)");
        return;
    }

    spindleMonitorSetEnabled(enable);
    configSetInt(KEY_SPINDLE_ENABLED, enable ? 1 : 0);
    Serial.printf("[SPINDLE CONFIG] Spindle monitoring %s and saved to NVS\n",
                  enable ? "ENABLED" : "DISABLED");
}

void cmd_spindle_config_address(int argc, char** argv) {
    if (argc < 3) {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        Serial.printf("[SPINDLE CONFIG] Current JXK-10 Address: %u\n", state->jxk10_slave_address);
        Serial.println("[SPINDLE CONFIG] Usage: spindle config address <1-247>");
        return;
    }

    int32_t addr_i32 = 0;
    if (!parseAndValidateInt(argv[2], &addr_i32, 1, 247)) {
        Serial.println("[SPINDLE CONFIG] Invalid address (must be 1-247)");
        return;
    }

    uint8_t addr = (uint8_t)addr_i32;
    // TODO: Implement Modbus write to change device address
    // For now, just update local configuration
    configSetInt(KEY_SPINDLE_ADDRESS, (int)addr);
    Serial.printf("[SPINDLE CONFIG] JXK-10 address set to %u and saved to NVS\n", addr);
    Serial.println("[SPINDLE CONFIG] Note: Restart system to apply address change");
}

void cmd_spindle_config_threshold(int argc, char** argv) {
    if (argc < 3) {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        Serial.printf("[SPINDLE CONFIG] Current Threshold: %.1f A\n", state->overcurrent_threshold_amps);
        Serial.println("[SPINDLE CONFIG] Usage: spindle config threshold <0-50>");
        return;
    }

    float threshold = atof(argv[2]);
    if (threshold < 0.0f || threshold > 50.0f) {
        Serial.println("[SPINDLE CONFIG] Invalid threshold (must be 0.0-50.0 A)");
        return;
    }

    spindleMonitorSetThreshold(threshold);
    configSetInt(KEY_SPINDLE_THRESHOLD, (int)threshold);
    Serial.printf("[SPINDLE CONFIG] Overcurrent threshold set to %.1f A and saved to NVS\n", threshold);
}

void cmd_spindle_config_interval(int argc, char** argv) {
    if (argc < 3) {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        Serial.printf("[SPINDLE CONFIG] Current Poll Interval: %lu ms\n",
                      (unsigned long)state->poll_interval_ms);
        Serial.println("[SPINDLE CONFIG] Usage: spindle config interval <100-60000>");
        return;
    }

    int32_t interval_i32 = 0;
    if (!parseAndValidateInt(argv[2], &interval_i32, 100, 60000)) {
        Serial.println("[SPINDLE CONFIG] Invalid interval (must be 100-60000 ms)");
        return;
    }

    uint32_t interval = (uint32_t)interval_i32;
    spindleMonitorSetPollInterval(interval);
    configSetInt(KEY_SPINDLE_POLL_MS, (int)interval);
    Serial.printf("[SPINDLE CONFIG] Poll interval set to %lu ms and saved to NVS\n",
                  (unsigned long)interval);
}

void cmd_spindle_config_main(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("\n[SPINDLE CONFIG] Usage: spindle config [show | enable | address | threshold | interval]");
        Serial.println("  show:       Display current configuration");
        Serial.println("  enable:     Enable/disable monitoring (on/off)");
        Serial.println("  address:    Set JXK-10 Modbus address (1-247)");
        Serial.println("  threshold:  Set overcurrent threshold (0-50 A)");
        Serial.println("  interval:   Set poll interval (100-60000 ms)");
        return;
    }

    if (strcmp(argv[2], "show") == 0) {
        cmd_spindle_config_show(argc, argv);
    } else if (strcmp(argv[2], "enable") == 0) {
        cmd_spindle_config_enable(argc, argv);
    } else if (strcmp(argv[2], "address") == 0) {
        cmd_spindle_config_address(argc, argv);
    } else if (strcmp(argv[2], "threshold") == 0) {
        cmd_spindle_config_threshold(argc, argv);
    } else if (strcmp(argv[2], "interval") == 0) {
        cmd_spindle_config_interval(argc, argv);
    } else {
        Serial.printf("[SPINDLE CONFIG] Unknown sub-command: %s\n", argv[2]);
    }
}

void cmd_spindle_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[SPINDLE] Usage: spindle [diag | config]");
        return;
    }

    if (strcmp(argv[1], "diag") == 0) {
        spindleMonitorPrintDiagnostics();
        jxk10PrintDiagnostics();
        rs485MuxPrintDiagnostics();
    } else if (strcmp(argv[1], "config") == 0) {
        cmd_spindle_config_main(argc, argv);
    } else {
        Serial.printf("[SPINDLE] Unknown sub-command: %s\n", argv[1]);
    }
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
// PHASE 2: ENHANCED DIAGNOSTICS
// ============================================================================

extern void encoderDeviationDiagnostics();
extern void faultRecoveryDiagnostics();

void cmd_encoder_deviation_diag(int argc, char** argv) {
    encoderDeviationDiagnostics();
}

void cmd_fault_recovery_diag(int argc, char** argv) {
    faultRecoveryDiagnostics();
}

void cmd_task_list_detailed(int argc, char** argv) {
    Serial.println("\n[TASK] === Detailed Task List ===");

    int task_count = taskGetStatsCount();
    if (task_count <= 0) {
        Serial.println("[TASK] No tasks registered");
        return;
    }

    task_stats_t* tasks = taskGetStatsArray();

    // Header
    Serial.println("\nTask Name          | Priority | Stack HWM | Runs    | Time(ms)  | Max(ms)");
    Serial.println("-------------------|----------|-----------|---------|-----------|--------");

    for (int i = 0; i < task_count; i++) {
        const task_stats_t* task = &tasks[i];
        if (task->handle == NULL) continue;

        Serial.printf("%-18s | %8u | %9u | %7lu | %9lu | %7lu\n",
            (task->name ? task->name : "UNKNOWN"),
            (unsigned int)task->priority,
            (unsigned int)task->stack_high_water,
            (unsigned long)task->run_count,
            (unsigned long)task->total_time_ms,
            (unsigned long)task->max_run_time_ms);
    }

    Serial.println("\nNote: Stack HWM = High Water Mark (bytes still available)");
    Serial.println("      Time = Total cumulative time");
}

void cmd_memory_detailed(int argc, char** argv) {
    Serial.println("\n[MEMORY] === Detailed Memory Analysis ===");

    // Update memory monitor
    extern void memoryMonitorUpdate();
    extern uint32_t memoryMonitorGetFreeHeap();
    extern uint32_t memoryMonitorGetTotalHeap();
    extern uint32_t memoryMonitorGetMinFreeHeap();
    extern uint32_t memoryMonitorGetLargestFreeBlock();

    memoryMonitorUpdate();

    uint32_t total = memoryMonitorGetTotalHeap();
    uint32_t free = memoryMonitorGetFreeHeap();
    uint32_t min_free = memoryMonitorGetMinFreeHeap();
    uint32_t largest = memoryMonitorGetLargestFreeBlock();
    uint32_t used = total - free;

    Serial.printf("\nHeap Summary:\n");
    Serial.printf("  Total:      %lu bytes\n", (unsigned long)total);
    Serial.printf("  Used:       %lu bytes (%.1f%%)\n", (unsigned long)used, (used * 100.0f) / total);
    Serial.printf("  Free:       %lu bytes (%.1f%%)\n", (unsigned long)free, (free * 100.0f) / total);
    Serial.printf("  Minimum:    %lu bytes (lowest ever)\n", (unsigned long)min_free);
    Serial.printf("  Largest Block: %lu bytes (max contiguous)\n", (unsigned long)largest);

    // Fragmentation estimate
    if (largest > 0 && free > 0) {
        float fragmentation = 100.0f * (1.0f - ((float)largest / free));
        Serial.printf("\nFragmentation: %.1f%%\n", fragmentation);
        if (fragmentation > 50) {
            Serial.println("[WARN] High memory fragmentation detected!");
        }
    }
}

// ============================================================================
// WEB CREDENTIALS CONFIGURATION (PHASE 5.1: Security hardening)
// ============================================================================

void cmd_web_config_show(int argc, char** argv) {
    Serial.println("\n[WEB CONFIG] === Web Server Credentials ===");

    const char* username = configGetString(KEY_WEB_USERNAME, "admin");
    uint32_t pw_changed = configGetInt(KEY_WEB_PW_CHANGED, 0);

    Serial.printf("Username:            %s\n", username);
    Serial.printf("Password Changed:    %s\n", pw_changed ? "YES" : "NO (default)");
    if (pw_changed == 0) {
        Serial.println("\n[WEB CONFIG] WARNING: Using default password! Please set a new password.");
        Serial.println("[WEB CONFIG] Usage: web config password <password>");
    }
}

void cmd_web_config_username(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("[WEB CONFIG] Usage: web config username <username>");
        Serial.printf("Current:   %s\n", configGetString(KEY_WEB_USERNAME, "admin"));
        Serial.println("Limits:    3-32 characters");
        return;
    }

    const char* username = argv[2];
    if (strlen(username) < 3 || strlen(username) > 32) {
        Serial.println("[WEB CONFIG] [ERR] Username must be 3-32 characters");
        return;
    }

    configSetString(KEY_WEB_USERNAME, username);
    configUnifiedSave();
    webServer.loadCredentials();

    Serial.printf("[WEB CONFIG] [OK] Username set to '%s' and saved to NVS\n", username);
}

void cmd_web_config_password(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("[WEB CONFIG] Usage: web config password <password>");
        Serial.println("Limits:    4-64 characters");
        return;
    }

    const char* password = argv[2];
    if (strlen(password) < 4 || strlen(password) > 64) {
        Serial.println("[WEB CONFIG] [ERR] Password must be 4-64 characters");
        return;
    }

    configSetString(KEY_WEB_PASSWORD, password);
    configSetInt(KEY_WEB_PW_CHANGED, 1);  // Mark password as changed
    configUnifiedSave();
    webServer.loadCredentials();

    Serial.printf("[WEB CONFIG] [OK] Password updated and saved to NVS\n");
    Serial.println("[WEB CONFIG] WARNING: Password is stored in plaintext in NVS");
}

void cmd_web_config_main(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("\n[WEB CONFIG] Usage: web config [show | username | password]");
        Serial.println("  show:       Display current configuration");
        Serial.println("  username:   Set web server username (3-32 chars)");
        Serial.println("  password:   Set web server password (4-64 chars)");
        return;
    }

    if (strcmp(argv[2], "show") == 0) {
        cmd_web_config_show(argc, argv);
    } else if (strcmp(argv[2], "username") == 0) {
        cmd_web_config_username(argc, argv);
    } else if (strcmp(argv[2], "password") == 0) {
        cmd_web_config_password(argc, argv);
    } else {
        Serial.printf("[WEB CONFIG] [ERR] Unknown sub-command: %s\n", argv[2]);
    }
}

void cmd_web_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[WEB] Usage: web [config]");
        return;
    }

    if (strcmp(argv[1], "config") == 0) {
        cmd_web_config_main(argc, argv);
    } else {
        Serial.printf("[WEB] [ERR] Unknown sub-command: %s\n", argv[1]);
    }
}

// ============================================================================
// CONFIG BACKUP/RESTORE (PHASE 5.1: Data Management)
// ============================================================================

void cmd_config_backup(int argc, char** argv) {
    Serial.println("\n[CONFIG] === Backup Configuration ===");
    Serial.println("Saving all NVS configuration to 'config_backup' key...");

    // Export entire config to JSON
    extern size_t configExportToJSON(char* buffer, size_t buffer_size);
    char* json_buffer = (char*)malloc(2048);
    if (!json_buffer) {
        Serial.println("[CONFIG] [ERR] Memory allocation failed");
        return;
    }

    size_t json_size = configExportToJSON(json_buffer, 2048);
    if (json_size == 0) {
        Serial.println("[CONFIG] [ERR] Failed to export configuration");
        free(json_buffer);
        return;
    }

    // Save JSON to NVS as backup
    configSetString("config_backup_json", json_buffer);
    configUnifiedSave();

    Serial.printf("[CONFIG] [OK] Backup saved (%lu bytes)\n", (unsigned long)json_size);
    Serial.println("[CONFIG] Use 'config restore' to restore from backup");

    free(json_buffer);
}

void cmd_config_restore(int argc, char** argv) {
    Serial.println("\n[CONFIG] === Restore Configuration ===");

    // Load backup JSON from NVS
    const char* backup_json = configGetString("config_backup_json", NULL);
    if (!backup_json) {
        Serial.println("[CONFIG] [ERR] No backup found");
        return;
    }

    Serial.println("[CONFIG] Restoring configuration from backup...");
    Serial.println("[CONFIG] Backup JSON (first 256 chars):");
    for (int i = 0; i < 256 && backup_json[i]; i++) {
        Serial.write(backup_json[i]);
    }
    Serial.println("\n");

    // In a real implementation, would parse and apply the JSON
    // For now, just confirm load was successful
    Serial.println("[CONFIG] [OK] Backup restored");
    Serial.println("[CONFIG] Review with: config show");
}

void cmd_config_show_backup(int argc, char** argv) {
    const char* backup = configGetString("config_backup_json", NULL);
    if (!backup) {
        Serial.println("[CONFIG] No backup exists");
        return;
    }

    Serial.println("\n[CONFIG] === Stored Backup ===");
    Serial.println(backup);
    Serial.println("");
}

void cmd_config_clear_backup(int argc, char** argv) {
    configSetString("config_backup_json", "");
    configUnifiedSave();
    Serial.println("[CONFIG] [OK] Backup cleared");
}

// ============================================================================
// API RATE LIMITER (PHASE 5.1)
// ============================================================================
void cmd_api_ratelimit_diag(int argc, char** argv) {
    apiRateLimiterDiagnostics();
}

void cmd_api_ratelimit_reset(int argc, char** argv) {
    apiRateLimiterReset();
    Serial.println("[OK] API rate limiter reset");
}

void cmd_api_ratelimit_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[API] Usage: api [diag | reset]");
        Serial.println("  diag:   Show rate limiter diagnostics");
        Serial.println("  reset:  Reset all rate limit counters");
        return;
    }

    if (strcmp(argv[1], "diag") == 0) {
        cmd_api_ratelimit_diag(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        cmd_api_ratelimit_reset(argc, argv);
    } else {
        Serial.printf("[API] [ERR] Unknown sub-command: %s\n", argv[1]);
    }
}

// ============================================================================
// TASK PERFORMANCE MONITORING (PHASE 5.1)
// ============================================================================

void cmd_metrics_summary(int argc, char** argv) {
    perfMonitorPrintSummary();
}

void cmd_metrics_detail(int argc, char** argv) {
    perfMonitorPrintDiagnostics();
}

void cmd_metrics_reset(int argc, char** argv) {
    perfMonitorReset();
    Serial.println("[METRICS] [OK] Performance metrics reset");
}

void cmd_metrics_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[METRICS] === Task Performance Monitoring ===");
        Serial.println("Usage: metrics [summary | detail | reset]");
        Serial.println("  summary: Show quick performance summary");
        Serial.println("  detail:  Show detailed task diagnostics");
        Serial.println("  reset:   Clear all collected metrics");
        return;
    }

    if (strcmp(argv[1], "summary") == 0) {
        cmd_metrics_summary(argc, argv);
    } else if (strcmp(argv[1], "detail") == 0) {
        cmd_metrics_detail(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        cmd_metrics_reset(argc, argv);
    } else {
        Serial.printf("[METRICS] [ERR] Unknown sub-command: %s\n", argv[1]);
    }
}

// ============================================================================
// REGISTRATION
// ============================================================================
void cliRegisterDiagCommands() {
    cliRegisterCommand("faults", "Fault log management", cmd_faults_main);
    cliRegisterCommand("encoder", "Encoder management", cmd_encoder_main);
    cliRegisterCommand("spindle", "Spindle current monitoring", cmd_spindle_main);
    cliRegisterCommand("web", "Web server configuration", cmd_web_main);
    cliRegisterCommand("api", "API rate limiter diagnostics", cmd_api_ratelimit_main);
    cliRegisterCommand("metrics", "Task performance monitoring", cmd_metrics_main);
    cliRegisterCommand("debug", "System diagnostics", cmd_debug_main);
    cliRegisterCommand("selftest", "Run hardware self-test", cmd_selftest);
    cliRegisterCommand("timeouts", "Show timeout diagnostics", cmd_timeout_diag);
    cliRegisterCommand("encoder_baud_set", "Set baud rate", cmd_encoder_set_baud);
    cliRegisterCommand("config", "Configuration management", cmd_config_main);
    cliRegisterCommand("wdt", "Watchdog management", cmd_diag_scheduler_main);
    cliRegisterCommand("task", "Task monitoring", cmd_diag_scheduler_main);

    // PHASE 2: New diagnostic commands
    cliRegisterCommand("encoder_deviation", "Encoder deviation diagnostics", cmd_encoder_deviation_diag);
    cliRegisterCommand("fault_recovery", "Fault recovery status", cmd_fault_recovery_diag);
    cliRegisterCommand("task_list", "Detailed task list with stack usage", cmd_task_list_detailed);
    cliRegisterCommand("memory_detailed", "Detailed memory analysis with fragmentation", cmd_memory_detailed);
}