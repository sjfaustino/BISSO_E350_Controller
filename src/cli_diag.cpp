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
#include "jxk10_modbus.h"
#include "spindle_current_monitor.h"
#include "rs485_device_registry.h"
#include "i2c_bus_recovery.h"
#include <Wire.h>
#include <WiFi.h>  // For WiFi.status() in status dashboard
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
#include "cutting_analytics.h"      // Stone cutting analytics
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
    
    logPrintln("\n+============================================================+");
    logPrintln("|           BISSO E350 QUICK STATUS DASHBOARD               |");
    logPrintf("|  Uptime: %02u:%02u:%02u                                        |\r\n", (unsigned int)hours, (unsigned int)mins, (unsigned int)secs);
    logPrintln("+============================================================+");
    
    logPrintln("| POSITION (mm)                                             |");
    logPrintf("|   X: %10.3f    Y: %10.3f                        |\r\n",
                  motionGetPosition(0) / 1000.0f, motionGetPosition(1) / 1000.0f);
    logPrintf("|   Z: %10.3f    A: %10.3f                        |\r\n",
                  motionGetPosition(2) / 1000.0f, motionGetPosition(3) / 1000.0f);
    
    logPrintln("+------------------------------------------------------------+");
    logPrintln("| ENCODER FEEDBACK                                          |");
    bool fb_active = encoderMotionIsFeedbackActive();
    logPrintf("|   Status: %s                                         |\r\n",
                  fb_active ? "[ON] " : "[OFF]");
    
    logPrintln("+------------------------------------------------------------+");
    logPrintln("| SPINDLE CURRENT                                           |");
    const spindle_monitor_state_t* spindle = spindleMonitorGetState();
    if (spindle->enabled) {
        logPrintf("|   Current: %5.1f A  |  Peak: %5.1f A                    |\r\n",
                      spindle->current_amps, spindle->current_peak_amps);
        const char* alarm = "OK";
        if (spindle->alarm_tool_breakage) alarm = "TOOL BREAK";
        else if (spindle->alarm_stall) alarm = "STALL";
        else if (spindle->alarm_overload) alarm = "OVERLOAD";
        logPrintf("|   Alarm: %-10s                                      |\r\n", alarm);
    } else {
        logPrintln("|   Status: [DISABLED]                                      |");
    }
    
    logPrintln("+------------------------------------------------------------+");
    logPrintln("| NETWORK                                                   |");
    if (WiFi.status() == WL_CONNECTED) {
        logPrintf("|   WiFi: Connected (%d dBm)                              |\r\n", WiFi.RSSI());
        logPrintf("|   IP: %-15s                                   |\r\n",
                      WiFi.localIP().toString().c_str());
    } else {
        logPrintln("|   WiFi: [DISCONNECTED]                                    |");
    }
    
    logPrintln("+------------------------------------------------------------+");
    logPrintln("| ACTIVE FAULTS                                             |");
    fault_stats_t faults = faultGetStats();
    if (faults.total_faults == 0) {
        logPrintln("|   [NONE] System healthy                                   |");
    } else {
        logPrintf("|   Total: %lu  |  Last: %lu sec ago                       |\r\n",
                      (unsigned long)faults.total_faults,
                      (unsigned long)((millis() - faults.last_fault_time_ms) / 1000));
    }
    
    if (emergencyStopIsActive()) {
        logPrintln("+============================================================+");
        logPrintln("|  E-STOP ACTIVE - MOTION DISABLED                          |");
    }
    
    logPrintln("+============================================================+");
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
            logInfo("[RUNTIME] Cycle counter reset to 0");
            return;
        } else if (strcasecmp(argv[1], "maint") == 0) {
            configSetInt(KEY_LAST_MAINT_MINS, total_mins);
            logInfo("[RUNTIME] Maintenance recorded");
            return;
        }
    }
    
    uint32_t hours = total_mins / 60;
    uint32_t mins = total_mins % 60;
    uint32_t maint_hours = since_maint / 60;
    
    logPrintln("\n[RUNTIME] === Machine Usage Statistics ===\n");
    logPrintln("+-------------------------+--------------------+");
    logPrintln("| Metric                  | Value              |");
    logPrintln("+-------------------------+--------------------+");
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu hrs %lu min", (unsigned long)hours, (unsigned long)mins);
    logPrintf("| %-23s | %-18s |\r\n", "Total Runtime", buf);
    
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)cycles);
    logPrintf("| %-23s | %-18s |\r\n", "Job Cycles Completed", buf);
    
    snprintf(buf, sizeof(buf), "%lu hrs", (unsigned long)maint_hours);
    logPrintf("| %-23s | %-18s |\r\n", "Since Last Maintenance", buf);
    
    logPrintln("+-------------------------+--------------------+");
    
    if (maint_hours >= 100) {
        logPrintln("\n[!] MAINTENANCE RECOMMENDED (100+ hours since last service)");
    }
}

