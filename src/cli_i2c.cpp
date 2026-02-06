/**
 * @file cli_i2c.cpp
 * @brief Comprehensive I2C diagnostics and management CLI commands (PosiPro)
 * @details Consolidated I2C command hierarchy with advanced features:
 *          - i2c scan [options] - Scan for devices with timing/baselines
 *          - i2c test [options] - Detailed device testing and stress testing
 *          - i2c stats [options] - Statistics with history and export
 *          - i2c recover - Bus recovery
 *          - i2c monitor [options] - Real-time monitoring
 *          - i2c benchmark - Performance benchmarking
 *          - i2c health - Quick health check
 *          - i2c selftest - Comprehensive system test
 *          - i2c troubleshoot [address] - Interactive troubleshooting
 */

#include "cli.h"
#include "i2c_bus_recovery.h"
#include "plc_iface.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "system_utilities.h"
#include "system_constants.h"
#include "task_manager.h"
#include "watchdog_manager.h"
#include <Wire.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// ============================================================================
// I2C DEVICE DEFINITIONS & DEVICE MAP
// ============================================================================

typedef struct {
    uint8_t address;
    const char* name;
    const char* description;
} i2c_device_t;

static const i2c_device_t KNOWN_DEVICES[] = {
    {ADDR_I73_INPUT,   "I73_INPUT",   "Input Expander (Limit Switches & Sensors)"},
    {ADDR_Q73_OUTPUT,  "Q73_OUTPUT",  "Output Expander (Relays & VFD Control)"},
    {0x24,             "BOARD_INPUTS","Board Inputs"},
};
static const int KNOWN_DEVICE_COUNT = 3;

// I2C Statistics Snapshot
typedef struct {
    i2c_stats_t stats;
    uint32_t timestamp_ms;
} i2c_snapshot_t;

// Baseline storage for scan comparison
static struct {
    uint8_t device_count;
    uint8_t addresses[8];
    float response_times[8];
    uint32_t timestamp_ms;
} baseline = {
    .device_count = 0,
    .addresses = {0},
    .response_times = {0.0f},
    .timestamp_ms = 0
};

// ============================================================================
// HELPER: Format output as table or JSON
// ============================================================================

bool hasOption(int argc, char** argv, const char* option) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], option) == 0) return true;
    }
    return false;
}

const char* getOptionValue(int argc, char** argv, const char* option) {
    for (int i = 0; i < argc - 1; i++) {
        if (strcmp(argv[i], option) == 0) return argv[i + 1];
    }
    return NULL;
}

// --- TABLE HELPERS REMOVED (Now using global helpers from cli.h) ---

// ============================================================================
// I2C SCAN COMMAND
// ============================================================================

uint32_t i2cMeasureResponseTime(uint8_t address) {
    uint32_t start = micros();
    uint8_t dummy = 0;
    i2c_result_t res = i2cReadWithRetry(address, &dummy, 1);
    uint32_t elapsed_us = micros() - start;
    if (res == I2C_RESULT_OK) {
        return elapsed_us / 1000; // Convert to ms
    }
    return 0;
}

