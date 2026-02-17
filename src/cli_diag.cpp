/**
 * @file cli_diag.cpp
 * @brief System Diagnostic CLI commands implementation
 * @details Handles memory, tasks, faults, and system status dashboard.
 */

#include "cli_diag.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "boot_validation.h"
#include "task_manager.h"
#include "watchdog_manager.h"
#include "timeout_manager.h"
#include "memory_monitor.h"
#include "psram_alloc.h"
#include "motion.h"
#include "config_unified.h"
#include "config_keys.h"
#include "job_manager.h"
#include "rtc_manager.h"
#include "firmware_selftest.h"
#include "safety.h"
#include <Arduino.h>
#include <WiFi.h>

// --- Runtime Stats Support ---
static uint32_t session_start_mins = 0;
static uint32_t boot_time_ms = 0;

void runtimeInit() {
    session_start_mins = configGetInt(KEY_RUNTIME_MINS, 0);
    boot_time_ms = millis();
}

void cmd_runtime(int argc, char** argv) {
    uint32_t now = millis();
    uint32_t elapsed_boot_ms = (now >= boot_time_ms) ? (now - boot_time_ms) : (UINT32_MAX - boot_time_ms + now + 1);
    uint32_t session_mins = elapsed_boot_ms / 60000;
    uint32_t total_mins = session_start_mins + session_mins;
    uint32_t cycles = configGetInt(KEY_CYCLE_COUNT, 0);
    uint32_t last_maint = configGetInt(KEY_LAST_MAINT_MINS, 0);
    uint32_t since_maint = total_mins - last_maint;
    
    if (argc >= 2) {
        if (strcasecmp(argv[1], "reset") == 0) {
            configSetInt(KEY_CYCLE_COUNT, 0);
            logPrintf("Cycle counter reset to 0\n");
            return;
        } else if (strcasecmp(argv[1], "maint") == 0) {
            configSetInt(KEY_LAST_MAINT_MINS, total_mins);
            extern void motionResetMaintenance();
            motionResetMaintenance();
            logPrintf("Maintenance recorded and axis counters reset\n");
            return;
        }
    }
    
    uint32_t hours = total_mins / 60;
    uint32_t mins = total_mins % 60;
    uint32_t maint_hours = since_maint / 60;
    
    if (!serialLoggerLock()) return;
    logDirectPrintln("\n=== Machine Usage Statistics ===\n");
    cliPrintTableHeader(23, 18, 0, 0, 0);
    cliPrintTableRow("Metric", "Value", nullptr, 23, 18, 0, nullptr, 0, nullptr, 0);
    cliPrintTableDivider(23, 18, 0, 0, 0);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu hrs %lu min", (unsigned long)hours, (unsigned long)mins);
    cliPrintTableRow("Total Runtime", buf, nullptr, 23, 18, 0, nullptr, 0, nullptr, 0);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)cycles);
    cliPrintTableRow("Job Cycles Completed", buf, nullptr, 23, 18, 0, nullptr, 0, nullptr, 0);
    snprintf(buf, sizeof(buf), "%lu hrs", (unsigned long)maint_hours);
    cliPrintTableRow("Since Last Maintenance", buf, nullptr, 23, 18, 0, nullptr, 0, nullptr, 0);
    cliPrintTableFooter(23, 18, 0, 0, 0);
    if (maint_hours >= 100) logDirectPrintln("\n[!] MAINTENANCE RECOMMENDED (100+ hours since last service)");
    Serial.flush();
    serialLoggerUnlock();
}

// --- Memory Leak Analysis Support ---
static uint32_t leak_baseline_heap = 0;
static uint32_t leak_baseline_time = 0;

void memoryLeakInit() {
    leak_baseline_heap = esp_get_free_heap_size();
    leak_baseline_time = millis();
}