// ============================================================================
// DIGITAL I/O STATUS DISPLAY
// ============================================================================
void cmd_dio_main(int argc, char** argv) {
    (void)argc; (void)argv;
    logPrintln("\n[DIO] === Digital I/O Status ===\n");
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
    
    logPrintln("+---------+----------------+------------------------+");
    logPrintln("| Addr    | Name           | State (MSB..LSB)       |");
    logPrintln("+---------+----------------+------------------------+");
    
    for (int b = 0; b < 4; b++) {
        Wire.beginTransmission(banks[b].addr);
        if (Wire.endTransmission() != 0) {
            logPrintf("| 0x%02X    | %-14s | [NOT CONNECTED]        |\r\n", banks[b].addr, banks[b].name);
            continue;
        }
        
        Wire.requestFrom(banks[b].addr, (uint8_t)1);
        uint8_t state = Wire.available() ? Wire.read() : 0xFF;
        
        char bits[9];
        for (int i = 7; i >= 0; i--) bits[7-i] = (state & (1 << i)) ? '1' : '0';
        bits[8] = '\0';
        
        logPrintf("| 0x%02X    | %-14s | %s (0x%02X)        |\r\n", banks[b].addr, banks[b].name, bits, state);
        
        // Show active channels
        char active_buf[64] = "";
        int pos = 0;
        int count = 0;
        for (int i = 0; i < 8; i++) {
            bool active = banks[b].is_output ? !(state & (1 << i)) : (state & (1 << i));
            if (active) {
                if (count > 0) pos += snprintf(active_buf + pos, sizeof(active_buf) - pos, ", ");
                pos += snprintf(active_buf + pos, sizeof(active_buf) - pos, "%s", banks[b].labels[i]);
                count++;
            }
        }
        if (count == 0) snprintf(active_buf, sizeof(active_buf), "(none active)");
        logPrintf("|         |                | %s\r\n", active_buf);
    }
    
    logPrintln("+---------+----------------+------------------------+");
    logPrintln("Legend: Inputs=HIGH when active, Outputs=LOW when relay ON");
}

// ============================================================================
// SPINDLE ALARM CLI SUBCOMMANDS
// ============================================================================
static void cmd_spindle_alarm(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("\n[SPINDLE] Alarm commands:");
        logPrintln("  spindle alarm status   - Show alarm states");
        logPrintln("  spindle alarm clear    - Clear all alarms");
        logPrintln("  spindle alarm toolbreak <amps> - Set threshold (1-20A)");
        logPrintln("  spindle alarm stall <amps> <ms> - Set stall params");
        return;
    }
    
    const spindle_monitor_state_t* state = spindleMonitorGetState();
    
    if (strcasecmp(argv[2], "status") == 0) {
        logPrintln("\n[SPINDLE] === Alarm Status ===");
        logPrintf("Tool Breakage: %s (count: %lu)\r\n", 
                     state->alarm_tool_breakage ? "ACTIVE" : "OK",
                     (unsigned long)state->tool_breakage_count);
        logPrintf("Stall:         %s (count: %lu)\r\n",
                     state->alarm_stall ? "ACTIVE" : "OK",
                     (unsigned long)state->stall_count);
        logPrintf("Thresholds: %.1f A drop, %.1f A for %lu ms\r\n", 
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
        logPrintln("\n[SELFTEST] === Self-Test Suite ===");
        logPrintln("Usage: selftest [command] [options]");
        logPrintln("  (no args)     Run comprehensive test suite");
        logPrintln("  quick         Quick health check (fast tests only)");
        logPrintln("  memory        Memory subsystem tests");
        logPrintln("  i2c           I2C bus and device tests");
        logPrintln("  storage       LittleFS and NVS tests");
        logPrintln("  motion        Motion system tests");
        logPrintln("  spindle       Spindle monitor tests");
        logPrintln("  safety        Safety system tests");
        logPrintln("  network       Network and WiFi tests");
        logPrintln("  watchdog      Watchdog timer tests");
        logPrintln("  list          List all available tests");
        logPrintln("  help          Show this message");
        return;
    }

    if (argc > 1 && strcmp(argv[1], "list") == 0) {
        selftestListTests();
        return;
    }

    if (argc > 1 && strcmp(argv[1], "quick") == 0) {
        logPrintln("\n[SELFTEST] === Quick Health Check ===");
        bool healthy = selftestQuickCheck();
        logInfo("%s", healthy ? "[OK] Quick checks passed\n" : "[FAIL] Quick checks failed\n");
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
    logPrintln(selftestGetSummary(&suite));

    // Free results
    selftestFreeResults(&suite);
}

// ============================================================================
// DEBUG MAIN DISPATCHER
// ============================================================================
void cmd_debug_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("\n[DEBUG] Usage: debug [all | encoders | config]");
        return;
    }

    if (strcmp(argv[1], "all") == 0) debugAllHandler();
    else if (strcmp(argv[1], "encoders") == 0) debugEncodersHandler();
    else if (strcmp(argv[1], "config") == 0) debugConfigHandler();
    else logWarning("[CLI] Unknown target '%s'", argv[1]);
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