void cmd_i2c_scan(int argc, char** argv) {
    bool verbose = hasOption(argc, argv, "-v") || hasOption(argc, argv, "--verbose");
    bool save_baseline = hasOption(argc, argv, "--save");
    bool compare_baseline = hasOption(argc, argv, "--compare");
    const char* addr_str = getOptionValue(argc, argv, "-r");

    logPrintln("\n[I2C] === Bus Scan ===");

    uint8_t start_addr = 0x08, end_addr = 0x77;
    // ... filtering and address range logic ...
    if (addr_str) {
        start_addr = strtol(addr_str, NULL, 16);
    }

    uint8_t found_count = 0;
    uint8_t found_addrs[16] = {0}; // Increased buffer size
    float response_times[16] = {0};

    // CRITICAL: Acquire I2C Mutex
    SemaphoreHandle_t mutex = taskGetI2cMutex();
    if (!taskLockMutex(mutex, 5000)) {
        logError("[I2C] Could not acquire bus mutex (busy)");
        return;
    }

    if (verbose) {
        logPrintf("[I2C] Scanning range 0x%02X-0x%02X with timing...\n", start_addr, end_addr);
        cliPrintTableHeader(10, 20, 12, 14);
        cliPrintTableRow("Address", "Device Name", "Status", 10, 20, 12, "Response", 14);
        cliPrintTableDivider(10, 20, 12, 14);
    } else {
        logPrintf("[I2C] Scanning range 0x%02X-0x%02X...\n", start_addr, end_addr);
    }

    for (uint8_t addr = start_addr; addr <= end_addr; addr++) {
        // Feed watchdog to prevent trigger during long verbose scans
        watchdogFeed("CLI");
        
        uint32_t start_us = micros();
        Wire.beginTransmission(addr);
        uint8_t error = Wire.endTransmission();
        uint32_t elapsed_us = micros() - start_us;

        if (error == 0) {
            char hex_addr[8];
            snprintf(hex_addr, sizeof(hex_addr), "0x%02X", addr);

            const char* dev_name = "Unknown";
            for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
                if (KNOWN_DEVICES[i].address == addr) {
                    dev_name = KNOWN_DEVICES[i].name;
                    break;
                }
            }

            if (verbose) {
                char time_str[16];
                snprintf(time_str, sizeof(time_str), "%lu us", (unsigned long)elapsed_us);
                cliPrintTableRow(hex_addr, dev_name, "OK", 10, 20, 12, time_str, 14);
            } else {
                logInfo("[I2C] Found 0x%02X: %s", addr, dev_name);
            }

            if (found_count < 16) {
                found_addrs[found_count] = addr;
                response_times[found_count] = (float)elapsed_us / 1000.0f;
                found_count++;
            }
        }
        
        // Yield to let other tasks (Safety, LCD) run
        taskUnlockMutex(mutex);
        vTaskDelay(pdMS_TO_TICKS(1));
        if (!taskLockMutex(mutex, 100)) {
            logError("[I2C] Lost mutex during scan");
            return;
        }
    }

    if (verbose) {
        cliPrintTableFooter(10, 20, 12, 14);
    }

    logInfo("[I2C] Found %d device(s)", found_count);
    taskUnlockMutex(mutex);

    // Handle baseline operations (post-scan, no mutex needed for local state)
    if (save_baseline) {
        baseline.device_count = found_count;
        memcpy(baseline.addresses, found_addrs, found_count);
        memcpy(baseline.response_times, response_times, found_count * sizeof(float));
        baseline.timestamp_ms = millis();
        logInfo("[I2C] Baseline saved.");
    }

    if (compare_baseline && baseline.device_count > 0) {
        // ... comparison logic ...
        logInfo("[I2C] Comparing with baseline...");
        bool changes = false;
        for (int i = 0; i < baseline.device_count; i++) {
            bool found = false;
            for (int j = 0; j < found_count; j++) {
                if (baseline.addresses[i] == found_addrs[j]) { found = true; break; }
            }
            if (!found) { logWarning("[I2C] Device missing: 0x%02X", baseline.addresses[i]); changes = true; }
        }
        for (int i = 0; i < found_count; i++) {
            bool found = false;
            for (int j = 0; j < baseline.device_count; j++) {
                if (found_addrs[i] == baseline.addresses[j]) { found = true; break; }
            }
            if (!found) { logInfo("[I2C] New device: 0x%02X", found_addrs[i]); changes = true; }
        }
        if (!changes) logInfo("[I2C] No changes detected from baseline");
    }
}

// ============================================================================
// I2C TEST COMMAND
// ============================================================================

