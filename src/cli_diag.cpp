/**
 * @file cli_diag.cpp
 * @brief Diagnostic CLI commands implementation
 * @project PosiPro
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
#include "psram_alloc.h"       // PSRAM-preferred allocations
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
#include "calibration.h"
#include "encoder_calibration.h"
#include "axis_utilities.h" 
#include "input_validation.h"
#include "board_inputs.h"
#include "system_constants.h"
#include "axis_synchronization.h"  // PHASE 5.6: Per-axis motion quality diagnostics
#include "cutting_analytics.h"      // Stone cutting analytics
#include "job_manager.h"            // C2: For diag summary job status
#include "rtc_manager.h"
#include <LittleFS.h>               // Boot log file operations
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

// Forward declarations
extern uint32_t taskGetUptime();
extern void cmd_config_main(int argc, char** argv);
extern void cmd_stress_test(int argc, char** argv);

// Local handlers
void debugEncodersHandler();
void debugAllHandler();
void debugConfigHandler();
void cmd_diag_scheduler_main(int argc, char** argv);
void cmd_encoder_deviation_diag(int argc, char** argv);
void cmd_memory_detailed(int argc, char** argv); // Forward declaration

// CLI wrapper functions (match cli_handler_t signature)
static void wrap_debugAllHandler(int argc, char** argv) { (void)argc; (void)argv; debugAllHandler(); }
static void wrap_debugEncodersHandler(int argc, char** argv) { (void)argc; (void)argv; debugEncodersHandler(); }
static void wrap_debugConfigHandler(int argc, char** argv) { (void)argc; (void)argv; debugConfigHandler(); }
static void wrap_spindle_diag(int argc, char** argv) { 
    (void)argc; (void)argv; 
    spindleMonitorPrintDiagnostics();
    jxk10PrintDiagnostics();
    rs485PrintDiagnostics();
}
static void wrap_memory_stats(int argc, char** argv) { (void)argc; (void)argv; memoryMonitorPrintStats(); }
static void wrap_memory_reset(int argc, char** argv) { (void)argc; (void)argv; memoryMonitorResetMinimum(); }

// ============================================================================
// QUICK STATUS DASHBOARD
// ============================================================================
void cmd_status_dashboard(int argc, char** argv) {
    watchdogFeed("CLI");
    
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
            break;
        }
    }

    static char output[2048];
    int pos = 0;
    
    // System Metrics
    uint32_t uptime_sec = millis() / 1000;
    uint32_t hours = uptime_sec / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;
    uint32_t secs = uptime_sec % 60;
    
    uint8_t cpu = taskGetCpuUsage();
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();

    // Job Status
    extern JobManager jobManager;
    job_status_t job = jobManager.getStatus();
    const char* job_state_str = "IDLE";
    char job_progress_str[32] = "";
    
    if (job.state == JOB_RUNNING) {
        job_state_str = "RUNNING";
        float progress = job.total_lines > 0 ? (float)job.current_line / job.total_lines * 100.0f : 0.0f;
        snprintf(job_progress_str, sizeof(job_progress_str), "(%.1f%%)", progress);
    } else if (job.state == JOB_PAUSED) job_state_str = "PAUSED";
    else if (job.state == JOB_COMPLETED) job_state_str = "DONE";
    else if (job.state == JOB_ERROR) job_state_str = "ERROR";

    pos += snprintf(output + pos, sizeof(output) - pos, "\n+============================================================+\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "|           BISSO E350 %-11s DASHBOARD              |\n", verbose ? "VERBOSE" : "MASTER");
    pos += snprintf(output + pos, sizeof(output) - pos, "|  Uptime: %02u:%02u:%02u   CPU: %-3u%%   Heap: %-4u KB (Min %-3u)  |\n", 
              (unsigned int)hours, (unsigned int)mins, (unsigned int)secs, 
              cpu, (unsigned)(free_heap/1024), (unsigned)(min_heap/1024));
    pos += snprintf(output + pos, sizeof(output) - pos, "+============================================================+\n");
    
    pos += snprintf(output + pos, sizeof(output) - pos, "| MOTION COORDINATES (mm)         | JOB STATUS               |\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "|   X: %10.3f    Y: %10.3f  | State: %-8s %-9s|\n",
                  motionGetPositionMM(0), motionGetPositionMM(1), job_state_str, job_progress_str);
    pos += snprintf(output + pos, sizeof(output) - pos, "|   Z: %10.3f    A: %10.3f  | Line:  %-6lu / %-6lu   |\n",
                  motionGetPositionMM(2), motionGetPositionMM(3), 
                  (unsigned long)job.current_line, (unsigned long)job.total_lines);
    
    if (verbose) {
        pos += snprintf(output + pos, sizeof(output) - pos, "+---------------------------------+--------------------------+\n");
        pos += snprintf(output + pos, sizeof(output) - pos, "| ENCODER FEEDBACK                                          |\n");
        bool fb_active = encoderMotionIsFeedbackActive();
        pos += snprintf(output + pos, sizeof(output) - pos, "|   Status: %-48s|\n",
                      fb_active ? "[ACTIVE]" : "[DISABLED]");
        
        pos += snprintf(output + pos, sizeof(output) - pos, "+------------------------------------------------------------+\n");
        pos += snprintf(output + pos, sizeof(output) - pos, "| SPINDLE MONITORING                                        |\n");
        const spindle_monitor_state_t* spindle = spindleMonitorGetState();
        if (spindle->enabled) {
            pos += snprintf(output + pos, sizeof(output) - pos, "|   Current: %5.1f A  |  Peak: %5.1f A   |  Load: %5.1f%% |\n",
                          spindle->current_amps, spindle->current_peak_amps, spindleMonitorGetLoadPercent());
            const char* alarm = "OK";
            if (spindle->alarm_tool_breakage) alarm = "TOOL BREAK";
            else if (spindle->alarm_stall) alarm = "STALL";
            else if (spindle->alarm_overload) alarm = "OVERLOAD";
            pos += snprintf(output + pos, sizeof(output) - pos, "|   Alarm: %-10s                                      |\n", alarm);
        } else {
            pos += snprintf(output + pos, sizeof(output) - pos, "|   Status: [DISABLED]                                      |\n");
        }
        
        pos += snprintf(output + pos, sizeof(output) - pos, "+------------------------------------------------------------+\n");
        pos += snprintf(output + pos, sizeof(output) - pos, "| NETWORK                                                   |\n");
        if (WiFi.status() == WL_CONNECTED) {
            pos += snprintf(output + pos, sizeof(output) - pos, "|   WiFi: Connected (%d dBm)  | IP: %-25s|\n", WiFi.RSSI(), WiFi.localIP().toString().c_str());
        } else {
            pos += snprintf(output + pos, sizeof(output) - pos, "|   WiFi: [DISCONNECTED]                                    |\n");
        }
        
        pos += snprintf(output + pos, sizeof(output) - pos, "+------------------------------------------------------------+\n");
        pos += snprintf(output + pos, sizeof(output) - pos, "| ACTIVE FAULTS                                             |\n");
        fault_stats_t faults = faultGetStats();
        if (faults.total_faults == 0) {
            pos += snprintf(output + pos, sizeof(output) - pos, "|   [NONE] System healthy                                   |\n");
        } else {
            pos += snprintf(output + pos, sizeof(output) - pos, "|   Total: %-8lu  |  Last Wait: %-10lu sec ago        |\n",
                          (unsigned long)faults.total_faults,
                          (unsigned long)((millis() - faults.last_fault_time_ms) / 1000));
        }
    }
    
    if (emergencyStopIsActive()) {
        pos += snprintf(output + pos, sizeof(output) - pos, "+============================================================+\n");
        pos += snprintf(output + pos, sizeof(output) - pos, "|  E-STOP ACTIVE - MOTION DISABLED                          |\n");
    }

#if BOARD_HAS_RTC_DS3231
    if (rtcHasBatteryWarning()) {
        pos += snprintf(output + pos, sizeof(output) - pos, "+============================================================+\n");
        pos += snprintf(output + pos, sizeof(output) - pos, "|  WARNING: RTC BATTERY LOW - CLOCK WILL RESET ON POWER OFF |\n");
    }
#endif
    
    pos += snprintf(output + pos, sizeof(output) - pos, "+============================================================+\n");
    if (!verbose) {
        pos += snprintf(output + pos, sizeof(output) - pos, " (Use 'status -v' for full diagnostics)\n");
    }
    
    // Acquire mutex and output entire buffer at once
    serialLoggerLock();
    Serial.print(output);
    Serial.flush();
    serialLoggerUnlock();
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
            
            extern void motionResetMaintenance();
            motionResetMaintenance();
            
            logInfo("[RUNTIME] Maintenance recorded and axis counters reset");
            return;
        }
    }
    
    uint32_t hours = total_mins / 60;
    uint32_t mins = total_mins % 60;
    uint32_t maint_hours = since_maint / 60;
    
    if (!serialLoggerLock()) return;

    logDirectPrintln("\n[RUNTIME] === Machine Usage Statistics ===\n");
    
    cliPrintTableHeader(23, 18, 0);
    cliPrintTableRow("Metric", "Value", nullptr, 23, 18, 0);
    cliPrintTableDivider(23, 18, 0);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu hrs %lu min", (unsigned long)hours, (unsigned long)mins);
    cliPrintTableRow("Total Runtime", buf, nullptr, 23, 18, 0);
    
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)cycles);
    cliPrintTableRow("Job Cycles Completed", buf, nullptr, 23, 18, 0);
    
    snprintf(buf, sizeof(buf), "%lu hrs", (unsigned long)maint_hours);
    cliPrintTableRow("Since Last Maintenance", buf, nullptr, 23, 18, 0);
    
    cliPrintTableFooter(23, 18, 0);
    
    if (maint_hours >= 100) {
        logDirectPrintln("\n[!] MAINTENANCE RECOMMENDED (100+ hours since last service)");
    }

    Serial.flush();
    serialLoggerUnlock();
}

// ============================================================================
// DIGITAL I/O STATUS DISPLAY
// ============================================================================
void cmd_dio_main(int argc, char** argv) {
    (void)argc; (void)argv;
    watchdogFeed("CLI");
    
    // PHASE 16 FIX: Build entire output in buffer, then print all at once
    // This prevents concurrent task logging from interleaving with CLI output
    static char output[2048];
    int pos = 0;
    
    static const char* input1_labels[] = {"Limit-X", "Limit-Y", "Limit-Z", "E-Stop", "Pause", "Resume", "Probe", "Door"};
    static const char* input2_labels[] = {"Home-X", "Home-Y", "Home-Z", "Home-A", "ToolSns", "Coolant", "In-15", "In-16"};
    static const char* output1_labels[] = {"Spindle", "SpinDir", "Coolant", "Mist", "Clamp", "Vacuum", "Light", "Out-8"};
    static const char* output2_labels[] = {"AirBlast", "Lube", "Alarm", "Ready", "Running", "Error", "Out-15", "Out-16"};
    
    struct { uint8_t addr; const char* name; const char** labels; bool is_output; } banks[] = {
        {0x22, "INPUTS-SAFE", input1_labels, false},
        {0x21, "INPUTS-AUX", input2_labels, false},
        {0x24, "OUTPUTS-MAIN", output1_labels, true},
        {0x25, "OUTPUTS-AUX", output2_labels, true}
    };
    
    // Build header
    pos += snprintf(output + pos, sizeof(output) - pos, "\n[DIO] === Digital I/O Status ===\n\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "+---------+----------------+------------------------------------------------------------------+\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "| Addr    | Name           | State (MSB..LSB)                                                 |\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "+---------+----------------+------------------------------------------------------------------+\n");
    
    for (int b = 0; b < 4; b++) {
        Wire.beginTransmission(banks[b].addr);
        if (Wire.endTransmission() != 0) {
            pos += snprintf(output + pos, sizeof(output) - pos, "| N/A     | %-14s | [NOT CONNECTED]                                                  |\n", banks[b].name);
            continue;
        }
        
        Wire.requestFrom(banks[b].addr, (uint8_t)1);
        uint8_t state = Wire.available() ? Wire.read() : 0xFF;
        
        char bits[9];
        for (int i = 7; i >= 0; i--) bits[7-i] = (state & (1 << i)) ? '1' : '0';
        bits[8] = '\0';
        
        pos += snprintf(output + pos, sizeof(output) - pos, "| 0x%02X    | %-14s | %s (0x%02X)                                                  |\n", 
                        banks[b].addr, banks[b].name, bits, state);
        
        // Show active channels
        char active_buf[128] = "";
        int apos = 0;
        int count = 0;
        for (int i = 0; i < 8; i++) {
            bool active = banks[b].is_output ? !(state & (1 << i)) : (state & (1 << i));
            if (active) {
                if (count > 0) apos += snprintf(active_buf + apos, sizeof(active_buf) - apos, ", ");
                apos += snprintf(active_buf + apos, sizeof(active_buf) - apos, "%s", banks[b].labels[i]);
                count++;
            }
        }
        if (count == 0) snprintf(active_buf, sizeof(active_buf), "(none active)");
        pos += snprintf(output + pos, sizeof(output) - pos, "|         |                | %-64s |\n", active_buf);
    }
    
    pos += snprintf(output + pos, sizeof(output) - pos, "+---------+----------------+------------------------------------------------------------------+\n");
    pos += snprintf(output + pos, sizeof(output) - pos, "Legend: Inputs=HIGH when active, Outputs=LOW when relay ON\n");
    
    // Acquire mutex and output entire buffer at once
    serialLoggerLock();
    Serial.print(output);
    Serial.flush();
    serialLoggerUnlock();
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
        CLI_USAGE("selftest", "[command] [options]");
        CLI_HELP_LINE("(no args)", "Run comprehensive test suite");
        CLI_HELP_LINE("quick", "Quick health check (fast tests only)");
        CLI_HELP_LINE("memory", "Memory subsystem tests");
        CLI_HELP_LINE("i2c", "I2C bus and device tests");
        CLI_HELP_LINE("storage", "LittleFS and NVS tests");
        CLI_HELP_LINE("motion", "Motion system tests");
        CLI_HELP_LINE("spindle", "Spindle monitor tests");
        CLI_HELP_LINE("safety", "Safety system tests");
        CLI_HELP_LINE("network", "Network and WiFi tests");
        CLI_HELP_LINE("watchdog", "Watchdog timer tests");
        CLI_HELP_LINE("list", "List all available tests");
        CLI_HELP_LINE("help", "Show this message");
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

static void wrap_debugStack(int argc, char** argv) { (void)argc; (void)argv; taskShowAllTasks(); }

// ============================================================================
// DEBUG MAIN DISPATCHER
// ============================================================================
void cmd_debug_main(int argc, char** argv) {
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"all",      wrap_debugAllHandler,      "Dump all debug info"},
        {"encoders", wrap_debugEncodersHandler, "Encoder statistics"},
        {"config",   wrap_debugConfigHandler,   "Configuration dump"},
        {"stack",    wrap_debugStack,           "Task stack usage (HWM)"}
    };
    
    cliDispatchSubcommand("[DEBUG]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

// ============================================================================
// TEST COMMANDS (PHASE 16: Logical grouping)
// ============================================================================
extern void cmd_stress_test(int argc, char** argv);
#include "operator_alerts.h"

void cmd_test_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"sl",       [](int c, char** v){ (void)c; (void)v; statusLightTest(); }, "Status light test"},
        {"buzzer",   [](int c, char** v){ 
            int p = (c >= 2) ? atoi(v[1]) : 1;
            buzzerPlay((buzzer_pattern_t)p);
            logInfo("[TEST] Playing buzzer pattern %d", p);
        }, "Buzzer test <pattern_id>"},
        {"stress",   cmd_stress_test,           "Run system stress tests (all, jitter, etc.)"},
        {"all",      cmd_stress_test,           "Alias for 'test stress all'"}
    };
    
    cliDispatchSubcommand("[TEST]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
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

void cmd_memory_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"stats",    wrap_memory_stats,   "Quick memory snapshot"},
        {"reset",    wrap_memory_reset,   "Reset minimum free heap counter"},
        {"detailed", cmd_memory_detailed, "Deep analysis with fragmentation"}
    };
    
    cliDispatchSubcommand("[MEMORY]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

// ============================================================================
// FAULT HANDLERS
// ============================================================================
static String formatTimestamp(uint32_t timestamp_ms) {
    time_t t = timestamp_ms / 1000;
    struct tm *tm = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return String(buf);
}

void cmd_faults_stats(int argc, char** argv) {
    fault_stats_t stats = faultGetStats();
    if (!serialLoggerLock()) return;
    
    logPrintln("\n[FAULT] === Statistics ===");
    logPrintf("Total: %lu\r\n", (unsigned long)stats.total_faults);
    if (stats.total_faults > 0) {
        logPrintf("Last: %s\r\n", formatTimestamp(stats.last_fault_time_ms).c_str());
    }
    
    logPrintf("Categories: ENC:%lu, MOT:%lu, SAF:%lu, CFG:%lu, PLC:%lu, SYS:%lu\r\n",
              (unsigned long)stats.encoder_faults, (unsigned long)stats.motion_faults,
              (unsigned long)stats.safety_faults, (unsigned long)stats.config_faults,
              (unsigned long)stats.plc_faults, (unsigned long)stats.system_faults);
              
    serialLoggerUnlock();
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

// ============================================================================
// RS-485 DIRECT BUS ACCESS
// ============================================================================

// P1 DRY: Common helper for RS-485 send/receive operations
static void rs485_send_and_receive(const uint8_t* payload, size_t len) {
    rs485ClearBuffer();
    
    if (!rs485Send(payload, len)) {
        logError("[RS485] Failed to send");
        return;
    }

    vTaskDelay(200 / portTICK_PERIOD_MS);
    
    uint8_t rx_buf[128];
    uint8_t rx_len = 0;
    if (rs485Receive(rx_buf, &rx_len) && rx_len > 0) {
        logPrintf("[RS485] Received %d bytes: ", rx_len);
        for (int i = 0; i < rx_len; i++) {
            if (rx_buf[i] >= 32 && rx_buf[i] <= 126) logPrintf("%c", rx_buf[i]);
            else logPrintf("[%02X]", rx_buf[i]);
        }
        logPrintln("");
    } else {
        logWarning("[RS485] No response received");
    }
}

void cmd_rs485_raw(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[RS485] Usage: rs485 raw <string>");
        logPrintln("  Example: rs485 raw #00\\r");
        return;
    }

    // Convert escaped characters like \r to actual values
    char payload[64];
    strncpy(payload, argv[1], sizeof(payload)-1);
    payload[sizeof(payload)-1] = '\0';
    
    char* r = strstr(payload, "\\r");
    if (r) { *r = '\r'; memmove(r+1, r+2, strlen(r+2)+1); }
    char* n = strstr(payload, "\\n");
    if (n) { *n = '\n'; memmove(n+1, n+2, strlen(n+2)+1); }

    logPrintf("[RS485] Sending: %s (%d bytes)\r\n", argv[1], (int)strlen(payload));
    rs485_send_and_receive((const uint8_t*)payload, strlen(payload));
}

void cmd_rs485_hex(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[RS485] Usage: rs485 hex <hex bytes...>");
        logPrintln("  Example: rs485 hex 23 30 30 0D (sends #00\\r)");
        return;
    }

    uint8_t payload[64];
    uint8_t len = 0;
    
    for (int i = 1; i < argc && len < sizeof(payload); i++) {
        payload[len++] = (uint8_t)strtol(argv[i], NULL, 16);
    }

    logPrintf("[RS485] Sending Hex (%d bytes)\r\n", len);
    rs485_send_and_receive(payload, len);
}

void cmd_rs485_diag(int argc, char** argv) { (void)argc; (void)argv; rs485PrintDiagnostics(); }
void cmd_rs485_reset(int argc, char** argv) { (void)argc; (void)argv; rs485ResetErrorCounters(); }

void cmd_rs485_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"raw",     cmd_rs485_raw,          "Send raw string to bus and show response"},
        {"hex",     cmd_rs485_hex,          "Send hex bytes to bus (e.g. 23 30 30 0D)"},
        {"diag",    cmd_rs485_diag,         "Show bus registry diagnostics"},
        {"reset",   cmd_rs485_reset,        "Reset error counters"}
    };
    cliDispatchSubcommand("[RS485]", argc, argv, subcmds, sizeof(subcmds)/sizeof(subcmds[0]), 1);
}

void cmd_encoder_diag(int argc, char** argv) { (void)argc; (void)argv; encoderMotionDiagnostics(); }
void cmd_encoder_test(int argc, char** argv) { (void)argc; (void)argv; wj66Diagnostics(); }
void cmd_encoder_baud_detect(int argc, char** argv) { (void)argc; (void)argv; wj66Autodetect(); }

void cmd_encoder_read(int argc, char** argv) {
    int n_reads = 10;
    if (argc >= 3) {
        n_reads = atoi(argv[2]);
        if (n_reads <= 0) n_reads = 1;
    }

    logPrintf("Reading %d times (0.5s interval)...\r\n", n_reads);
    logPrintln("| Axis 0    | Axis 1    | Axis 2    | Axis 3    |");
    logPrintln("+-----------+-----------+-----------+-----------+");

    for (int i = 0; i < n_reads; i++) {
        logPrintf("| %9ld | %9ld | %9ld | %9ld |\r\n",
                  (long)wj66GetPosition(0),
                  (long)wj66GetPosition(1),
                  (long)wj66GetPosition(2),
                  (long)wj66GetPosition(3));
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void cmd_encoder_status(int argc, char** argv) {
    (void)argc; (void)argv;
    const encoder_hal_config_t* config = encoderHalGetConfig();
    if (!config) return;
    
    if (!serialLoggerLock()) return;

    logPrintln("\n=== Encoder Configuration & Status Dashboard ===");
    logPrintln("+-----------------+---------------------------------------+");
    logPrintf("| Interface       | %-37s |\r\n", encoderHalGetInterfaceName(config->interface));
    logPrintf("| Pins            | RX:%-2d TX:%-2d                             |\r\n", config->rx_pin, config->tx_pin);
    logPrintf("| Baud Rate       | %-37lu |\r\n", (unsigned long)config->baud_rate);
    int proto = configGetInt(KEY_ENC_PROTO, 0);
    logPrintf("| Protocol        | %-37s |\r\n", (proto == 1) ? "Modbus RTU" : "ASCII (#XX\\r)");
    logPrintf("| Address         | %-37d |\r\n", configGetInt(KEY_ENC_ADDR, 0));
    logPrintln("+-----------------+---------------------------------------+");

    // Helper for compact metric formatting (max 4 chars)
    auto formatMetric = [](uint32_t val, char* buf) {
        if (val < 1000) snprintf(buf, 6, "%lu", (unsigned long)val);
        else if (val < 1000000) snprintf(buf, 6, "%luK", (unsigned long)(val / 1000));
        else snprintf(buf, 6, "%luM", (unsigned long)(val / 1000000));
    };

    logPrintln("+-----------------+---------------------------------------+");
    logPrintln("| Axis | Name | Pos (Pulse)|  Pos (mm)  | Status | Age (ms)  | Reads | Missed |");
    logPrintln("+------+------+------------+------------+--------+-----------+-------+--------+");

    for (int i = 0; i < 4; i++) {
        int32_t pos = wj66GetPosition(i);
        uint32_t age = wj66GetAxisAge(i);
        
        // Center-aligned status strings (width 6)
        const char* status = wj66IsStale(i) ? "STALE " : "  OK  ";
        
        uint32_t reads = wj66GetReadCount(i);
        uint32_t polls = wj66GetPollCount();
        uint32_t missed = (polls > reads) ? (polls - reads) : 0;
        
        float ppm = machineCal.axes[i].pulses_per_mm;
        float pos_mm = (ppm > 0.001f) ? (float)pos / ppm : 0.0f;
        
        // Compact counters
        char reads_str[8];
        char missed_str[8];
        formatMetric(reads, reads_str);
        formatMetric(missed, missed_str);

        char axis_name = "XYZA"[i];

        // Status is %s (pre-padded), Reads/Missed are %5s/%6s (Right Justified)
        logPrintf("|  %d   |  %c   | %10ld | %10.3f | %s | %9lu | %5s | %6s |\r\n", 
            i, axis_name, (long)pos, pos_mm, status, (unsigned long)age, reads_str, missed_str);
    }
    logPrintln("+------+------+------------+------------+--------+-----------+-------+--------+");

    // Integration diagnostics
    serialLoggerUnlock(); // Unlock before calling sub-function that might lock again (though it's recursive)
    encoderMotionDiagnostics(); // This function handles its own locking correctly
}

void cmd_encoder_protocol(int argc, char** argv) {
    if (argc < 3) {
        int current = configGetInt(KEY_ENC_PROTO, 0);
        logPrintf("Current Protocol: %s (%d)\r\n", (current == 1) ? "Modbus RTU" : "ASCII (#XX\\r)", current);
        CLI_USAGE("encoder", "protocol <0|1>");
        logPrintln("  0: ASCII mode (Default, eg: #01\\r -> !+0000.00,...)");
        logPrintln("  1: Modbus RTU mode (Read Holding Registers FC03)");
        return;
    }

    int proto = atoi(argv[2]);
    if (proto != 0 && proto != 1) {
        logError("Invalid protocol: %d (use 0 or 1)", proto);
        return;
    }

    configSetInt(KEY_ENC_PROTO, proto);
    configUnifiedSave();
    logInfo("Protocol set to %s. Changes will take effect on next poll.", 
            (proto == 1) ? "Modbus RTU" : "ASCII");
}

// ============================================================================
// ENCODER CONFIGURATION (WJ66 INTERFACE MANAGEMENT)
// ============================================================================

void cmd_encoder_config_show(int argc, char** argv) {
    logPrintln("\n=== WJ66 Encoder Configuration ===");

    const encoder_hal_config_t* config = encoderHalGetConfig();
    if (!config) {
        logError("Unable to get HAL configuration");
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
        logInfo("Switched to %s", encoderHalGetInterfaceName(interface_type));

        // Save to NVS
        configSetInt(KEY_ENC_INTERFACE, (int)interface_type);
        logInfo("Configuration saved to NVS");
    } else {
        logError("Failed to switch interface");
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
        logError("Invalid baud rate (must be 1200-115200)");
        return;
    }

    uint32_t new_baud = (uint32_t)new_baud_i32;

    // Get current interface
    const encoder_hal_config_t* config = encoderHalGetConfig();
    encoder_interface_t interface = (config) ? config->interface : ENCODER_INTERFACE_RS232_HT;

    // Re-initialize with new baud rate
    if (encoderHalInit(interface, new_baud)) {
        logInfo("Baud rate set to %lu", (unsigned long)new_baud);

        // Save to NVS
        configSetInt(KEY_ENC_BAUD, (int)new_baud);
        logInfo("Configuration saved to NVS");
    } else {
        logError("Failed to set baud rate");
    }
}

void cmd_encoder_config_main(int argc, char** argv) {
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"show",      cmd_encoder_config_show,      "Display current configuration"},
        {"interface", cmd_encoder_config_interface, "Set encoder interface (RS232_HT or RS485_RXD2)"},
        {"baud",      cmd_encoder_config_baud,      "Set baud rate"}
    };
    
    cliDispatchSubcommand("", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 2);
}

void cmd_encoder_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"status", cmd_encoder_status,      "Unified dashboard (config + runtime)"},
        {"read",   cmd_encoder_read,        "Display encoder positions N times (default 10) every 0.5s"},
        {"diag",   cmd_encoder_diag,        "Run encoder integration diagnostics"},
        {"deviation", cmd_encoder_deviation_diag, "Encoder deviation diagnostics"},
        {"test",   cmd_encoder_test,        "Show raw encoder counts and hardware stats"},
        {"baud",   cmd_encoder_baud_detect, "Auto-detect baud rate"},
        {"scan",   cmd_encoder_baud_detect, "Alias for baud (scan for encoder)"},
        {"poll",   cmd_encoder_test,        "One-shot poll showing raw response (alias for test)"},
        {"config",   cmd_encoder_config_main, "Configure encoder interface"},
        {"protocol", cmd_encoder_protocol,    "Set protocol: 0=ASCII (#XX\\r), 1=Modbus RTU"}
    };
    
    cliDispatchSubcommand("", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
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
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"show",      cmd_spindle_config_show,      "Display current configuration"},
        {"enable",    cmd_spindle_config_enable,    "Enable/disable monitoring (on/off)"},
        {"address",   cmd_spindle_config_address,   "Set JXK-10 Modbus address (1-247)"},
        {"threshold", cmd_spindle_config_threshold, "Set overcurrent threshold (0-50 A)"},
        {"interval",  cmd_spindle_config_interval,  "Set poll interval (100-60000 ms)"}
    };
    
    cliDispatchSubcommand("[SPINDLE CONFIG]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 2);
}

void cmd_spindle_main(int argc, char** argv) {
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"diag",   wrap_spindle_diag,       "Print spindle diagnostics"},
        {"config", cmd_spindle_config_main, "Configure spindle settings"},
        {"alarm",  cmd_spindle_alarm,       "Alarm management"}
    };
    
    cliDispatchSubcommand("[SPINDLE]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
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
    logPrintf("Firmware: %s | Uptime: %lu s\r\n", ver, (unsigned long)taskGetUptime());
    
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
    if (!serialLoggerLock()) return;

    logPrintln("\n[TASK] === Detailed Task List ===");

    int task_count = taskGetStatsCount();
    if (task_count <= 0) {
        logPrintln("[TASK] No tasks registered");
        serialLoggerUnlock();
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
    
    serialLoggerUnlock();
}

void cmd_memory_detailed(int argc, char** argv) {
    // Build entire output into a local buffer to prevent interleaved corruption
    // (logPrintf acquires/releases mutex on each call, allowing other tasks to interleave)
    char buf[768];
    int pos = 0;

    extern void memoryMonitorUpdate();
    memoryMonitorUpdate();
    
    memory_stats_t* stats = memoryMonitorGetStats();
    
    uint32_t internal_total = memoryMonitorGetTotalHeap();
    uint32_t internal_free = memoryMonitorGetFreeHeap();
    uint32_t internal_used = internal_total - internal_free;
    uint32_t internal_largest = memoryMonitorGetLargestFreeBlock();

    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "\r\nInternal Heap (DRAM):\r\n"
        "  Total:      %lu bytes\r\n"
        "  Used:       %lu bytes (%.1f%%)\r\n"
        "  Free:       %lu bytes (%.1f%%)\r\n"
        "  Largest:    %lu bytes (max contiguous)\r\n"
        "  Min Free:   %lu bytes (lowest ever)\r\n",
        (unsigned long)internal_total,
        (unsigned long)internal_used, (internal_used * 100.0f) / internal_total,
        (unsigned long)internal_free, (internal_free * 100.0f) / internal_total,
        (unsigned long)internal_largest,
        (unsigned long)stats->minimum_free);

    if (internal_largest > 0 && internal_free > 0) {
        float frag = 100.0f * (1.0f - ((float)internal_largest / internal_free));
        pos += snprintf(buf + pos, sizeof(buf) - pos, "  Frag:       %.1f%%\r\n", frag);
    }

    if (stats->psram_total > 0) {
        uint32_t psram_used = stats->psram_total - stats->psram_current_free;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "\r\nExternal Heap (PSRAM):\r\n"
            "  Total:      %lu bytes\r\n"
            "  Used:       %lu bytes (%.1f%%)\r\n"
            "  Free:       %lu bytes (%.1f%%)\r\n"
            "  Largest:    %lu bytes (max contiguous)\r\n",
            (unsigned long)stats->psram_total,
            (unsigned long)psram_used, (psram_used * 100.0f) / stats->psram_total,
            (unsigned long)stats->psram_current_free, (stats->psram_current_free * 100.0f) / stats->psram_total,
            (unsigned long)stats->psram_largest_block);
        
        if (stats->psram_largest_block > 0 && stats->psram_current_free > 0) {
            float pfrag = 100.0f * (1.0f - ((float)stats->psram_largest_block / stats->psram_current_free));
            pos += snprintf(buf + pos, sizeof(buf) - pos, "  Frag:       %.1f%%\r\n", pfrag);
        }
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\r\nExternal Heap (PSRAM): NOT FOUND\r\n");
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "\r\nSamples:      %lu\r\n", (unsigned long)stats->sample_count);

    // Single atomic print — direct to Serial under mutex
    if (serialLoggerLock()) {
        Serial.print(buf);
        Serial.flush();
        serialLoggerUnlock();
    }
}

// ============================================================================
// WEB CREDENTIALS CONFIGURATION (PHASE 5.1: Security hardening)
// ============================================================================

void cmd_web_config_show(int argc, char** argv) {
    logPrintln("\n[WEB CONFIG] === Web Server Credentials ===");

    const char* username = configGetString(KEY_WEB_USERNAME, "admin");
    uint32_t pw_changed = configGetInt(KEY_WEB_PW_CHANGED, 0);

    logPrintf("Username:            %s\r\n", username);
    logPrintf("Password Changed:    %s\r\n", pw_changed ? "YES" : "NO (default)");
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
    char* json_buffer = (char*)psramMalloc(2048);  // Use PSRAM for large buffer
    if (!json_buffer) {
        logError("[CONFIG] Memory allocation failed");
        return;
    }

    size_t json_size = configExportToJSON(json_buffer, 2048);
    if (json_size == 0) {
        logError("[CONFIG] Failed to export configuration");
        psramFree(json_buffer);
        return;
    }

    // Save JSON to NVS as backup
    configSetString("config_backup_json", json_buffer);
    configUnifiedSave();

    logInfo("[CONFIG] [OK] Backup saved (%lu bytes)", (unsigned long)json_size);
    logPrintln("[CONFIG] Use 'config restore' to restore from backup");

    psramFree(json_buffer);
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
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"diag",  cmd_api_ratelimit_diag,  "Show rate limiter diagnostics"},
        {"reset", cmd_api_ratelimit_reset, "Reset all rate limit counters"}
    };
    
    cliDispatchSubcommand("[API]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
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
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"summary", cmd_metrics_summary, "Show quick performance summary"},
        {"detail",  cmd_metrics_detail,  "Show detailed task diagnostics"},
        {"reset",   cmd_metrics_reset,   "Clear all collected metrics"}
    };
    
    cliDispatchSubcommand("[METRICS]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
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
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"status", cmd_ota_status, "Show OTA update status"},
        {"cancel", cmd_ota_cancel, "Cancel current OTA operation"}
    };
    
    cliDispatchSubcommand("[OTA]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
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
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"status", cmd_axis_status, "Show all axes quality summary"},
        {"detail", cmd_axis_detail, "Show detailed diagnostics (usage: axis detail X|Y|Z)"},
        {"reset",  cmd_axis_reset,  "Reset quality metrics (usage: axis reset X|Y|Z|all)"}
    };
    
    cliDispatchSubcommand("[AXIS]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

void cmd_telemetry_summary(int argc, char** argv) {
    telemetryPrintSummary();
}

void cmd_telemetry_detail(int argc, char** argv) {
    telemetryPrintDetailed();
}

void cmd_telemetry_main(int argc, char** argv) {
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"summary", cmd_telemetry_summary, "Show brief telemetry snapshot"},
        {"detail",  cmd_telemetry_detail,  "Show complete telemetry data"}
    };
    
    cliDispatchSubcommand("[TELEMETRY]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
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
        CLI_USAGE("cutting", "[diag|start|stop|reset|depth <mm>|blade <mm>|baseline <sce>]");
    }
}

// ============================================================================
// NVS INSPECTOR COMMANDS
// ============================================================================
void cmd_nvs_main(int argc, char** argv) {
    if (argc < 2) {
        CLI_USAGE("nvs", "[stats | dump | cleanup]");
        return;
    }
    
    if (strcmp(argv[1], "stats") == 0) {
        configLogNvsStats();
    } else if (strcmp(argv[1], "dump") == 0) {
        configDumpNvsContents();
    } else if (strcmp(argv[1], "cleanup") == 0) {
        if (argc < 3 || strcmp(argv[2], "legacy") == 0) {
            configEraseNamespace("gemini_cfg");
        } else if (strcmp(argv[2], "faults") == 0) {
            faultClearHistory();
        } else {
            CLI_USAGE("nvs", "cleanup [legacy|faults]");
        }
    } else if (strcmp(argv[1], "clear") == 0) {
        logPrintln("Use 'config factory_reset' or 'nvs erase' (if implemented) to clear.");
        // We have configEraseNvs but it reboots, maybe warn user first? 
        // For now just pointing to stats/dump as requested.
    } else {
        logPrintln("Unknown subcommand");
    }
}

// ============================================================================
// BOOT LOG VIEWER
// ============================================================================
void cmd_log_boot(int argc, char** argv) {
    if (!serialLoggerLock()) return;

    logPrintln("\n[LOG] === Boot Log ===");
    
    size_t log_size = bootLogGetSize();
    logPrintf("Boot log size: %lu bytes\n\n", (unsigned long)log_size);
    
    if (log_size == 0) {
        logPrintln("(No boot log available)");
        serialLoggerUnlock();
        return;
    }
    
    // Read and print in chunks to avoid large stack allocation
    const size_t chunk_size = 512;
    char* chunk = (char*)psramMalloc(chunk_size);  // Use PSRAM for buffer
    if (!chunk) {
        logError("[LOG] Memory allocation failed");
        serialLoggerUnlock();
        return;
    }
    
    size_t bytes_read = bootLogRead(chunk, chunk_size);
    while (bytes_read > 0) {
        CLI_SERIAL.print(chunk);
        
        // If we read less than buffer size, we're done
        if (bytes_read < chunk_size - 1) break;
        
        // For full log, we'd need file offset tracking - for now just show first chunk
        break;
    }
    
    psramFree(chunk);
    logPrintln("\n[LOG] === End Boot Log ===");
    
    serialLoggerUnlock();
}

void cmd_log_enable(int argc, char** argv) {
    if (argc < 3) {
        int enabled = configGetInt(KEY_BOOTLOG_EN, 1);
        logPrintf("[LOG] Boot log capture: %s\n", enabled ? "ENABLED" : "DISABLED");
        logPrintln("[LOG] Usage: log enable [on | off]");
        return;
    }
    
    bool enable = false;
    if (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "yes") == 0 || strcmp(argv[2], "1") == 0) {
        enable = true;
    } else if (strcmp(argv[2], "off") == 0 || strcmp(argv[2], "no") == 0 || strcmp(argv[2], "0") == 0) {
        enable = false;
    } else {
        logError("[LOG] Invalid option (use: on, off)");
        return;
    }
    
    configSetInt(KEY_BOOTLOG_EN, enable ? 1 : 0);
    configUnifiedSave();
    logInfo("[LOG] Boot log capture %s (takes effect on next boot)", enable ? "ENABLED" : "DISABLED");
}

void cmd_log_delete(int argc, char** argv) {
    if (LittleFS.exists("/bootlog.txt")) {
        if (LittleFS.remove("/bootlog.txt")) {
            logInfo("[LOG] Boot log deleted");
        } else {
            logError("[LOG] Failed to delete boot log");
        }
    } else {
        logPrintln("[LOG] No boot log to delete");
    }
}

void cmd_log_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("[LOG] Usage: log [boot | enable | delete]");
        logPrintln("  boot:     Display captured boot log from last startup");
        logPrintln("  enable:   Enable/disable boot log capture (on/off)");
        logPrintln("  delete:   Delete the boot log file");
        return;
    }
    
    if (strcmp(argv[1], "boot") == 0) {
        cmd_log_boot(argc, argv);
    } else if (strcmp(argv[1], "enable") == 0) {
        cmd_log_enable(argc, argv);
    } else if (strcmp(argv[1], "delete") == 0) {
        cmd_log_delete(argc, argv);
    } else {
        logWarning("[LOG] Unknown sub-command: %s", argv[1]);
    }
}

// ============================================================================
// C2: DIAG SUMMARY - One-command critical stats dump
// ============================================================================
void cmd_diag_summary(int argc, char** argv) {
    (void)argc; (void)argv;
    
    uint32_t uptime_sec = millis() / 1000;
    uint32_t hours = uptime_sec / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;
    
    logPrintln("\n[DIAG] =========== SYSTEM SUMMARY ===========");
    logPrintf("Uptime:     %02lu:%02lu:%02lu\n", (unsigned long)hours, (unsigned long)mins, (unsigned long)(uptime_sec % 60));
    
    // Memory
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    logPrintf("Heap:       %u KB free (min: %u KB)\n", (unsigned)(free_heap/1024), (unsigned)(min_heap/1024));
    
    // CPU
    uint8_t cpu = taskGetCpuUsage();
    logPrintf("CPU:        %u%%\n", cpu);
    
    // Safety
    bool alarm = safetyIsAlarmed();
    bool estop = emergencyStopIsActive();
    logPrintf("Safety:     %s%s%s\n", 
             (!alarm && !estop) ? "OK" : "",
             alarm ? "ALARM " : "",
             estop ? "E-STOP" : "");
    
    // Spindle
    const spindle_monitor_state_t* spindle = spindleMonitorGetState();
    if (spindle->enabled) {
        logPrintf("Spindle:    %.1f A (peak %.1f A)\n", spindle->current_amps, spindle->current_peak_amps);
    }
    
    // Network
    if (WiFi.status() == WL_CONNECTED) {
        logPrintf("WiFi:       %s (%d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        logPrintln("WiFi:       Disconnected");
    }
    
    // Job
    extern JobManager jobManager;
    job_status_t job = jobManager.getStatus();
    if (job.state == JOB_RUNNING) {
        float progress = job.total_lines > 0 ? (float)job.current_line / job.total_lines * 100 : 0;
        logPrintf("Job:        %.1f%% (%lu/%lu lines)\n", progress, (unsigned long)job.current_line, (unsigned long)job.total_lines);
    } else {
        const char* states[] = {"Idle", "Running", "Paused", "Complete", "Error"};
        logPrintf("Job:        %s\n", states[job.state < 5 ? job.state : 0]);
    }
    
    // Faults
    fault_stats_t faults = faultGetStats();
    logPrintf("Faults:     %lu total\n", (unsigned long)faults.total_faults);
    
    logPrintln("=============================================");
}

// ============================================================================
// S3: MEMORY LEAK DETECTION
// ============================================================================
static uint32_t leak_baseline_heap = 0;
static uint32_t leak_baseline_time = 0;

void memoryLeakInit() {
    leak_baseline_heap = esp_get_free_heap_size();
    leak_baseline_time = millis();
}

void cmd_memory_leak_check(int argc, char** argv) {
    (void)argc; (void)argv;
    
    uint32_t current_heap = esp_get_free_heap_size();
    uint32_t elapsed_ms = millis() - leak_baseline_time;
    float elapsed_hours = elapsed_ms / 3600000.0f;
    
    logPrintln("\n[MEMORY] === Memory Leak Analysis ===");
    logPrintf("Baseline:    %u KB (set %.1f hours ago)\n", (unsigned)(leak_baseline_heap/1024), elapsed_hours);
    logPrintf("Current:     %u KB\n", (unsigned)(current_heap/1024));
    
    int32_t delta = (int32_t)current_heap - (int32_t)leak_baseline_heap;
    float delta_pct = (leak_baseline_heap > 0) ? (delta * 100.0f / leak_baseline_heap) : 0;
    
    logPrintf("Change:      %+d bytes (%+.1f%%)\n", delta, delta_pct);
    
    // Minimum heap ever seen
    size_t min_heap = esp_get_minimum_free_heap_size();
    logPrintf("All-time min: %u KB\n", (unsigned)(min_heap/1024));
    
    // Leak warning thresholds
    if (delta_pct < -10.0f && elapsed_hours > 1.0f) {
        logWarning("[MEMORY] !!! POTENTIAL LEAK: >10%% loss over %.1f hours !!!", elapsed_hours);
    } else if (delta_pct < -5.0f && elapsed_hours > 0.5f) {
        logWarning("[MEMORY] Gradual memory loss detected (%.1f%%)", delta_pct);
    } else {
        logPrintln("[MEMORY] No significant leak detected");
    }
    
    if (argc >= 2 && strcasecmp(argv[1], "reset") == 0) {
        memoryLeakInit();
        logInfo("[MEMORY] Baseline reset to current heap");
    }
}

// ============================================================================
// REGISTRATION
// ============================================================================
void cliRegisterDiagCommands() {
    // Initialize baseline tracking for runtime and memory leak detection
    runtimeInit();
    memoryLeakInit();
    
    cliRegisterCommand("memory", "Heap memory diagnostics", cmd_memory_main);
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
    cliRegisterCommand("wdt", "Watchdog management", cmd_diag_scheduler_main);
    cliRegisterCommand("task", "Task monitoring", cmd_diag_scheduler_main);

    // PHASE 2: New diagnostic commands
    // encoder_deviation is now a subcommand of 'encoder'
    cliRegisterCommand("fault_recovery", "Fault recovery status", cmd_fault_recovery_diag);
    cliRegisterCommand("task_list", "Detailed task list with stack usage", cmd_task_list_detailed);



    // Session features
    cliRegisterCommand("rs485", "RS-485 device registry diag", cmd_rs485_main);
    cliRegisterCommand("status", "Quick system status dashboard", cmd_status_dashboard);
    cliRegisterCommand("runtime", "Machine runtime & cycle counter", cmd_runtime);
    cliRegisterCommand("nvs", "NVS storage inspector", cmd_nvs_main);

    cliRegisterCommand("dio", "Digital I/O status display", cmd_dio_main);
    cliRegisterCommand("cutting", "Stone cutting analytics", cmd_cutting_main);
    
    // C2: Quick summary command
    cliRegisterCommand("diag", "System diagnostic summary", cmd_diag_summary);
    
    // S3: Memory leak detection  
    cliRegisterCommand("memleak", "Memory leak analysis", cmd_memory_leak_check);
    
    // Boot log viewer
    cliRegisterCommand("log", "Log management (boot log viewer)", cmd_log_main);

    // Filesystem management (LittleFS)
    extern void cmd_fs_ls(int argc, char** argv);
    extern void cmd_fs_df(int argc, char** argv);
    extern void cmd_fs_cat(int argc, char** argv);
    cliRegisterCommand("ls", "List LittleFS files", cmd_fs_ls);
    cliRegisterCommand("df", "Show LittleFS status", cmd_fs_df);
    cliRegisterCommand("cat", "View LittleFS file content", cmd_fs_cat);
    cliRegisterCommand("test", "Hardware and stress tests", cmd_test_main);
}