void cmd_wdt_test_stall(int argc, char** argv) {
    logPrintln("\n[WDT TEST] === Watchdog Verification Test ===");
    logPrintln("[WDT TEST] WARNING: This will deliberately stall for 10 seconds");
    logPrintln("[WDT TEST] The watchdog should detect this and log a fault");
    logPrintln("[WDT TEST] System will NOT reboot during this test");
    logPrintln("\n[WDT TEST] Starting deliberate stall in 3 seconds...");

    // Give user time to read warning
    delay(1000); logPrintln("[WDT TEST] 2...");
    delay(1000); logPrintln("[WDT TEST] 1...");
    delay(1000); logPrintln("[WDT TEST] Starting stall NOW");

    // Record starting stats
    watchdog_stats_t* stats_before = watchdogGetStats();
    uint32_t timeouts_before = stats_before->timeouts_detected;
    uint32_t missed_before = stats_before->missed_ticks;

    logPrintln("[WDT TEST] CLI task will now stall for 10 seconds without feeding watchdog");

    // DELIBERATELY stall without feeding watchdog
    uint32_t stall_start = millis();
    while (millis() - stall_start < 10000) {
        // Do nothing - don't feed watchdog
        // This should trigger watchdog timeout detection
        delay(100);
    }

    logPrintln("\n[WDT TEST] Stall complete - checking watchdog response...");

    // Feed watchdog again to recover
    watchdogFeed("CLI");

    // Check if watchdog detected the stall
    watchdog_stats_t* stats_after = watchdogGetStats();
    uint32_t timeouts_after = stats_after->timeouts_detected;
    uint32_t missed_after = stats_after->missed_ticks;

    bool test_passed = (timeouts_after > timeouts_before) || (missed_after > missed_before);

    logPrintln("\n[WDT TEST] === Test Results ===");
    logPrintf("Timeouts Detected: %lu -> %lu (delta: %lu)\r\n",
                  (unsigned long)timeouts_before,
                  (unsigned long)timeouts_after,
                  (unsigned long)(timeouts_after - timeouts_before));
    logPrintf("Missed Ticks:      %lu -> %lu (delta: %lu)\r\n",
                  (unsigned long)missed_before,
                  (unsigned long)missed_after,
                  (unsigned long)(missed_after - missed_before));

    if (test_passed) {
        logPrintln("\n[WDT TEST] [PASS] Watchdog successfully detected task stall");
        logPrintln("[WDT TEST] System fault monitoring is functioning correctly");
    } else {
        logPrintln("\n[WDT TEST] [FAIL] Watchdog did NOT detect stall");
        logPrintln("[WDT TEST] WARNING: Watchdog monitoring may not be working properly");
    }

    logPrintln("\n[WDT TEST] Use 'faults show' to view logged faults");
}

void cmd_wdt_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[WDT] Usage: wdt [status | tasks | stats | report | test]");
        logPrintln("  test: Run watchdog verification test (deliberate 10s stall)");
        return;
    }
    if (strcmp(argv[1], "status") == 0) watchdogShowStatus();
    else if (strcmp(argv[1], "tasks") == 0) watchdogShowTasks();
    else if (strcmp(argv[1], "stats") == 0) watchdogShowStats();
    else if (strcmp(argv[1], "report") == 0) watchdogPrintDetailedReport();
    else if (strcmp(argv[1], "test") == 0) cmd_wdt_test_stall(argc, argv);
}

void cmd_task_main(int argc, char** argv) {
    if (argc < 2) { 
        logPrintln("[TASK] Usage: task [stats | list | cpu]");
        return;
    }
    if (strcmp(argv[1], "stats") == 0) taskShowStats();
    else if (strcmp(argv[1], "list") == 0) taskShowAllTasks();
    else if (strcmp(argv[1], "cpu") == 0) logInfo("[TASK] CPU: %u%%", taskGetCpuUsage());
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
    logPrintln("\n[FAULT] === Statistics ===");
    logPrintf("Total: %lu\n", (unsigned long)stats.total_faults);
    if (stats.total_faults > 0) {
        logPrintf("Last: %s\n", formatTimestamp(stats.last_fault_time_ms));
    }
}

void cmd_faults_main(int argc, char** argv) {
    if (argc < 2) { 
        logPrintln("[FAULTS] Usage: faults [show | stats | clear]");
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
    logPrintln("[CLI] Usage: encoder_baud_set <baud_rate>");
    return;
  }

  int32_t new_baud_rate_i32 = 0;
  if (!parseAndValidateInt(argv[1], &new_baud_rate_i32, 1200, 115200)) {
    logError("[CLI] Invalid baud rate (1200-115200).");
    return;
  }

  if (encoderSetBaudRate((uint32_t)new_baud_rate_i32)) {
    logInfo("[CLI] [OK] Encoder baud set to %ld.", (long)new_baud_rate_i32);
  } else {
    logError("[CLI] Failed to set baud rate.");
  }
}

// ============================================================================
// ENCODER CONFIGURATION (WJ66 INTERFACE MANAGEMENT)
// ============================================================================

void cmd_encoder_config_show(int argc, char** argv) {
    logPrintln("\n[ENCODER CONFIG] === WJ66 Configuration ===");

    const encoder_hal_config_t* config = encoderHalGetConfig();
    if (!config) {
        logError("[ENCODER CONFIG] Unable to get HAL configuration");
        return;
    }

    logPrintf("Interface:      %s\r\n", encoderHalGetInterfaceName(config->interface));
    logPrintf("Description:    %s\r\n", encoderHalGetInterfaceDescription(config->interface));
    logPrintf("Baud Rate:      %lu\r\n", (unsigned long)config->baud_rate);
    logPrintf("RX Pin:         %u\r\n", config->rx_pin);
    logPrintf("TX Pin:         %u\r\n", config->tx_pin);
    logPrintf("Read Interval:  %lu ms\r\n", (unsigned long)config->read_interval_ms);
    logPrintf("Timeout:        %lu ms\r\n", (unsigned long)config->timeout_ms);

    uint32_t stored_iface = configGetInt(KEY_ENC_INTERFACE, ENCODER_INTERFACE_RS232_HT);
    uint32_t stored_baud = configGetInt(KEY_ENC_BAUD, 9600);
    logPrintf("\r\nStored in NVS:  Interface=%lu, Baud=%lu\r\n", (unsigned long)stored_iface, (unsigned long)stored_baud);
}