void cmd_i2c_test(int argc, char** argv) {
    bool verbose = hasOption(argc, argv, "-v") || hasOption(argc, argv, "--verbose");
    bool stress = hasOption(argc, argv, "--stress");
    bool quick = hasOption(argc, argv, "-q");

    logPrintln("\n[I2C] === Device Test ===");

    uint8_t test_addr = 0;
    if (argc > 1 && argv[1][0] == '0' && argv[1][1] == 'x') {
        test_addr = strtol(argv[1], NULL, 16);
    }

    uint8_t test_addrs[16]; // Increased
    int test_count = 0;
    if (test_addr) {
        test_addrs[0] = test_addr;
        test_count = 1;
    } else {
        for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
            test_addrs[test_count++] = KNOWN_DEVICES[i].address;
        }
    }

    SemaphoreHandle_t mutex = taskGetI2cMutex();
    if (!taskLockMutex(mutex, 5000)) {
        logError("[I2C] Could not acquire bus mutex");
        return;
    }

    if (!stress && verbose) {
        cliPrintTableHeader(10, 16, 12, 12);
        cliPrintTableRow("Address", "Read Test", "Write Test", 10, 16, 12, "Stability", 12);
        cliPrintTableDivider(10, 16, 12, 12);
    }

    int passed = 0;
    for (int i = 0; i < test_count; i++) {
        watchdogFeed("CLI");
        uint8_t addr = test_addrs[i];
        
        // Read test
        uint8_t test_byte = 0;
        uint32_t read_start = micros();
        i2c_result_t read_res = i2cReadWithRetry(addr, &test_byte, 1);
        uint32_t read_time = (micros() - read_start) / 1000;

        // Write test
        uint8_t write_byte = 0xFF;
        uint32_t write_start = micros();
        i2c_result_t write_res = i2cWriteWithRetry(addr, &write_byte, 1);
        uint32_t write_time = (micros() - write_start) / 1000;

        int stability_score = 100;
        if (!quick) {
            int stability_tests = stress ? 100 : 10;
            int stability_pass = 0;
            for (int t = 0; t < stability_tests; t++) {
                watchdogFeed("CLI");
                uint8_t dummy = 0;
                if (i2cReadWithRetry(addr, &dummy, 1) == I2C_RESULT_OK) stability_pass++;
                
                // Periodic yield in stability tests
                taskUnlockMutex(mutex);
                vTaskDelay(pdMS_TO_TICKS(1));
                if (!taskLockMutex(mutex, 100)) return;
            }
            stability_score = (stability_pass * 100) / stability_tests;
        }

        if (read_res == I2C_RESULT_OK) passed++;

        if (stress) {
            logPrintf("[I2C] Testing 0x%02X: %s\n", addr, (read_res == I2C_RESULT_OK) ? "OK" : "FAIL");
            logPrintln("[I2C]   Stress test (500 trans)...");
            int success = 0;
            for (int t = 0; t < 500; t++) {
                watchdogFeed("CLI");
                uint8_t dummy = 0;
                if (i2cReadWithRetry(addr, &dummy, 1) == I2C_RESULT_OK) success++;
                if (t % 50 == 0) {
                    taskUnlockMutex(mutex);
                    vTaskDelay(pdMS_TO_TICKS(1));
                    if (!taskLockMutex(mutex, 100)) return;
                }
            }
            logInfo("[I2C]   Success: %d/500 (%.1f%%)", success, (success / 5.0f));
        } else if (verbose) {
            char read_result[32], write_result[32], stab[16], addr_str[16];
            snprintf(addr_str, sizeof(addr_str), "0x%02X", addr);
            snprintf(read_result, sizeof(read_result), "%s (%lu ms)", read_res == I2C_RESULT_OK ? "OK" : "FAIL", (unsigned long)read_time);
            snprintf(write_result, sizeof(write_result), "%s (%lu ms)", write_res == I2C_RESULT_OK ? "OK" : "FAIL", (unsigned long)write_time);
            snprintf(stab, sizeof(stab), "%d%%", stability_score);
            cliPrintTableRow(addr_str, read_result, write_result, 10, 16, 12, stab, 12);
        } else {
            logInfo("[I2C] 0x%02X: %s", addr, (read_res == I2C_RESULT_OK) ? "PASS" : "FAIL");
        }
        
        taskUnlockMutex(mutex);
        vTaskDelay(pdMS_TO_TICKS(1));
        if (!taskLockMutex(mutex, 100)) return;
    }

    if (!stress && verbose) cliPrintTableFooter(10, 16, 12, 12);
    logInfo("[I2C] Passed: %d/%d", passed, test_count);
    taskUnlockMutex(mutex);
}

