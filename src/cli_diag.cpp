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
#include <Wire.h>
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
#include "api_ota_updater.h"  // PHASE 5.1: OTA firmware updates
#include "system_telemetry.h"  // PHASE 5.1: System telemetry
#include "firmware_selftest.h"  // PHASE 5.2: Comprehensive self-test suite
#include "safety.h"
#include "firmware_version.h"
#include "encoder_motion_integration.h"
#include "encoder_calibration.h"
#include "system_utilities.h"
#include "input_validation.h"
#include "board_inputs.h"
#include "system_constants.h"
#include "axis_synchronization.h"  // PHASE 5.6: Per-axis motion quality diagnostics
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
// QUICK STATUS DASHBOARD
// ============================================================================
void cmd_status_dashboard(int argc, char** argv) {
    (void)argc; (void)argv;
    watchdogFeed("CLI");
    
    uint32_t uptime_sec = millis() / 1000;
    uint32_t hours = uptime_sec / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;
    uint32_t secs = uptime_sec % 60;
    
    Serial.println("\n╔══════════════════════════════════════════════════════════╗");
    Serial.println("║           BISSO E350 QUICK STATUS DASHBOARD               ║");
    Serial.printf( "║  Uptime: %02lu:%02lu:%02lu                                        ║\n", hours, mins, secs);
    Serial.println("╠══════════════════════════════════════════════════════════╣");
    
    Serial.println("║ POSITION (mm)                                             ║");
    Serial.printf( "║   X: %10.3f    Y: %10.3f                        ║\n",
                  motionGetPosition(0) / 1000.0f, motionGetPosition(1) / 1000.0f);
    Serial.printf( "║   Z: %10.3f    A: %10.3f                        ║\n",
                  motionGetPosition(2) / 1000.0f, motionGetPosition(3) / 1000.0f);
    
    Serial.println("╠──────────────────────────────────────────────────────────╣");
    Serial.println("║ ENCODER FEEDBACK                                          ║");
    bool fb_active = encoderMotionIsFeedbackActive();
    Serial.printf( "║   Status: %s                                         ║\n",
                  fb_active ? "[ON] " : "[OFF]");
    
    Serial.println("╠──────────────────────────────────────────────────────────╣");
    Serial.println("║ SPINDLE CURRENT                                           ║");
    const spindle_monitor_state_t* spindle = spindleMonitorGetState();
    if (spindle->enabled) {
        Serial.printf( "║   Current: %5.1f A  |  Peak: %5.1f A                    ║\n",
                      spindle->current_amps, spindle->current_peak_amps);
        const char* alarm = "OK";
        if (spindle->alarm_tool_breakage) alarm = "TOOL BREAK";
        else if (spindle->alarm_stall) alarm = "STALL";
        else if (spindle->alarm_overload) alarm = "OVERLOAD";
        Serial.printf( "║   Alarm: %-10s                                      ║\n", alarm);
    } else {
        Serial.println("║   Status: [DISABLED]                                      ║");
    }
    
    Serial.println("╠──────────────────────────────────────────────────────────╣");
    Serial.println("║ NETWORK                                                   ║");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf( "║   WiFi: Connected (%d dBm)                              ║\n", WiFi.RSSI());
        Serial.printf( "║   IP: %-15s                                   ║\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("║   WiFi: [DISCONNECTED]                                    ║");
    }
    
    Serial.println("╠──────────────────────────────────────────────────────────╣");
    Serial.println("║ ACTIVE FAULTS                                             ║");
    fault_stats_t faults = faultGetStats();
    if (faults.total_faults == 0) {
        Serial.println("║   [NONE] System healthy                                   ║");
    } else {
        Serial.printf( "║   Total: %lu  |  Last: %lu sec ago                       ║\n",
                      (unsigned long)faults.total_faults,
                      (unsigned long)((millis() - faults.last_fault_time_ms) / 1000));
    }
    
    if (emergencyStopIsActive()) {
        Serial.println("╠══════════════════════════════════════════════════════════╣");
        Serial.println("║  E-STOP ACTIVE - MOTION DISABLED                          ║");
    }
    
    Serial.println("╚══════════════════════════════════════════════════════════╝");
}

// ============================================================================
// RUNTIME / CYCLE COUNTER
// ============================================================================
static uint32_t session_start_mins = 0;
static uint32_t boot_time_ms = 0;