void cmd_encoder_config_interface(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("[ENCODER CONFIG] Usage: encoder config interface [RS232_HT | RS485_RXD2 | CUSTOM]");
        logPrintln("  RS232_HT:    GPIO14/33 (HT1/HT2) - RS232 3.3V (standard)");
        logPrintln("  RS485_RXD2:  GPIO17/18 (RXD2/TXD2) - RS485 Differential (alternative)");
        logPrintln("  CUSTOM:      User-defined pins");

        // Show current
        const encoder_hal_config_t* config = encoderHalGetConfig();
        if (config) {
            logPrintf("\r\nCurrent: %s\r\n", encoderHalGetInterfaceName(config->interface));
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
        logWarning("[ENCODER CONFIG] Unknown interface: %s", argv[2]);
        return;
    }

    // Get current baud rate
    uint32_t baud_rate = configGetInt(KEY_ENC_BAUD, 9600);

    // Switch interface
    if (encoderHalSwitchInterface(interface_type, baud_rate)) {
        logInfo("[ENCODER CONFIG] Switched to %s", encoderHalGetInterfaceName(interface_type));

        // Save to NVS
        configSetInt(KEY_ENC_INTERFACE, (int)interface_type);
        logInfo("[ENCODER CONFIG] Configuration saved to NVS");
    } else {
        logError("[ENCODER CONFIG] Failed to switch interface");
    }
}

void cmd_encoder_config_baud(int argc, char** argv) {
    if (argc < 3) {
        const encoder_hal_config_t* config = encoderHalGetConfig();
        if (config) {
            logInfo("[ENCODER CONFIG] Current Baud Rate: %lu", (unsigned long)config->baud_rate);
        }
        logPrintln("[ENCODER CONFIG] Usage: encoder config baud <rate>");
        logPrintln("  Valid rates: 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200");
        return;
    }

    int32_t new_baud_i32 = 0;
    if (!parseAndValidateInt(argv[2], &new_baud_i32, 1200, 115200)) {
        logError("[ENCODER CONFIG] Invalid baud rate (must be 1200-115200)");
        return;
    }

    uint32_t new_baud = (uint32_t)new_baud_i32;

    // Get current interface
    const encoder_hal_config_t* config = encoderHalGetConfig();
    encoder_interface_t interface = (config) ? config->interface : ENCODER_INTERFACE_RS232_HT;

    // Re-initialize with new baud rate
    if (encoderHalInit(interface, new_baud)) {
        logInfo("[ENCODER CONFIG] Baud rate set to %lu", (unsigned long)new_baud);

        // Save to NVS
        configSetInt(KEY_ENC_BAUD, (int)new_baud);
        logInfo("[ENCODER CONFIG] Configuration saved to NVS");
    } else {
        logError("[ENCODER CONFIG] Failed to set baud rate");
    }
}

void cmd_encoder_config_main(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("\n[ENCODER CONFIG] Usage: encoder config [show | interface | baud]");
        logPrintln("  show:       Display current configuration");
        logPrintln("  interface:  Set encoder interface (RS232_HT or RS485_RXD2)");
        logPrintln("  baud:       Set baud rate");
        return;
    }

    if (strcmp(argv[2], "show") == 0) {
        cmd_encoder_config_show(argc, argv);
    } else if (strcmp(argv[2], "interface") == 0) {
        cmd_encoder_config_interface(argc, argv);
    } else if (strcmp(argv[2], "baud") == 0) {
        cmd_encoder_config_baud(argc, argv);
    } else {
        logWarning("[ENCODER CONFIG] Unknown sub-command: %s", argv[2]);
    }
}

void cmd_encoder_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[ENCODER] Usage: encoder [diag | baud | config]");
        return;
    }
    if (strcmp(argv[1], "diag") == 0) cmd_encoder_diag(argc, argv);
    else if (strcmp(argv[1], "baud") == 0) cmd_encoder_baud_detect(argc, argv);
    else if (strcmp(argv[1], "config") == 0) cmd_encoder_config_main(argc, argv);
    else logWarning("[ENCODER] Unknown sub-command: %s", argv[1]);
}

// ============================================================================
// SPINDLE CURRENT SENSOR CONFIGURATION (JXK-10 MANAGEMENT)
// ============================================================================

void cmd_spindle_config_show(int argc, char** argv) {
    logPrintln("\n[SPINDLE CONFIG] === JXK-10 Configuration ===");

    const spindle_monitor_state_t* state = spindleMonitorGetState();
    if (!state) {
        logError("[SPINDLE CONFIG] Unable to get spindle state");
        return;
    }

    logPrintf("Status:              %s\r\n", state->enabled ? "ENABLED" : "DISABLED");
    logPrintf("JXK-10 Address:      %u\r\n", state->jxk10_slave_address);
    logPrintf("Baud Rate:           %lu bps\r\n", (unsigned long)state->jxk10_baud_rate);
    logPrintf("Overcurrent Thresh:  %.1f A\r\n", state->overcurrent_threshold_amps);
    logPrintf("Poll Interval:       %lu ms\r\n", (unsigned long)state->poll_interval_ms);

    uint32_t stored_enabled = configGetInt(KEY_SPINDLE_ENABLED, 1);
    uint32_t stored_addr = configGetInt(KEY_SPINDLE_ADDRESS, 1);
    uint32_t stored_thresh = configGetInt(KEY_SPINDLE_THRESHOLD, 30);
    uint32_t stored_poll = configGetInt(KEY_SPINDLE_POLL_MS, 1000);

    logPrintf("\r\nStored in NVS:       Enabled=%lu, Address=%lu, Threshold=%luA, Poll=%lums\r\n",
                  (unsigned long)stored_enabled,
                  (unsigned long)stored_addr,
                  (unsigned long)stored_thresh,
                  (unsigned long)stored_poll);
}