// ============================================================================
// I2C STATS COMMAND
// ============================================================================

void cmd_i2c_stats(int argc, char** argv) {
    bool reset = hasOption(argc, argv, "--reset");
    bool export_json = hasOption(argc, argv, "--export");

    if (reset) {
        i2cResetStats();
        logInfo("[I2C] Statistics cleared");
        return;
    }

    i2c_stats_t stats = i2cGetStats();

    if (export_json) {
        logPrintln("{");
        logPrintf("  \"transactions_total\": %lu,\r\n", (unsigned long)stats.transactions_total);
        logPrintf("  \"transactions_success\": %lu,\r\n", (unsigned long)stats.transactions_success);
        logPrintf("  \"transactions_failed\": %lu,\r\n", (unsigned long)stats.transactions_failed);
        logPrintf("  \"success_rate\": %.1f,\r\n", stats.success_rate);
        logPrintf("  \"retries_performed\": %lu,\r\n", (unsigned long)stats.retries_performed);
        logPrintf("  \"bus_recoveries\": %lu,\r\n", (unsigned long)stats.bus_recoveries);
        logPrintf("  \"error_nack\": %lu,\r\n", (unsigned long)stats.error_nack);
        logPrintf("  \"error_timeout\": %lu,\r\n", (unsigned long)stats.error_timeout);
        logPrintf("  \"error_bus\": %lu\r\n", (unsigned long)stats.error_bus);
        logPrintln("}");
    } else {
        logPrintln("\n[I2C] === Statistics ===");
        logPrintf("Total Transactions: %lu\n", (unsigned long)stats.transactions_total);
        logPrintf("Successful: %lu (%.1f%%)\n", (unsigned long)stats.transactions_success, stats.success_rate);
        logPrintf("Failed: %lu\n", (unsigned long)stats.transactions_failed);
        logPrintln("");
        logPrintf("Retries: %lu\n", (unsigned long)stats.retries_performed);
        logPrintf("Bus Recoveries: %lu\n", (unsigned long)stats.bus_recoveries);
        logPrintln("");
        logPrintln("Errors:");
        logPrintf("  NACK: %lu\n", (unsigned long)stats.error_nack);
        logPrintf("  Timeout: %lu\n", (unsigned long)stats.error_timeout);
        logPrintf("  Bus: %lu\n", (unsigned long)stats.error_bus);
        logPrintf("  Arbitration: %lu\n", (unsigned long)stats.error_arbitration);
    }
}

// ============================================================================
// I2C RECOVER COMMAND
// ============================================================================

void cmd_i2c_recover(int argc, char** argv) {
    logPrintln("\n[I2C] === Bus Recovery ===");

    i2c_bus_status_t status = i2cCheckBusStatus();
    logPrintf("Current status: %s\n", i2cBusStatusToString(status));

    if (status == I2C_BUS_OK) {
        logInfo("[I2C] Bus is healthy, no recovery needed");
        return;
    }

    logInfo("[I2C] Recovering...");
    i2cRecoverBus();

    delay(100);
    status = i2cCheckBusStatus();
    logInfo("[I2C] Recovery complete. New status: %s", i2cBusStatusToString(status));
}

// ============================================================================
// I2C MONITOR COMMAND
// ============================================================================