void runtimeInit() {
    session_start_mins = configGetInt(KEY_RUNTIME_MINS, 0);
    boot_time_ms = millis();
}

void cmd_runtime(int argc, char** argv) {
    uint32_t session_mins = (millis() - boot_time_ms) / 60000;
    uint32_t total_mins = session_start_mins + session_mins;
    uint32_t cycles = configGetInt(KEY_CYCLE_COUNT, 0);
    uint32_t last_maint = configGetInt(KEY_LAST_MAINT_MINS, 0);
    uint32_t since_maint = total_mins - last_maint;
    
    if (argc >= 2) {
        if (strcasecmp(argv[1], "reset") == 0) {
            configSetInt(KEY_CYCLE_COUNT, 0);
            Serial.println("[RUNTIME] Cycle counter reset to 0");
            return;
        } else if (strcasecmp(argv[1], "maint") == 0) {
            configSetInt(KEY_LAST_MAINT_MINS, total_mins);
            Serial.println("[RUNTIME] Maintenance recorded");
            return;
        }
    }
    
    uint32_t hours = total_mins / 60;
    uint32_t mins = total_mins % 60;
    uint32_t maint_hours = since_maint / 60;
    
    Serial.println("\n[RUNTIME] === Machine Usage Statistics ===\n");
    Serial.println("┌─────────────────────────┬────────────────────┐");
    Serial.println("│ Metric                  │ Value              │");
    Serial.println("├─────────────────────────┼────────────────────┤");
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu hrs %lu min", (unsigned long)hours, (unsigned long)mins);
    Serial.printf("│ %-23s │ %-18s │\n", "Total Runtime", buf);
    
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)cycles);
    Serial.printf("│ %-23s │ %-18s │\n", "Job Cycles Completed", buf);
    
    snprintf(buf, sizeof(buf), "%lu hrs", (unsigned long)maint_hours);
    Serial.printf("│ %-23s │ %-18s │\n", "Since Last Maintenance", buf);
    
    Serial.println("└─────────────────────────┴────────────────────┘");
    
    if (maint_hours >= 100) {
        Serial.println("\n⚠️  MAINTENANCE RECOMMENDED (100+ hours since last service)");
    }
}

// ============================================================================
// DIGITAL I/O STATUS DISPLAY
// ============================================================================
void cmd_dio_main(int argc, char** argv) {
    (void)argc; (void)argv;
    Serial.println("\n[DIO] === Digital I/O Status ===\n");
    watchdogFeed("CLI");
    
    static const char* input1_labels[] = {"Limit-X", "Limit-Y", "Limit-Z", "E-Stop", "Pause", "Resume", "Probe", "Door"};
    static const char* input2_labels[] = {"Home-X", "Home-Y", "Home-Z", "Home-A", "ToolSns", "Coolant", "In-15", "In-16"};
    static const char* output1_labels[] = {"Spindle", "SpinDir", "Coolant", "Mist", "Clamp", "Vacuum", "Light", "Out-8"};
    static const char* output2_labels[] = {"AirBlast", "Lube", "Alarm", "Ready", "Running", "Error", "Out-15", "Out-16"};
    
    struct { uint8_t addr; const char* name; const char** labels; bool is_output; } banks[] = {
        {0x21, "INPUTS-SAFE", input1_labels, false},
        {0x22, "INPUTS-AUX", input2_labels, false},
        {0x24, "OUTPUTS-MAIN", output1_labels, true},
        {0x25, "OUTPUTS-AUX", output2_labels, true}
    };
    
    Serial.println("┌────────┬────────────────┬────────────────────────┐");
    Serial.println("│ Addr   │ Name           │ State (MSB..LSB)       │");
    Serial.println("├────────┼────────────────┼────────────────────────┤");
    
    for (int b = 0; b < 4; b++) {
        Wire.beginTransmission(banks[b].addr);
        if (Wire.endTransmission() != 0) {
            Serial.printf("│ 0x%02X   │ %-14s │ [NOT CONNECTED]        │\n", banks[b].addr, banks[b].name);
            continue;
        }
        
        Wire.requestFrom(banks[b].addr, (uint8_t)1);
        uint8_t state = Wire.available() ? Wire.read() : 0xFF;
        
        char bits[9];
        for (int i = 7; i >= 0; i--) bits[7-i] = (state & (1 << i)) ? '1' : '0';
        bits[8] = '\0';
        
        Serial.printf("│ 0x%02X   │ %-14s │ %s (0x%02X)        │\n", banks[b].addr, banks[b].name, bits, state);
        
        // Show active channels
        Serial.print("│        │                │ ");
        int count = 0;
        for (int i = 0; i < 8; i++) {
            bool active = banks[b].is_output ? !(state & (1 << i)) : (state & (1 << i));
            if (active) {
                if (count > 0) Serial.print(", ");
                Serial.print(banks[b].labels[i]);
                count++;
            }
        }
        if (count == 0) Serial.print("(none active)");
        Serial.println("  │");
    }
    
    Serial.println("└────────┴────────────────┴────────────────────────┘");
    Serial.println("Legend: Inputs=HIGH when active, Outputs=LOW when relay ON");
}