void cmd_memory_leak_check(int argc, char** argv) {
    uint32_t current_heap = esp_get_free_heap_size();
    uint32_t elapsed_ms = millis() - leak_baseline_time;
    float elapsed_hours = elapsed_ms / 3600000.0f;
    
    if (!serialLoggerLock()) return;
    logDirectPrintln("\n=== Memory Leak Analysis ===\n");
    cliPrintTableHeader(23, 18, 0, 0, 0);
    cliPrintTableRow("Metric", "Value", nullptr, 23, 18, 0, nullptr, 0, nullptr, 0);
    cliPrintTableDivider(23, 18, 0, 0, 0);
    
    char buf1[32], buf2[32];
    snprintf(buf1, sizeof(buf1), "%u KB", (unsigned)(leak_baseline_heap/1024));
    snprintf(buf2, sizeof(buf2), "(%.1f hrs ago)", elapsed_hours);
    cliPrintTableRow("Baseline Heap", buf1, buf2, 23, 18, 0, nullptr, 0, nullptr, 0);
    
    snprintf(buf1, sizeof(buf1), "%u KB", (unsigned)(current_heap/1024));
    cliPrintTableRow("Current Heap", buf1, nullptr, 23, 18, 0, nullptr, 0, nullptr, 0);
    
    int32_t delta = (int32_t)current_heap - (int32_t)leak_baseline_heap;
    float delta_pct = (leak_baseline_heap > 0) ? (delta * 100.0f / leak_baseline_heap) : 0;
    snprintf(buf1, sizeof(buf1), "%+d bytes", delta);
    snprintf(buf2, sizeof(buf2), "(%+.1f%%)", delta_pct);
    cliPrintTableRow("Change", buf1, buf2, 23, 18, 0, nullptr, 0, nullptr, 0);
    
    snprintf(buf1, sizeof(buf1), "%u KB", (unsigned)(esp_get_minimum_free_heap_size()/1024));
    cliPrintTableRow("All-time Minimum", buf1, nullptr, 23, 18, 0, nullptr, 0, nullptr, 0);
    
    cliPrintTableFooter(23, 18, 0, 0, 0);
    
    if (delta_pct < -10.0f && elapsed_hours > 1.0f) logDirectPrintf("!!! POTENTIAL LEAK: >10%% loss over %.1f hours !!!\n", elapsed_hours);
    else if (delta_pct < -5.0f && elapsed_hours > 0.5f) logDirectPrintf("Gradual memory loss detected (%.1f%%)\n", delta_pct);
    else logDirectPrintln("Status: No significant leak detected");
    
    serialLoggerUnlock();
    
    if (argc >= 2 && strcasecmp(argv[1], "reset") == 0) {
        memoryLeakInit();
        logPrintf("Baseline reset to current heap\n");
    }
}

// --- Status Dashboard ---
void cmd_status_dashboard(int argc, char** argv) {
    (void)argc; (void)argv;
    watchdogFeed("CLI");
    bool verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    
    uint32_t uptime_sec = millis() / 1000;
    uint32_t hours = uptime_sec / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;
    uint32_t secs = uptime_sec % 60;
    
    extern JobManager jobManager;
    job_status_t job = jobManager.getStatus();
    
    if (!serialLoggerLock()) return;
    
    logDirectPrintln("\n+============================================================+");
    logDirectPrintf("|           BISSO E350 %-11s DASHBOARD              |\n", verbose ? "VERBOSE" : "MASTER");
    logDirectPrintln("+============================================================+");
    
    cliPrintTableHeader(15, 14, 15, 14, 0);
    char buf1[32], buf2[32];
    
    // Row 1: Uptime & CPU
    snprintf(buf1, sizeof(buf1), "%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    snprintf(buf2, sizeof(buf2), "%u%%", taskGetCpuUsage());
    cliPrintTableRow("Uptime", buf1, "System CPU", 15, 14, 15, buf2, 14, nullptr, 0);
    
    // Row 2: Heap & Job State
    snprintf(buf1, sizeof(buf1), "%u KB", (unsigned)(esp_get_free_heap_size()/1024));
    cliPrintTableRow("Free Heap", buf1, "Job State", 15, 14, 15, job.state == JOB_RUNNING ? "RUNNING" : "IDLE", 14, nullptr, 0);
    
    cliPrintTableDivider(15, 14, 15, 14, 0);
    
    // Row 3: Positions XY
    snprintf(buf1, sizeof(buf1), "%.2f", motionGetPositionMM(0));
    snprintf(buf2, sizeof(buf2), "%.2f", motionGetPositionMM(1));
    cliPrintTableRow("X Position", buf1, "Y Position", 15, 14, 15, buf2, 14, nullptr, 0);
    
    // Row 4: Positions ZA
    snprintf(buf1, sizeof(buf1), "%.2f", motionGetPositionMM(2));
    snprintf(buf2, sizeof(buf2), "%.2f", motionGetPositionMM(3));
    cliPrintTableRow("Z Position", buf1, "A Position", 15, 14, 15, buf2, 14, nullptr, 0);
    
    // Row 5: Job Progress
    snprintf(buf1, sizeof(buf1), "%lu / %lu", (unsigned long)job.current_line, (unsigned long)job.total_lines);
    cliPrintTableRow("G-Code Line", buf1, "Status", 15, 14, 15, "OK", 14, nullptr, 0);
    
    cliPrintTableFooter(15, 14, 15, 14, 0);
    logDirectPrintln("+============================================================+");
    
    serialLoggerUnlock();
}

// --- System Command Dispatchers ---
void cmd_memory_main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "reset") == 0) memoryMonitorResetMinimum();
    else memoryMonitorPrintStats();
}