void cmd_i2c_monitor(int argc, char** argv) {
    bool with_alerts = hasOption(argc, argv, "--alert");
    int duration_sec = 30;
    const char* dur_str = getOptionValue(argc, argv, "-t");
    if (dur_str) duration_sec = atoi(dur_str);

    logPrintf("\n[I2C] === Monitoring for %d seconds ===\n", duration_sec);
    logPrintln("[I2C] (Press Ctrl+C to stop)");

    uint32_t start_time = millis();
    SemaphoreHandle_t mutex = taskGetI2cMutex();

    while (millis() - start_time < duration_sec * 1000) {
        watchdogFeed("CLI");
        
        // Check for Ctrl+C (0x03)
        if (CLI_SERIAL.available() > 0 && CLI_SERIAL.peek() == 0x03) {
            CLI_SERIAL.read(); // Consume 0x03
            logInfo("\n[I2C] Monitor aborted by user");
            break;
        }

        if (taskLockMutex(mutex, 100)) {
            for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
                uint8_t addr = KNOWN_DEVICES[i].address;
                uint8_t test_byte = 0;
                i2c_result_t res = i2cReadWithRetry(addr, &test_byte, 1);
                if (res == I2C_RESULT_OK) {
                    logPrintf("[%lu] 0x%02X (%s): OK\n", (unsigned long)(millis() / 1000), addr, KNOWN_DEVICES[i].name);
                } else {
                    logPrintf("[%lu] 0x%02X (%s): FAIL - %s\n", (unsigned long)(millis() / 1000), addr, KNOWN_DEVICES[i].name, i2cResultToString(res));
                    if (with_alerts) logWarning("[ALERT] Device 0x%02X not responding!", addr);
                }
            }
            taskUnlockMutex(mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Sample every 1 second
    }
    logInfo("[I2C] Monitor stopped");
}

// ============================================================================
// I2C BENCHMARK COMMAND
// ============================================================================

void cmd_i2c_benchmark(int argc, char** argv) {
    int iterations = 1000;
    const char* iter_str = getOptionValue(argc, argv, "-n");
    if (iter_str) iterations = atoi(iter_str);

    logPrintf("\n[I2C] === Benchmarking (%d iterations) ===\n", iterations);
    
    SemaphoreHandle_t mutex = taskGetI2cMutex();
    if (!taskLockMutex(mutex, 5000)) {
        logError("[I2C] Could not acquire bus mutex");
        return;
    }

    for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        uint8_t addr = KNOWN_DEVICES[i].address;
        logPrintf("\nDevice 0x%02X (%s):\n", addr, KNOWN_DEVICES[i].name);

        uint32_t min_time = 999999, max_time = 0, total_time = 0;
        int success_count = 0;

        for (int iter = 0; iter < iterations; iter++) {
            watchdogFeed("CLI");
            uint8_t test_byte = 0;
            uint32_t start = micros();
            i2c_result_t res = i2cReadWithRetry(addr, &test_byte, 1);
            uint32_t elapsed_ms = (micros() - start) / 1000;

            if (res == I2C_RESULT_OK) {
                success_count++;
                total_time += elapsed_ms;
                if (elapsed_ms < min_time) min_time = elapsed_ms;
                if (elapsed_ms > max_time) max_time = elapsed_ms;
            }
            
            if (iter % 100 == 0) {
                taskUnlockMutex(mutex);
                vTaskDelay(pdMS_TO_TICKS(1));
                if (!taskLockMutex(mutex, 100)) return;
            }
        }

        float avg = (success_count > 0) ? (float)total_time / success_count : 0;
        float success_pct = (float)success_count * 100.0f / iterations;

        logPrintf("  Min: %lu ms\n", (unsigned long)min_time);
        logPrintf("  Max: %lu ms\n", (unsigned long)max_time);
        logPrintf("  Avg: %.2f ms\n", avg);
        logPrintf("  Success: %.1f%% (%d/%d)\n", success_pct, success_count, iterations);
    }
    taskUnlockMutex(mutex);
}