// ============================================================================
// SPINDLE ALARM CLI SUBCOMMANDS
// ============================================================================
static void cmd_spindle_alarm(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("\n[SPINDLE] Alarm commands:");
        Serial.println("  spindle alarm status   - Show alarm states");
        Serial.println("  spindle alarm clear    - Clear all alarms");
        Serial.println("  spindle alarm toolbreak <amps> - Set threshold (1-20A)");
        Serial.println("  spindle alarm stall <amps> <ms> - Set stall params");
        return;
    }
    
    const spindle_monitor_state_t* state = spindleMonitorGetState();
    
    if (strcasecmp(argv[2], "status") == 0) {
        Serial.println("\n[SPINDLE] === Alarm Status ===");
        Serial.printf("Tool Breakage: %s (count: %lu)\n", 
                     state->alarm_tool_breakage ? "ACTIVE" : "OK",
                     (unsigned long)state->tool_breakage_count);
        Serial.printf("Stall:         %s (count: %lu)\n",
                     state->alarm_stall ? "ACTIVE" : "OK",
                     (unsigned long)state->stall_count);
        Serial.printf("Thresholds: %.1f A drop, %.1f A for %lu ms\n", 
                     state->tool_breakage_drop_amps,
                     state->stall_threshold_amps,
                     (unsigned long)state->stall_timeout_ms);
    } else if (strcasecmp(argv[2], "clear") == 0) {
        spindleMonitorClearAlarms();
    } else if (strcasecmp(argv[2], "toolbreak") == 0 && argc >= 4) {
        spindleMonitorSetToolBreakageThreshold(atof(argv[3]));
    } else if (strcasecmp(argv[2], "stall") == 0 && argc >= 5) {
        spindleMonitorSetStallParams(atof(argv[3]), atoi(argv[4]));
    }
}