void cmd_faults_main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "clear") == 0) faultClearHistory();
    else if (argc > 1 && strcmp(argv[1], "stats") == 0) {
        fault_stats_t stats = faultGetStats();
        logPrintf("Faults: %lu total\n", (unsigned long)stats.total_faults);
    } else faultShowHistory();
}

void cmd_selftest(int argc, char** argv) {
    (void)argc; (void)argv;
    selftest_suite_t suite = selftestRunSuite(SELFTEST_CAT_ALL, true);
    selftestPrintResults(&suite);
    selftestFreeResults(&suite);
}

void cmd_wdt_main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "tasks") == 0) watchdogShowTasks();
    else watchdogShowStatus();
}

void cmd_task_main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "list") == 0) taskShowAllTasks();
    else taskShowStats();
}

void cmd_timeout_diag(int argc, char** argv) { (void)argc; (void)argv; timeoutShowDiagnostics(); }

extern void systemDumpDiagnostics();
void cmd_diag_dump(int argc, char** argv) {
    (void)argc; (void)argv;
    systemDumpDiagnostics();
}

// --- Registration ---
void cliRegisterDiagCommands() {
    runtimeInit();
    memoryLeakInit();
    
    // System
    cliRegisterCommand("status", "System dashboard", cmd_status_dashboard);
    cliRegisterCommand("diag", "Full system diagnostic dump", cmd_diag_dump);
    cliRegisterCommand("runtime", "Uptime & cycle count", cmd_runtime);
    cliRegisterCommand("memory", "Heap diagnostics", cmd_memory_main);
    cliRegisterCommand("memleak", "Leak analysis", cmd_memory_leak_check);
    cliRegisterCommand("faults", "Fault history", cmd_faults_main);
    cliRegisterCommand("selftest", "Safe self-test", cmd_selftest);
    cliRegisterCommand("wdt", "Watchdog status", cmd_wdt_main);
    cliRegisterCommand("task", "Task metrics", cmd_task_main);
    cliRegisterCommand("timeouts", "Timeout monitor", cmd_timeout_diag);
    
    // Delegated to cli_diag_motion.cpp
    cliRegisterCommand("encoder", "Encoder feedback", cmd_encoder_status);
    
    // Delegated to cli_diag_hardware.cpp
    cliRegisterCommand("dio", "Digital I/O status", cmd_dio_main);
    cliRegisterCommand("spindle", "Spindle monitor", cmd_spindle_diag);
    cliRegisterCommand("rs485", "RS-485 bus diag", cmd_rs485_main);
    
    // Delegated to cli_diag_network.cpp
    cliRegisterCommand("net", "Network diagnostics", cmd_net_diag);
    
    // ... Other legacy registrations if needed ...
}