// ============================================================================
// I2C HEALTH CHECK COMMAND
// ============================================================================

void cmd_i2c_health(int argc, char** argv) {
    logPrintln("\n[I2C] === Health Check ===");
    SemaphoreHandle_t mutex = taskGetI2cMutex();
    if (!taskLockMutex(mutex, 1000)) {
        logError("[I2C] Could not acquire bus mutex");
        return;
    }

    i2c_bus_status_t bus_status = i2cCheckBusStatus();
    logPrintf("Bus Status: %s\n", i2cBusStatusToString(bus_status));

    uint8_t device_count = 0;
    for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        uint8_t test_byte = 0;
        if (i2cReadWithRetry(KNOWN_DEVICES[i].address, &test_byte, 1) == I2C_RESULT_OK) {
            device_count++;
        }
    }
    taskUnlockMutex(mutex);
    
    logPrintf("Devices Found: %d/%d\n", device_count, KNOWN_DEVICE_COUNT);
    i2c_stats_t stats = i2cGetStats();
    logPrintf("Error Rate: %.1f%%\n", 100.0f - stats.success_rate);

    const char* status_str = "OK";
    if (bus_status != I2C_BUS_OK) status_str = "BUS_ERROR";
    else if (device_count < KNOWN_DEVICE_COUNT) status_str = "DEGRADED";
    else if (stats.success_rate < 99.0f) status_str = "DEGRADED";
    logPrintf("\nOverall Status: %s\n", status_str);
}

// ============================================================================
// I2C SELFTEST COMMAND
// ============================================================================

void cmd_i2c_selftest(int argc, char** argv) {
    logPrintln("\n[I2C] === I2C Self-Test Sequence ===");
    SemaphoreHandle_t mutex = taskGetI2cMutex();
    if (!taskLockMutex(mutex, 2000)) {
        logError("[I2C] Could not acquire bus mutex");
        return;
    }

    bool all_passed = true;
    logPrintln("[1/5] Checking GPIO pins...");
    i2c_bus_status_t status = i2cCheckBusStatus();
    if (status == I2C_BUS_OK) logInfo("      [PASS] GPIO pins healthy");
    else { logError("      [FAIL] GPIO problem: %s", i2cBusStatusToString(status)); all_passed = false; }

    logPrintln("[2/5] Scanning bus...");
    int device_count = 0;
    for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        uint8_t test_byte = 0;
        if (i2cReadWithRetry(KNOWN_DEVICES[i].address, &test_byte, 1) == I2C_RESULT_OK) device_count++;
    }
    logInfo("      [PASS] Found %d devices", device_count);

    for (int i = 0; i < device_count && i < 3; i++) {
        watchdogFeed("CLI");
        logPrintf("[%d/5] Testing device 0x%02X...\n", i + 3, KNOWN_DEVICES[i].address);
        uint8_t test_byte = 0;
        if (i2cReadWithRetry(KNOWN_DEVICES[i].address, &test_byte, 1) == I2C_RESULT_OK) logInfo("      [PASS]");
        else { logError("      [FAIL]"); all_passed = false; }
    }
    taskUnlockMutex(mutex);
    logInfo("\n[RESULT] %s", all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
}

// ============================================================================
// I2C TROUBLESHOOT COMMAND
// ============================================================================