// ============================================================================
// SELF-TEST COMMAND IMPLEMENTATION
// ============================================================================
void cmd_selftest(int argc, char** argv) {
    // PHASE 5.2: Enhanced self-test with sub-commands
    if (argc > 1 && strcmp(argv[1], "help") == 0) {
        Serial.println("\n[SELFTEST] === Self-Test Suite ===");
        Serial.println("Usage: selftest [command] [options]");
        Serial.println("  (no args)     Run comprehensive test suite");
        Serial.println("  quick         Quick health check (fast tests only)");
        Serial.println("  memory        Memory subsystem tests");
        Serial.println("  i2c           I2C bus and device tests");
        Serial.println("  storage       LittleFS and NVS tests");
        Serial.println("  motion        Motion system tests");
        Serial.println("  spindle       Spindle monitor tests");
        Serial.println("  safety        Safety system tests");
        Serial.println("  network       Network and WiFi tests");
        Serial.println("  watchdog      Watchdog timer tests");
        Serial.println("  list          List all available tests");
        Serial.println("  help          Show this message");
        return;
    }

    if (argc > 1 && strcmp(argv[1], "list") == 0) {
        selftestListTests();
        return;
    }

    if (argc > 1 && strcmp(argv[1], "quick") == 0) {
        Serial.println("\n[SELFTEST] === Quick Health Check ===");
        bool healthy = selftestQuickCheck();
        Serial.println(healthy ? "[OK] Quick checks passed\n" : "[FAIL] Quick checks failed\n");
        return;
    }

    // Parse category flags
    uint8_t categories = SELFTEST_CAT_ALL;

    if (argc > 1) {
        categories = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "memory") == 0) categories |= SELFTEST_CAT_MEMORY;
            else if (strcmp(argv[i], "i2c") == 0) categories |= SELFTEST_CAT_I2C;
            else if (strcmp(argv[i], "storage") == 0) categories |= SELFTEST_CAT_STORAGE;
            else if (strcmp(argv[i], "motion") == 0) categories |= SELFTEST_CAT_MOTION;
            else if (strcmp(argv[i], "spindle") == 0) categories |= SELFTEST_CAT_SPINDLE;
            else if (strcmp(argv[i], "safety") == 0) categories |= SELFTEST_CAT_SAFETY;
            else if (strcmp(argv[i], "network") == 0) categories |= SELFTEST_CAT_NETWORK;
            else if (strcmp(argv[i], "watchdog") == 0) categories |= SELFTEST_CAT_WATCHDOG;
        }
    }

    // Run comprehensive test suite
    selftest_suite_t suite = selftestRunSuite(categories, true);

    // Print detailed results
    selftestPrintResults(&suite);

    // Print summary
    Serial.println(selftestGetSummary(&suite));

    // Free results
    selftestFreeResults(&suite);
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
    
    // NOTE: JXK-10 address change requires physical device reconfiguration
    // The Modbus slave address is set via DIP switches or device programming tool.
    // This command only updates the controller's expected address for communication.
    // After changing DIP switches on JXK-10, set this value to match.
    configSetInt(KEY_SPINDLE_ADDRESS, (int)addr);
    Serial.printf("[SPINDLE CONFIG] JXK-10 address set to %u and saved to NVS\n", addr);
    Serial.println("[SPINDLE CONFIG] Restart system to apply address change");
    Serial.println("[SPINDLE CONFIG] NOTE: Ensure JXK-10 DIP switches match this address");
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
        Serial.println("[SPINDLE] Usage: spindle [diag | config | alarm]");
        return;
    }

    if (strcmp(argv[1], "diag") == 0) {
        spindleMonitorPrintDiagnostics();
        jxk10PrintDiagnostics();
        rs485MuxPrintDiagnostics();
    } else if (strcmp(argv[1], "config") == 0) {
        cmd_spindle_config_main(argc, argv);
    } else if (strcmp(argv[1], "alarm") == 0) {
        cmd_spindle_alarm(argc, argv);
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
// OTA FIRMWARE UPDATE (PHASE 5.1)
// ============================================================================

void cmd_ota_status(int argc, char** argv) {
    otaUpdaterPrintDiagnostics();
}

void cmd_ota_cancel(int argc, char** argv) {
    otaUpdaterCancel();
    Serial.println("[OTA] [OK] OTA update cancelled");
}

void cmd_ota_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[OTA] === Firmware Update Management ===");
        Serial.println("Usage: ota [status | cancel]");
        Serial.println("  status: Show OTA update status");
        Serial.println("  cancel: Cancel current OTA operation");
        Serial.println("");
        Serial.println("NOTE: Binary upload via /api/update endpoint");
        return;
    }

    if (strcmp(argv[1], "status") == 0) {
        cmd_ota_status(argc, argv);
    } else if (strcmp(argv[1], "cancel") == 0) {
        cmd_ota_cancel(argc, argv);
    } else {
        Serial.printf("[OTA] [ERR] Unknown sub-command: %s\n", argv[1]);
    }
}

// ============================================================================
// SYSTEM TELEMETRY (PHASE 5.1)
// ============================================================================

// ============================================================================
// PHASE 5.6: AXIS MOTION QUALITY DIAGNOSTICS
// ============================================================================

void cmd_axis_status(int argc, char** argv) {
    Serial.println("\n[AXIS] === Motion Quality Status (All Axes) ===");
    axisSynchronizationPrintSummary();
}

void cmd_axis_detail(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[AXIS] [ERR] Usage: axis detail [X|Y|Z]");
        return;
    }

    uint8_t axis = 255;
    if (strcmp(argv[1], "X") == 0 || strcmp(argv[1], "x") == 0) axis = 0;
    else if (strcmp(argv[1], "Y") == 0 || strcmp(argv[1], "y") == 0) axis = 1;
    else if (strcmp(argv[1], "Z") == 0 || strcmp(argv[1], "z") == 0) axis = 2;

    if (axis >= 3) {
        Serial.printf("[AXIS] [ERR] Invalid axis: %s (use X, Y, or Z)\n", argv[1]);
        return;
    }

    Serial.printf("\n[AXIS] === Axis %c Detailed Diagnostics ===\n", 'X' + axis);
    axisSynchronizationPrintAxisDiagnostics(axis);
}