void cmd_spindle_config_enable(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("[SPINDLE CONFIG] Usage: spindle config enable [on | off]");
        logInfo("Current status: %s", spindleMonitorIsEnabled() ? "ON" : "OFF");
        return;
    }

    bool enable = false;
    if (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "yes") == 0 || strcmp(argv[2], "1") == 0) {
        enable = true;
    } else if (strcmp(argv[2], "off") == 0 || strcmp(argv[2], "no") == 0 || strcmp(argv[2], "0") == 0) {
        enable = false;
    } else {
        logError("[SPINDLE CONFIG] Invalid option (use: on, off)");
        return;
    }

    spindleMonitorSetEnabled(enable);
    configSetInt(KEY_SPINDLE_ENABLED, enable ? 1 : 0);
    logInfo("[SPINDLE CONFIG] Spindle monitoring %s and saved to NVS",
                  enable ? "ENABLED" : "DISABLED");
}

void cmd_spindle_config_address(int argc, char** argv) {
    if (argc < 3) {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        logInfo("[SPINDLE CONFIG] Current JXK-10 Address: %u", state->jxk10_slave_address);
        logPrintln("[SPINDLE CONFIG] Usage: spindle config address <1-247>");
        return;
    }

    int32_t addr_i32 = 0;
    if (!parseAndValidateInt(argv[2], &addr_i32, 1, 247)) {
        logError("[SPINDLE CONFIG] Invalid address (must be 1-247)");
        return;
    }

    uint8_t addr = (uint8_t)addr_i32;
    configSetInt(KEY_SPINDLE_ADDRESS, (int)addr);
    logInfo("[SPINDLE CONFIG] JXK-10 address set to %u and saved to NVS", addr);
    logPrintln("[SPINDLE CONFIG] Restart system to apply address change");
    logPrintln("[SPINDLE CONFIG] NOTE: Ensure JXK-10 DIP switches match this address");
}

void cmd_spindle_config_threshold(int argc, char** argv) {
    if (argc < 3) {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        logPrintf("[SPINDLE CONFIG] Current Threshold: %.1f A\n", state->overcurrent_threshold_amps);
        logPrintln("[SPINDLE CONFIG] Usage: spindle config threshold <0-50>");
        return;
    }

    float threshold = atof(argv[2]);
    if (threshold < 0.0f || threshold > 50.0f) {
        logError("[SPINDLE CONFIG] Invalid threshold (must be 0.0-50.0 A)");
        return;
    }

    spindleMonitorSetThreshold(threshold);
    configSetInt(KEY_SPINDLE_THRESHOLD, (int)threshold);
    logPrintf("[SPINDLE CONFIG] Overcurrent threshold set to %.1f A and saved to NVS\n", threshold);
}

void cmd_spindle_config_interval(int argc, char** argv) {
    if (argc < 3) {
        const spindle_monitor_state_t* state = spindleMonitorGetState();
        logInfo("[SPINDLE CONFIG] Current Poll Interval: %lu ms",
                      (unsigned long)state->poll_interval_ms);
        logPrintln("[SPINDLE CONFIG] Usage: spindle config interval <100-60000>");
        return;
    }

    int32_t interval_i32 = 0;
    if (!parseAndValidateInt(argv[2], &interval_i32, 100, 60000)) {
        logError("[SPINDLE CONFIG] Invalid interval (must be 100-60000 ms)");
        return;
    }

    uint32_t interval = (uint32_t)interval_i32;
    spindleMonitorSetPollInterval(interval);
    configSetInt(KEY_SPINDLE_POLL_MS, (int)interval);
    logInfo("[SPINDLE CONFIG] Poll interval set to %lu ms and saved to NVS",
                  (unsigned long)interval);
}

void cmd_spindle_config_main(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("\n[SPINDLE CONFIG] Usage: spindle config [show | enable | address | threshold | interval]");
        logPrintln("  show:       Display current configuration");
        logPrintln("  enable:     Enable/disable monitoring (on/off)");
        logPrintln("  address:    Set JXK-10 Modbus address (1-247)");
        logPrintln("  threshold:  Set overcurrent threshold (0-50 A)");
        logPrintln("  interval:   Set poll interval (100-60000 ms)");
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
        logWarning("[SPINDLE CONFIG] Unknown sub-command: %s", argv[2]);
    }
}

void cmd_spindle_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[SPINDLE] Usage: spindle [diag | config | alarm]");
        return;
    }

    if (strcmp(argv[1], "diag") == 0) {
        spindleMonitorPrintDiagnostics();
        jxk10PrintDiagnostics();
        rs485PrintDiagnostics();
    } else if (strcmp(argv[1], "config") == 0) {
        cmd_spindle_config_main(argc, argv);
    } else if (strcmp(argv[1], "alarm") == 0) {
        cmd_spindle_alarm(argc, argv);
    } else {
        logWarning("[SPINDLE] Unknown sub-command: %s", argv[1]);
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
    logPrintln("[DEBUG] -- Encoder Status --");
    wj66Diagnostics();
}

void debugConfigHandler() {
    logPrintln("[DEBUG] -- Config Status --");
    configUnifiedDiagnostics();
}

void debugAllHandler() {
    logPrintln("\n[DEBUG] === FULL SYSTEM DUMP ===");
    char ver[FIRMWARE_VERSION_STRING_LEN]; 
    firmwareGetVersionString(ver, sizeof(ver));
    logPrintf("Firmware: %s | Uptime: %lu s\n", ver, (unsigned long)taskGetUptime());
    
    debugEncodersHandler();
    motionDiagnostics();
    safetyDiagnostics();
    
    elboDiagnostics();
    
    configUnifiedDiagnostics();
    watchdogShowStatus();
    taskShowStats();
    logPrintln("[DEBUG] === END DUMP ===");
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
    logPrintln("\n[TASK] === Detailed Task List ===");

    int task_count = taskGetStatsCount();
    if (task_count <= 0) {
        logPrintln("[TASK] No tasks registered");
        return;
    }

    task_stats_t* tasks = taskGetStatsArray();

    // Header
    logPrintln("\nTask Name          | Priority | Stack HWM | Runs    | Time(ms)  | Max(ms)");
    logPrintln("-------------------|----------|-----------|---------|-----------|--------");

    for (int i = 0; i < task_count; i++) {
        const task_stats_t* task = &tasks[i];
        if (task->handle == NULL) continue;

        logPrintf("%-18s | %8u | %9u | %7lu | %9lu | %7lu\r\n",
            (task->name ? task->name : "UNKNOWN"),
            (unsigned int)task->priority,
            (unsigned int)task->stack_high_water,
            (unsigned long)task->run_count,
            (unsigned long)task->total_time_ms,
            (unsigned long)task->max_run_time_ms);
    }

    logPrintln("\nNote: Stack HWM = High Water Mark (bytes still available)");
    logPrintln("      Time = Total cumulative time");
}