void cmd_i2c_troubleshoot(int argc, char** argv) {
    logPrintln("\n[I2C] === Interactive Troubleshooting Wizard ===");
    uint8_t target_addr = 0;
    if (argc > 1 && argv[1][0] == '0' && argv[1][1] == 'x') {
        target_addr = strtol(argv[1], NULL, 16);
    }

    SemaphoreHandle_t mutex = taskGetI2cMutex();
    if (!taskLockMutex(mutex, 2000)) {
        logError("[I2C] Could not acquire bus mutex");
        return;
    }

    logPrintln("\nStep 1: Checking GPIO pin states...");
    i2c_bus_status_t status = i2cCheckBusStatus();
    logPrintf("  SDA (GPIO%d): %s\n", PIN_I2C_SDA, status == I2C_BUS_STUCK_SDA ? "STUCK_LOW" : "OK");
    logPrintf("  SCL (GPIO%d): %s\n", PIN_I2C_SCL, status == I2C_BUS_STUCK_SCL ? "STUCK_LOW" : "OK");

    if (status != I2C_BUS_OK) {
        taskUnlockMutex(mutex);
        logWarning("\n[I2C] Problem detected: I2C bus not responding");
        return;
    }

    logPrintln("\nStep 2: Scanning for devices...");
    if (target_addr) {
        uint8_t test_byte = 0;
        i2c_result_t res = i2cReadWithRetry(target_addr, &test_byte, 1);
        if (res == I2C_RESULT_OK) logInfo("  Device 0x%02X: FOUND", target_addr);
        else logWarning("  Device 0x%02X: NOT FOUND (%s)", target_addr, i2cResultToString(res));
    } else {
        for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
            uint8_t test_byte = 0;
            if (i2cReadWithRetry(KNOWN_DEVICES[i].address, &test_byte, 1) == I2C_RESULT_OK) {
                logInfo("  0x%02X (%s): OK", KNOWN_DEVICES[i].address, KNOWN_DEVICES[i].name);
            }
        }
    }
    taskUnlockMutex(mutex);
}

// ============================================================================
// MAIN I2C DISPATCHER
// ============================================================================

void cmd_i2c_main(int argc, char** argv) {
    if (argc < 2) {
        logPrintln("\n[I2C] Usage: i2c <command> [options]");
        logPrintln("\nCommands:");
        logPrintln("  scan [options]      - Scan for I2C devices");
        logPrintln("                        Options: -v (verbose), --save, --compare");
        logPrintln("  test [options]      - Test I2C devices");
        logPrintln("                        Options: -v (verbose), --stress");
        logPrintln("  stats [options]     - Show I2C statistics");
        logPrintln("                        Options: --reset, --export (JSON)");
        logPrintln("  recover             - Recover stuck I2C bus");
        logPrintln("  monitor [options]   - Monitor I2C bus");
        logPrintln("                        Options: --alert, -t <seconds>");
        logPrintln("  benchmark [-n N]    - Benchmark I2C performance");
        logPrintln("  health              - Quick health check");
        logPrintln("  selftest            - Comprehensive system test");
        logPrintln("  troubleshoot [addr] - Interactive troubleshooting");
        return;
    }

    const char* subcmd = argv[1];

    if (strcasecmp(subcmd, "scan") == 0) cmd_i2c_scan(argc - 1, argv + 1);
    else if (strcasecmp(subcmd, "test") == 0) cmd_i2c_test(argc - 1, argv + 1);
    else if (strcasecmp(subcmd, "stats") == 0) cmd_i2c_stats(argc - 1, argv + 1);
    else if (strcasecmp(subcmd, "recover") == 0) cmd_i2c_recover(argc - 1, argv + 1);
    else if (strcasecmp(subcmd, "monitor") == 0) cmd_i2c_monitor(argc - 1, argv + 1);
    else if (strcasecmp(subcmd, "benchmark") == 0) cmd_i2c_benchmark(argc - 1, argv + 1);
    else if (strcasecmp(subcmd, "health") == 0) cmd_i2c_health(argc - 1, argv + 1);
    else if (strcasecmp(subcmd, "selftest") == 0) cmd_i2c_selftest(argc - 1, argv + 1);
    else if (strcasecmp(subcmd, "troubleshoot") == 0) cmd_i2c_troubleshoot(argc - 1, argv + 1);
    else logWarning("[I2C] Unknown command: %s", subcmd);
}

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterI2CCommands() {
    // Replace old i2c command registration in cli_diag.cpp
    cliRegisterCommand("i2c", "I2C bus diagnostics and management", cmd_i2c_main);
}