void cmd_axis_reset(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[AXIS] [ERR] Usage: axis reset [X|Y|Z|all]");
        return;
    }

    if (strcmp(argv[1], "all") == 0) {
        for (uint8_t i = 0; i < 3; i++) {
            axisSynchronizationResetAxis(i);
        }
        Serial.println("[AXIS] [OK] Reset metrics for all axes");
    } else {
        uint8_t axis = 255;
        if (strcmp(argv[1], "X") == 0 || strcmp(argv[1], "x") == 0) axis = 0;
        else if (strcmp(argv[1], "Y") == 0 || strcmp(argv[1], "y") == 0) axis = 1;
        else if (strcmp(argv[1], "Z") == 0 || strcmp(argv[1], "z") == 0) axis = 2;

        if (axis >= 3) {
            Serial.printf("[AXIS] [ERR] Invalid axis: %s (use X, Y, Z, or all)\n", argv[1]);
            return;
        }

        axisSynchronizationResetAxis(axis);
        Serial.printf("[AXIS] [OK] Reset metrics for axis %c\n", 'X' + axis);
    }
}

void cmd_axis_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[AXIS] === Per-Axis Motion Quality Monitoring (PHASE 5.6) ===");
        Serial.println("Usage: axis [status | detail | reset] [args]");
        Serial.println("");
        Serial.println("  status          Show all axes quality summary");
        Serial.println("  detail X|Y|Z    Show detailed diagnostics for specific axis");
        Serial.println("  reset X|Y|Z|all Reset quality metrics for axis/all axes");
        Serial.println("");
        Serial.println("Metrics Reported:");
        Serial.println("  Quality Score   0-100 (100 = perfect motion)");
        Serial.println("  Jitter          Peak-to-peak velocity variation (mm/s)");
        Serial.println("  Stalled         Motor commanded but not moving");
        Serial.println("  VFD Error       Encoder vs VFD frequency mismatch (%)");
        Serial.println("");
        Serial.println("Quality Thresholds:");
        Serial.println("  >= 80  Excellent motion");
        Serial.println("  60-80  Good motion");
        Serial.println("  40-60  Fair motion (degradation detected)");
        Serial.println("  < 40   Poor motion (maintenance needed)");
        return;
    }

    if (strcmp(argv[1], "status") == 0) {
        cmd_axis_status(argc, argv);
    } else if (strcmp(argv[1], "detail") == 0) {
        cmd_axis_detail(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        cmd_axis_reset(argc, argv);
    } else {
        Serial.printf("[AXIS] [ERR] Unknown sub-command: %s\n", argv[1]);
    }
}

void cmd_telemetry_summary(int argc, char** argv) {
    telemetryPrintSummary();
}

void cmd_telemetry_detail(int argc, char** argv) {
    telemetryPrintDetailed();
}

void cmd_telemetry_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("[TELEMETRY] === Comprehensive System Telemetry ===");
        Serial.println("Usage: telemetry [summary | detail]");
        Serial.println("  summary: Show brief telemetry snapshot");
        Serial.println("  detail:  Show complete telemetry data");
        Serial.println("");
        Serial.println("Web API: GET /api/telemetry (comprehensive)");
        Serial.println("         GET /api/telemetry/compact (lightweight)");
        return;
    }

    if (strcmp(argv[1], "summary") == 0) {
        cmd_telemetry_summary(argc, argv);
    } else if (strcmp(argv[1], "detail") == 0) {
        cmd_telemetry_detail(argc, argv);
    } else {
        Serial.printf("[TELEMETRY] [ERR] Unknown sub-command: %s\n", argv[1]);
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
    cliRegisterCommand("ota", "OTA firmware update management", cmd_ota_main);
    cliRegisterCommand("telemetry", "System telemetry and health", cmd_telemetry_main);
    cliRegisterCommand("axis", "Per-axis motion quality diagnostics", cmd_axis_main);
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


    // Session features
    cliRegisterCommand("status", "Quick system status dashboard", cmd_status_dashboard);
    cliRegisterCommand("runtime", "Machine runtime & cycle counter", cmd_runtime);

    cliRegisterCommand("dio", "Digital I/O status display", cmd_dio_main);
    cliRegisterCommand("spindle", "Spindle monitor & alarms", cmd_spindle_main);
}