void cmd_memory_detailed(int argc, char** argv) {
    logPrintln("\n[MEMORY] === Detailed Memory Analysis ===");

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

    logPrintf("\r\nHeap Summary:\r\n");
    logPrintf("  Total:      %lu bytes\r\n", (unsigned long)total);
    logPrintf("  Used:       %lu bytes (%.1f%%)\r\n", (unsigned long)used, (used * 100.0f) / total);
    logPrintf("  Free:       %lu bytes (%.1f%%)\r\n", (unsigned long)free, (free * 100.0f) / total);
    logPrintf("  Minimum:    %lu bytes (lowest ever)\r\n", (unsigned long)min_free);
    logPrintf("  Largest Block: %lu bytes (max contiguous)\r\n", (unsigned long)largest);

    // Fragmentation estimate
    if (largest > 0 && free > 0) {
        float fragmentation = 100.0f * (1.0f - ((float)largest / free));
        logPrintf("\r\nFragmentation: %.1f%%\r\n", fragmentation);
        if (fragmentation > 50) {
            logPrintln("[WARN] High memory fragmentation detected!");
        }
    }
}

// ============================================================================
// WEB CREDENTIALS CONFIGURATION (PHASE 5.1: Security hardening)
// ============================================================================

void cmd_web_config_show(int argc, char** argv) {
    logPrintln("\n[WEB CONFIG] === Web Server Credentials ===");

    const char* username = configGetString(KEY_WEB_USERNAME, "admin");
    uint32_t pw_changed = configGetInt(KEY_WEB_PW_CHANGED, 0);

    logPrintf("Username:            %s\n", username);
    logPrintf("Password Changed:    %s\n", pw_changed ? "YES" : "NO (default)");
    if (pw_changed == 0) {
        logWarning("[WEB CONFIG] Using default password! Please set a new password.");
        logPrintln("[WEB CONFIG] Usage: web config password <password>");
    }
}

void cmd_web_config_username(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("[WEB CONFIG] Usage: web config username <username>");
        logPrintf("Current:   %s\n", configGetString(KEY_WEB_USERNAME, "admin"));
        logPrintln("Limits:    3-32 characters");
        return;
    }

    const char* username = argv[2];
    if (strlen(username) < 3 || strlen(username) > 32) {
        logError("[WEB CONFIG] Username must be 3-32 characters");
        return;
    }

    configSetString(KEY_WEB_USERNAME, username);
    configUnifiedSave();
    webServer.loadCredentials();

    logInfo("[WEB CONFIG] [OK] Username set to '%s' and saved to NVS", username);
}

void cmd_web_config_password(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("[WEB CONFIG] Usage: web config password <password>");
        logPrintln("Limits:    4-64 characters");
        return;
    }

    const char* password = argv[2];
    if (strlen(password) < 4 || strlen(password) > 64) {
        logError("[WEB CONFIG] Password must be 4-64 characters");
        return;
    }

    configSetString(KEY_WEB_PASSWORD, password);
    configSetInt(KEY_WEB_PW_CHANGED, 1);
    configUnifiedSave();
    webServer.loadCredentials();

    logInfo("[WEB CONFIG] [OK] Password updated and saved to NVS");
    logWarning("[WEB CONFIG] Password is stored in plaintext in NVS");
}

void cmd_web_config_main(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("\n[WEB CONFIG] Usage: web config [show | username | password]");
        logPrintln("  show:       Display current configuration");
        logPrintln("  username:   Set web server username (3-32 chars)");
        logPrintln("  password:   Set web server password (4-64 chars)");
        return;
    }

    if (strcmp(argv[2], "show") == 0) {
        cmd_web_config_show(argc, argv);
    } else if (strcmp(argv[2], "username") == 0) {
        cmd_web_config_username(argc, argv);
    } else if (strcmp(argv[2], "password") == 0) {
        cmd_web_config_password(argc, argv);
    } else {
        logWarning("[WEB CONFIG] Unknown sub-command: %s", argv[2]);
    }
}

void cmd_web_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[WEB] Usage: web [config]");
        return;
    }

    if (strcmp(argv[1], "config") == 0) {
        cmd_web_config_main(argc, argv);
    } else {
        logWarning("[WEB] Unknown sub-command: %s", argv[1]);
    }
}

// ============================================================================
// CONFIG BACKUP/RESTORE (PHASE 5.1: Data Management)
// ============================================================================

void cmd_config_backup(int argc, char** argv) {
    logPrintln("\n[CONFIG] === Backup Configuration ===");
    logPrintln("Saving all NVS configuration to 'config_backup' key...");

    // Export entire config to JSON
    extern size_t configExportToJSON(char* buffer, size_t buffer_size);
    char* json_buffer = (char*)malloc(2048);
    if (!json_buffer) {
        logError("[CONFIG] Memory allocation failed");
        return;
    }

    size_t json_size = configExportToJSON(json_buffer, 2048);
    if (json_size == 0) {
        logError("[CONFIG] Failed to export configuration");
        free(json_buffer);
        return;
    }

    // Save JSON to NVS as backup
    configSetString("config_backup_json", json_buffer);
    configUnifiedSave();

    logInfo("[CONFIG] [OK] Backup saved (%lu bytes)", (unsigned long)json_size);
    logPrintln("[CONFIG] Use 'config restore' to restore from backup");

    free(json_buffer);
}

void cmd_config_restore(int argc, char** argv) {
    logPrintln("\n[CONFIG] === Restore Configuration ===");

    // Load backup JSON from NVS
    const char* backup_json = configGetString("config_backup_json", NULL);
    if (!backup_json) {
        logError("[CONFIG] No backup found");
        return;
    }

    logPrintln("[CONFIG] Restoring configuration from backup...");
    logPrintln("[CONFIG] Backup JSON (first 256 chars):");
    char preview[257];
    int len = 0;
    for (int i = 0; i < 256 && backup_json[i]; i++) {
        preview[len++] = backup_json[i];
    }
    preview[len] = '\0';
    logPrintln(preview);
    logPrintln("\n");
    logPrintln("[CONFIG] [OK] Backup restored");
    logPrintln("[CONFIG] Review with: config show");
}

void cmd_config_show_backup(int argc, char** argv) {
    const char* backup = configGetString("config_backup_json", NULL);
    if (!backup) {
        logPrintln("[CONFIG] No backup exists");
        return;
    }

    logPrintln("\n[CONFIG] === Stored Backup ===");
    logPrintln(backup);
    logPrintln("");
}

void cmd_config_clear_backup(int argc, char** argv) {
    configSetString("config_backup_json", "");
    configUnifiedSave();
    logInfo("[CONFIG] [OK] Backup cleared");
}

// ============================================================================
// API RATE LIMITER (PHASE 5.1)
// ============================================================================
void cmd_api_ratelimit_diag(int argc, char** argv) {
    apiRateLimiterDiagnostics();
}

void cmd_api_ratelimit_reset(int argc, char** argv) {
    apiRateLimiterReset();
    logInfo("[OK] API rate limiter reset");
}

void cmd_api_ratelimit_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[API] Usage: api [diag | reset]");
        logPrintln("  diag:   Show rate limiter diagnostics");
        logPrintln("  reset:  Reset all rate limit counters");
        return;
    }

    if (strcmp(argv[1], "diag") == 0) {
        cmd_api_ratelimit_diag(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        cmd_api_ratelimit_reset(argc, argv);
    } else {
        logWarning("[API] Unknown sub-command: %s", argv[1]);
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
    logInfo("[METRICS] [OK] Performance metrics reset");
}

void cmd_metrics_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[METRICS] === Task Performance Monitoring ===");
        logPrintln("Usage: metrics [summary | detail | reset]");
        logPrintln("  summary: Show quick performance summary");
        logPrintln("  detail:  Show detailed task diagnostics");
        logPrintln("  reset:   Clear all collected metrics");
        return;
    }

    if (strcmp(argv[1], "summary") == 0) {
        cmd_metrics_summary(argc, argv);
    } else if (strcmp(argv[1], "detail") == 0) {
        cmd_metrics_detail(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        cmd_metrics_reset(argc, argv);
    } else {
        logWarning("[METRICS] Unknown sub-command: %s", argv[1]);
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
    logInfo("[OTA] [OK] OTA update cancelled");
}

void cmd_ota_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[OTA] === Firmware Update Management ===");
        logPrintln("Usage: ota [status | cancel]");
        logPrintln("  status: Show OTA update status");
        logPrintln("  cancel: Cancel current OTA operation");
        logPrintln("");
        logPrintln("NOTE: Binary upload via /api/update endpoint");
        return;
    }

    if (strcmp(argv[1], "status") == 0) {
        cmd_ota_status(argc, argv);
    } else if (strcmp(argv[1], "cancel") == 0) {
        cmd_ota_cancel(argc, argv);
    } else {
        logWarning("[OTA] Unknown sub-command: %s", argv[1]);
    }
}

// ============================================================================
// SYSTEM TELEMETRY (PHASE 5.1)
// ============================================================================

// ============================================================================
// PHASE 5.6: AXIS MOTION QUALITY DIAGNOSTICS
// ============================================================================

void cmd_axis_status(int argc, char** argv) {
    logPrintln("\n[AXIS] === Motion Quality Status (All Axes) ===");
    axisSynchronizationPrintSummary();
}

void cmd_axis_detail(int argc, char** argv) {
    if (argc < 2) {
        logError("[AXIS] Usage: axis detail [X|Y|Z]");
        return;
    }

    uint8_t axis = 255;
    if (strcmp(argv[1], "X") == 0 || strcmp(argv[1], "x") == 0) axis = 0;
    else if (strcmp(argv[1], "Y") == 0 || strcmp(argv[1], "y") == 0) axis = 1;
    else if (strcmp(argv[1], "Z") == 0 || strcmp(argv[1], "z") == 0) axis = 2;

    if (axis >= 3) {
        logError("[AXIS] Invalid axis: %s (use X, Y, or Z)", argv[1]);
        return;
    }

    logPrintf("\n[AXIS] === Axis %c Detailed Diagnostics ===\n", 'X' + axis);
    axisSynchronizationPrintAxisDiagnostics(axis);
}

void cmd_axis_reset(int argc, char** argv) {
    if (argc < 2) {
        logError("[AXIS] Usage: axis reset [X|Y|Z|all]");
        return;
    }

    if (strcmp(argv[1], "all") == 0) {
        for (uint8_t i = 0; i < 3; i++) {
            axisSynchronizationResetAxis(i);
        }
        logInfo("[AXIS] [OK] Reset metrics for all axes");
    } else {
        uint8_t axis = 255;
        if (strcmp(argv[1], "X") == 0 || strcmp(argv[1], "x") == 0) axis = 0;
        else if (strcmp(argv[1], "Y") == 0 || strcmp(argv[1], "y") == 0) axis = 1;
        else if (strcmp(argv[1], "Z") == 0 || strcmp(argv[1], "z") == 0) axis = 2;

        if (axis >= 3) {
            logError("[AXIS] Invalid axis: %s (use X, Y, Z, or all)", argv[1]);
            return;
        }

        axisSynchronizationResetAxis(axis);
        logInfo("[AXIS] [OK] Reset metrics for axis %c", 'X' + axis);
    }
}

void cmd_axis_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[AXIS] === Per-Axis Motion Quality Monitoring (PHASE 5.6) ===");
        logPrintln("Usage: axis [status | detail | reset] [args]");
        logPrintln("");
        logPrintln("  status          Show all axes quality summary");
        logPrintln("  detail X|Y|Z    Show detailed diagnostics for specific axis");
        logPrintln("  reset X|Y|Z|all Reset quality metrics for axis/all axes");
        logPrintln("");
        logPrintln("Metrics Reported:");
        logPrintln("  Quality Score   0-100 (100 = perfect motion)");
        logPrintln("  Jitter          Peak-to-peak velocity variation (mm/s)");
        logPrintln("  Stalled         Motor commanded but not moving");
        logPrintln("  VFD Error       Encoder vs VFD frequency mismatch (%)");
        logPrintln("");
        logPrintln("Quality Thresholds:");
        logPrintln("  >= 80  Excellent motion");
        logPrintln("  60-80  Good motion");
        logPrintln("  40-60  Fair motion (degradation detected)");
        logPrintln("  < 40   Poor motion (maintenance needed)");
        return;
    }

    if (strcmp(argv[1], "status") == 0) {
        cmd_axis_status(argc, argv);
    } else if (strcmp(argv[1], "detail") == 0) {
        cmd_axis_detail(argc, argv);
    } else if (strcmp(argv[1], "reset") == 0) {
        cmd_axis_reset(argc, argv);
    } else {
        logWarning("[AXIS] Unknown sub-command: %s", argv[1]);
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
        logPrintln("[TELEMETRY] === Comprehensive System Telemetry ===");
        logPrintln("Usage: telemetry [summary | detail]");
        logPrintln("  summary: Show brief telemetry snapshot");
        logPrintln("  detail:  Show complete telemetry data");
        logPrintln("");
        logPrintln("Web API: GET /api/telemetry (comprehensive)");
        logPrintln("         GET /api/telemetry/compact (lightweight)");
        return;
    }

    if (strcmp(argv[1], "summary") == 0) {
        cmd_telemetry_summary(argc, argv);
    } else if (strcmp(argv[1], "detail") == 0) {
        cmd_telemetry_detail(argc, argv);
    } else {
        logWarning("[TELEMETRY] Unknown sub-command: %s", argv[1]);
    }
}

// ============================================================================
// RS-485 REGISTRY DIAGNOSTICS
// ============================================================================
void cmd_rs485_diag(int argc, char** argv) {
    (void)argc; (void)argv;
    rs485PrintDiagnostics();
}

void cmd_rs485_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[RS485] Usage: rs485 diag");
        return;
    }
    if (strcmp(argv[1], "diag") == 0) cmd_rs485_diag(argc, argv);
    else logWarning("[RS485] Unknown sub-command: %s", argv[1]);
}

// ============================================================================
// CUTTING ANALYTICS COMMANDS
// ============================================================================
void cmd_cutting_main(int argc, char** argv) {
    if (argc < 2) {
        cuttingPrintDiagnostics();
        return;
    }
    
    if (strcmp(argv[1], "diag") == 0) {
        cuttingPrintDiagnostics();
    } else if (strcmp(argv[1], "start") == 0) {
        cuttingStartSession();
        logInfo("[CUTTING] Session started");
    } else if (strcmp(argv[1], "stop") == 0) {
        cuttingEndSession();
        logInfo("[CUTTING] Session stopped");
    } else if (strcmp(argv[1], "reset") == 0) {
        cuttingResetStats();
    } else if (strcmp(argv[1], "depth") == 0 && argc >= 3) {
        float depth = atof(argv[2]);
        cuttingSetDepth(depth);
    } else if (strcmp(argv[1], "blade") == 0 && argc >= 3) {
        float width = atof(argv[2]);
        cuttingSetBladeWidth(width);
    } else if (strcmp(argv[1], "baseline") == 0 && argc >= 3) {
        float sce = atof(argv[2]);
        cuttingSetSCEBaseline(sce);
    } else {
        logPrintln("Usage: cutting [diag|start|stop|reset|depth <mm>|blade <mm>|baseline <sce>]");
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
    cliRegisterCommand("rs485", "RS-485 device registry diag", cmd_rs485_main);
    cliRegisterCommand("status", "Quick system status dashboard", cmd_status_dashboard);
    cliRegisterCommand("runtime", "Machine runtime & cycle counter", cmd_runtime);

    cliRegisterCommand("dio", "Digital I/O status display", cmd_dio_main);
    cliRegisterCommand("spindle", "Spindle monitor & alarms", cmd_spindle_main);
    cliRegisterCommand("cutting", "Stone cutting analytics", cmd_cutting_main);
}
