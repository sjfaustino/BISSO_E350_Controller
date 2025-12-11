/**
 * @file cli_i2c.cpp
 * @brief Comprehensive I2C diagnostics and management CLI commands (Gemini v3.5.25)
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
} baseline = {0};

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

void printTableSeparator(int col1, int col2, int col3, int col4) {
    Serial.print("┌");
    for (int i = 0; i < col1; i++) Serial.print("─");
    Serial.print("┬");
    for (int i = 0; i < col2; i++) Serial.print("─");
    Serial.print("┬");
    for (int i = 0; i < col3; i++) Serial.print("─");
    Serial.print("┬");
    for (int i = 0; i < col4; i++) Serial.print("─");
    Serial.println("┐");
}

void printTableRow(const char* col1, const char* col2, const char* col3, const char* col4,
                   int col1_width, int col2_width, int col3_width, int col4_width) {
    Serial.print("│ ");
    Serial.print(col1);
    for (int i = strlen(col1); i < col1_width - 1; i++) Serial.print(" ");
    Serial.print("│ ");
    Serial.print(col2);
    for (int i = strlen(col2); i < col2_width - 1; i++) Serial.print(" ");
    Serial.print("│ ");
    Serial.print(col3);
    for (int i = strlen(col3); i < col3_width - 1; i++) Serial.print(" ");
    Serial.print("│ ");
    Serial.print(col4);
    for (int i = strlen(col4); i < col4_width - 1; i++) Serial.print(" ");
    Serial.println("│");
}

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

    Serial.println("\n[I2C] === Bus Scan ===");

    uint8_t start_addr = 0x08, end_addr = 0x77;
    if (addr_str) {
        start_addr = strtol(addr_str, NULL, 16);
        const char* end_str = getOptionValue(argc, argv, "-r");
        if (end_str) end_addr = strtol(end_str, NULL, 16);
    }

    uint8_t found_count = 0;
    uint8_t found_addrs[8] = {0};
    float response_times[8] = {0};

    if (verbose) {
        Serial.printf("[I2C] Scanning range 0x%02X-0x%02X with timing...\n", start_addr, end_addr);
        printTableSeparator(10, 20, 12, 14);
        printTableRow("Address", "Device Name", "Status", "Response", 10, 20, 12, 14);
        printTableSeparator(10, 20, 12, 14);
    } else {
        Serial.printf("[I2C] Scanning range 0x%02X-0x%02X...\n", start_addr, end_addr);
    }

    for (uint8_t addr = start_addr; addr <= end_addr; addr++) {
        uint8_t test_byte = 0;
        uint32_t start = micros();
        i2c_result_t res = i2cReadWithRetry(addr, &test_byte, 1);
        uint32_t elapsed_ms = (micros() - start) / 1000;

        if (res == I2C_RESULT_OK) {
            // Found a device
            char hex_addr[8];
            snprintf(hex_addr, sizeof(hex_addr), "0x%02X", addr);

            // Find device name
            const char* dev_name = "Unknown";
            for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
                if (KNOWN_DEVICES[i].address == addr) {
                    dev_name = KNOWN_DEVICES[i].name;
                    break;
                }
            }

            if (verbose) {
                char time_str[16];
                snprintf(time_str, sizeof(time_str), "%lu ms", (unsigned long)elapsed_ms);
                printTableRow(hex_addr, dev_name, "OK", time_str, 10, 20, 12, 14);
            } else {
                Serial.printf("[I2C] Found 0x%02X: %s\n", addr, dev_name);
            }

            found_addrs[found_count] = addr;
            response_times[found_count] = (float)elapsed_ms;
            found_count++;
        }
    }

    if (verbose) {
        printTableSeparator(10, 20, 12, 14);
    }

    Serial.printf("[I2C] Found %d device(s)\n", found_count);

    // Handle baseline operations
    if (save_baseline) {
        baseline.device_count = found_count;
        memcpy(baseline.addresses, found_addrs, found_count);
        memcpy(baseline.response_times, response_times, found_count * sizeof(float));
        baseline.timestamp_ms = millis();
        Serial.println("[I2C] Baseline saved. Use 'i2c scan --compare' to check for changes");
    }

    if (compare_baseline && baseline.device_count > 0) {
        Serial.println("[I2C] Comparing with baseline...");
        bool changes = false;

        // Check for missing devices
        for (int i = 0; i < baseline.device_count; i++) {
            bool found = false;
            for (int j = 0; j < found_count; j++) {
                if (baseline.addresses[i] == found_addrs[j]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                Serial.printf("[I2C] [WARN] Device missing: 0x%02X\n", baseline.addresses[i]);
                changes = true;
            }
        }

        // Check for new devices
        for (int i = 0; i < found_count; i++) {
            bool found = false;
            for (int j = 0; j < baseline.device_count; j++) {
                if (found_addrs[i] == baseline.addresses[j]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                Serial.printf("[I2C] [INFO] New device: 0x%02X\n", found_addrs[i]);
                changes = true;
            }
        }

        if (!changes) {
            Serial.println("[I2C] No changes detected from baseline");
        }
    }
}

// ============================================================================
// I2C TEST COMMAND
// ============================================================================

void cmd_i2c_test(int argc, char** argv) {
    bool verbose = hasOption(argc, argv, "-v") || hasOption(argc, argv, "--verbose");
    bool stress = hasOption(argc, argv, "--stress");
    bool quick = hasOption(argc, argv, "-q");

    Serial.println("\n[I2C] === Device Test ===");

    // Get specific address if provided
    uint8_t test_addr = 0;
    if (argc > 1 && argv[1][0] == '0' && argv[1][1] == 'x') {
        test_addr = strtol(argv[1], NULL, 16);
    }

    // Collect devices to test
    uint8_t test_addrs[8];
    int test_count = 0;

    if (test_addr) {
        test_addrs[0] = test_addr;
        test_count = 1;
    } else {
        // Test all known devices
        for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
            test_addrs[test_count++] = KNOWN_DEVICES[i].address;
        }
    }

    if (!stress) {
        if (verbose) {
            printTableSeparator(10, 16, 12, 12);
            printTableRow("Address", "Read Test", "Write Test", "Stability", 10, 16, 12, 12);
            printTableSeparator(10, 16, 12, 12);
        }
    }

    int passed = 0;
    for (int i = 0; i < test_count; i++) {
        uint8_t addr = test_addrs[i];
        uint8_t test_byte = 0;

        // Read test
        uint32_t read_start = micros();
        i2c_result_t read_res = i2cReadWithRetry(addr, &test_byte, 1);
        uint32_t read_time = (micros() - read_start) / 1000;

        // Write test
        uint8_t write_byte = 0xFF;
        uint32_t write_start = micros();
        i2c_result_t write_res = i2cWriteWithRetry(addr, &write_byte, 1);
        uint32_t write_time = (micros() - write_start) / 1000;

        // Stability test (if not quick mode)
        int stability_score = 0;
        if (!quick) {
            int stability_tests = stress ? 100 : 10;
            int stability_pass = 0;
            for (int t = 0; t < stability_tests; t++) {
                uint8_t dummy = 0;
                if (i2cReadWithRetry(addr, &dummy, 1) == I2C_RESULT_OK) {
                    stability_pass++;
                }
            }
            stability_score = (stability_pass * 100) / stability_tests;
        }

        bool test_passed = (read_res == I2C_RESULT_OK);
        if (test_passed) passed++;

        if (stress) {
            if (verbose || i == 0) {
                Serial.printf("[I2C] Testing 0x%02X: %s\n", addr,
                    (read_res == I2C_RESULT_OK) ? "OK" : "FAIL");
            }

            // Stress test - 1000 transactions
            Serial.printf("[I2C]   Stress test (1000 trans)...\n");
            int success = 0;
            uint32_t total_time = 0;
            uint32_t min_time = 999999, max_time = 0;

            for (int t = 0; t < 1000; t++) {
                uint8_t dummy = 0;
                uint32_t st = micros();
                if (i2cReadWithRetry(addr, &dummy, 1) == I2C_RESULT_OK) {
                    success++;
                }
                uint32_t et = (micros() - st) / 1000;
                total_time += et;
                if (et < min_time) min_time = et;
                if (et > max_time) max_time = et;
            }

            float avg = (float)total_time / 1000.0f;
            Serial.printf("[I2C]   Success: %d/1000 (%.1f%%)\n", success, (success / 10.0f));
            Serial.printf("[I2C]   Time: min=%lu, max=%lu, avg=%.1f ms\n",
                (unsigned long)min_time, (unsigned long)max_time, avg);
        } else if (verbose) {
            char addr_str[8];
            snprintf(addr_str, sizeof(addr_str), "0x%02X", addr);
            char read_result[12];
            snprintf(read_result, sizeof(read_result), "%s (%lu ms)",
                read_res == I2C_RESULT_OK ? "OK" : "FAIL", (unsigned long)read_time);
            char write_result[12];
            snprintf(write_result, sizeof(write_result), "%s (%lu ms)",
                write_res == I2C_RESULT_OK ? "OK" : "FAIL", (unsigned long)write_time);
            char stab[12];
            snprintf(stab, sizeof(stab), "%d%%", stability_score);
            printTableRow(addr_str, read_result, write_result, stab, 10, 16, 12, 12);
        } else {
            Serial.printf("[I2C] 0x%02X: %s\n", addr, test_passed ? "PASS" : "FAIL");
        }
    }

    if (!stress && verbose) {
        printTableSeparator(10, 16, 12, 12);
    }

    Serial.printf("[I2C] Passed: %d/%d\n", passed, test_count);
}

// ============================================================================
// I2C STATS COMMAND
// ============================================================================

void cmd_i2c_stats(int argc, char** argv) {
    bool reset = hasOption(argc, argv, "--reset");
    bool export_json = hasOption(argc, argv, "--export");

    if (reset) {
        i2cResetStats();
        Serial.println("[I2C] Statistics cleared");
        return;
    }

    i2c_stats_t stats = i2cGetStats();

    if (export_json) {
        Serial.println("{");
        Serial.printf("  \"transactions_total\": %lu,\n", (unsigned long)stats.transactions_total);
        Serial.printf("  \"transactions_success\": %lu,\n", (unsigned long)stats.transactions_success);
        Serial.printf("  \"transactions_failed\": %lu,\n", (unsigned long)stats.transactions_failed);
        Serial.printf("  \"success_rate\": %.1f,\n", stats.success_rate);
        Serial.printf("  \"retries_performed\": %lu,\n", (unsigned long)stats.retries_performed);
        Serial.printf("  \"bus_recoveries\": %lu,\n", (unsigned long)stats.bus_recoveries);
        Serial.printf("  \"error_nack\": %lu,\n", (unsigned long)stats.error_nack);
        Serial.printf("  \"error_timeout\": %lu,\n", (unsigned long)stats.error_timeout);
        Serial.printf("  \"error_bus\": %lu\n", (unsigned long)stats.error_bus);
        Serial.println("}");
    } else {
        Serial.println("\n[I2C] === Statistics ===");
        Serial.printf("Total Transactions: %lu\n", (unsigned long)stats.transactions_total);
        Serial.printf("Successful: %lu (%.1f%%)\n", (unsigned long)stats.transactions_success, stats.success_rate);
        Serial.printf("Failed: %lu\n", (unsigned long)stats.transactions_failed);
        Serial.println();
        Serial.printf("Retries: %lu\n", (unsigned long)stats.retries_performed);
        Serial.printf("Bus Recoveries: %lu\n", (unsigned long)stats.bus_recoveries);
        Serial.println();
        Serial.printf("Errors:\n");
        Serial.printf("  NACK: %lu\n", (unsigned long)stats.error_nack);
        Serial.printf("  Timeout: %lu\n", (unsigned long)stats.error_timeout);
        Serial.printf("  Bus: %lu\n", (unsigned long)stats.error_bus);
        Serial.printf("  Arbitration: %lu\n", (unsigned long)stats.error_arbitration);
    }
}

// ============================================================================
// I2C RECOVER COMMAND
// ============================================================================

void cmd_i2c_recover(int argc, char** argv) {
    Serial.println("\n[I2C] === Bus Recovery ===");

    i2c_bus_status_t status = i2cCheckBusStatus();
    Serial.printf("Current status: %s\n", i2cBusStatusToString(status));

    if (status == I2C_BUS_OK) {
        Serial.println("[I2C] Bus is healthy, no recovery needed");
        return;
    }

    Serial.println("[I2C] Recovering...");
    i2cRecoverBus();

    delay(100);
    status = i2cCheckBusStatus();
    Serial.printf("[I2C] Recovery complete. New status: %s\n", i2cBusStatusToString(status));
}

// ============================================================================
// I2C MONITOR COMMAND
// ============================================================================

void cmd_i2c_monitor(int argc, char** argv) {
    bool with_alerts = hasOption(argc, argv, "--alert");
    int duration_sec = 30; // Default 30 seconds

    const char* dur_str = getOptionValue(argc, argv, "-t");
    if (dur_str) {
        duration_sec = atoi(dur_str);
    }

    Serial.printf("\n[I2C] === Monitoring for %d seconds ===\n", duration_sec);
    Serial.println("[I2C] (Press Ctrl+C to stop)");

    uint32_t start_time = millis();
    uint32_t last_error_time = 0;

    while (millis() - start_time < duration_sec * 1000) {
        // Test each known device
        for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
            uint8_t addr = KNOWN_DEVICES[i].address;
            uint8_t test_byte = 0;

            uint32_t trans_start = micros();
            i2c_result_t res = i2cReadWithRetry(addr, &test_byte, 1);
            uint32_t trans_time = (micros() - trans_start) / 1000;

            if (res == I2C_RESULT_OK) {
                Serial.printf("[%lu] 0x%02X (%s): OK (%lu ms)\n",
                    (unsigned long)(millis() / 1000), addr, KNOWN_DEVICES[i].name, (unsigned long)trans_time);
            } else {
                Serial.printf("[%lu] 0x%02X (%s): FAIL - %s\n",
                    (unsigned long)(millis() / 1000), addr, KNOWN_DEVICES[i].name, i2cResultToString(res));

                if (with_alerts) {
                    Serial.printf("[ALERT] Device 0x%02X not responding!\n", addr);
                }
                last_error_time = millis();
            }
        }

        delay(1000); // Sample every 1 second
    }

    Serial.println("[I2C] Monitor stopped");
}

// ============================================================================
// I2C BENCHMARK COMMAND
// ============================================================================

void cmd_i2c_benchmark(int argc, char** argv) {
    int iterations = 1000;
    const char* iter_str = getOptionValue(argc, argv, "-n");
    if (iter_str) {
        iterations = atoi(iter_str);
    }

    Serial.printf("\n[I2C] === Benchmarking (%d iterations) ===\n", iterations);

    for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        uint8_t addr = KNOWN_DEVICES[i].address;

        Serial.printf("\nDevice 0x%02X (%s):\n", addr, KNOWN_DEVICES[i].name);

        uint32_t min_time = 999999, max_time = 0;
        uint32_t total_time = 0;
        int success_count = 0;

        for (int iter = 0; iter < iterations; iter++) {
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
        }

        float avg = (success_count > 0) ? (float)total_time / success_count : 0;
        float success_pct = (float)success_count * 100.0f / iterations;

        Serial.printf("  Min: %lu ms\n", (unsigned long)min_time);
        Serial.printf("  Max: %lu ms\n", (unsigned long)max_time);
        Serial.printf("  Avg: %.2f ms\n", avg);
        Serial.printf("  Success: %.1f%% (%d/%d)\n", success_pct, success_count, iterations);
    }
}

// ============================================================================
// I2C HEALTH CHECK COMMAND
// ============================================================================

void cmd_i2c_health(int argc, char** argv) {
    Serial.println("\n[I2C] === Health Check ===");

    // Bus status
    i2c_bus_status_t bus_status = i2cCheckBusStatus();
    Serial.printf("Bus Status: %s\n", i2cBusStatusToString(bus_status));

    // Device count
    uint8_t device_count = 0;
    for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        uint8_t test_byte = 0;
        if (i2cReadWithRetry(KNOWN_DEVICES[i].address, &test_byte, 1) == I2C_RESULT_OK) {
            device_count++;
        }
    }
    Serial.printf("Devices Found: %d/%d\n", device_count, KNOWN_DEVICE_COUNT);

    // Error rate
    i2c_stats_t stats = i2cGetStats();
    Serial.printf("Error Rate: %.1f%%\n", 100.0f - stats.success_rate);

    // Overall status
    const char* status_str = "OK";
    if (bus_status != I2C_BUS_OK) status_str = "BUS_ERROR";
    else if (device_count < KNOWN_DEVICE_COUNT) status_str = "DEGRADED";
    else if (stats.success_rate < 99.0f) status_str = "DEGRADED";

    Serial.printf("\nOverall Status: %s\n", status_str);
}

// ============================================================================
// I2C SELFTEST COMMAND
// ============================================================================

void cmd_i2c_selftest(int argc, char** argv) {
    Serial.println("\n[I2C] === I2C Self-Test Sequence ===");

    bool all_passed = true;

    // Test 1: GPIO Pins
    Serial.println("[1/5] Checking GPIO pins...");
    i2c_bus_status_t status = i2cCheckBusStatus();
    if (status == I2C_BUS_OK) {
        Serial.println("      [PASS] GPIO pins healthy");
    } else {
        Serial.printf("      [FAIL] GPIO problem: %s\n", i2cBusStatusToString(status));
        all_passed = false;
    }

    // Test 2: Bus Scan
    Serial.println("[2/5] Scanning bus...");
    int device_count = 0;
    for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        uint8_t test_byte = 0;
        if (i2cReadWithRetry(KNOWN_DEVICES[i].address, &test_byte, 1) == I2C_RESULT_OK) {
            device_count++;
        }
    }
    Serial.printf("      [PASS] Found %d devices\n", device_count);
    if (device_count < KNOWN_DEVICE_COUNT) {
        Serial.printf("      [WARN] Expected %d devices\n", KNOWN_DEVICE_COUNT);
    }

    // Test 3-5: Device Testing
    for (int i = 0; i < device_count && i < 3; i++) {
        Serial.printf("[%d/5] Testing device 0x%02X...\n", i + 3, KNOWN_DEVICES[i].address);
        uint8_t test_byte = 0;
        i2c_result_t res = i2cReadWithRetry(KNOWN_DEVICES[i].address, &test_byte, 1);
        if (res == I2C_RESULT_OK) {
            Serial.println("      [PASS]");
        } else {
            Serial.printf("      [FAIL] %s\n", i2cResultToString(res));
            all_passed = false;
        }
    }

    Serial.println("\n[RESULT] " + String(all_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED"));
}

// ============================================================================
// I2C TROUBLESHOOT COMMAND
// ============================================================================

void cmd_i2c_troubleshoot(int argc, char** argv) {
    Serial.println("\n[I2C] === Interactive Troubleshooting Wizard ===");

    uint8_t target_addr = 0;
    if (argc > 1 && argv[1][0] == '0' && argv[1][1] == 'x') {
        target_addr = strtol(argv[1], NULL, 16);
    }

    // Step 1: Check pin voltages
    Serial.println("\nStep 1: Checking GPIO pin states...");
    i2c_bus_status_t status = i2cCheckBusStatus();

    Serial.printf("  SDA (GPIO%d): %s\n", PIN_I2C_SDA,
        status == I2C_BUS_STUCK_SDA ? "STUCK_LOW (Problem!)" : "OK");
    Serial.printf("  SCL (GPIO%d): %s\n", PIN_I2C_SCL,
        status == I2C_BUS_STUCK_SCL ? "STUCK_LOW (Problem!)" : "OK");

    if (status != I2C_BUS_OK) {
        Serial.println("\n[I2C] Problem detected: I2C bus not responding");
        Serial.println("\nPossible causes:");
        if (status == I2C_BUS_STUCK_SDA) {
            Serial.println("  1. Device holding SDA line low");
            Serial.println("  2. Short circuit to ground (SDA)");
            Serial.println("  3. Faulty pull-up resistor");
        } else if (status == I2C_BUS_STUCK_SCL) {
            Serial.println("  1. Device holding SCL line low");
            Serial.println("  2. Short circuit to ground (SCL)");
            Serial.println("  3. Faulty pull-up resistor");
        }
        Serial.println("\nSuggested actions:");
        Serial.println("  1. Check all I2C cable connections");
        Serial.println("  2. Verify PCF8574 chips are properly seated");
        Serial.println("  3. Try: i2c recover");
        return;
    }

    // Step 2: Device detection
    Serial.println("\nStep 2: Scanning for devices...");

    if (target_addr) {
        // Test specific device
        uint8_t test_byte = 0;
        i2c_result_t res = i2cReadWithRetry(target_addr, &test_byte, 1);
        if (res == I2C_RESULT_OK) {
            Serial.printf("  Device 0x%02X: FOUND (responsive)\n", target_addr);
        } else {
            Serial.printf("  Device 0x%02X: NOT FOUND (%s)\n", target_addr, i2cResultToString(res));
            Serial.println("\nPossible causes:");
            Serial.println("  1. Device powered off");
            Serial.println("  2. Device not at expected address");
            Serial.println("  3. Faulty device or connection");
            Serial.println("  4. I2C pull-up resistors missing/weak");
        }
    } else {
        int found_count = 0;
        for (int i = 0; i < KNOWN_DEVICE_COUNT; i++) {
            uint8_t test_byte = 0;
            if (i2cReadWithRetry(KNOWN_DEVICES[i].address, &test_byte, 1) == I2C_RESULT_OK) {
                Serial.printf("  0x%02X (%s): OK\n", KNOWN_DEVICES[i].address, KNOWN_DEVICES[i].name);
                found_count++;
            }
        }
        if (found_count == 0) {
            Serial.println("  No devices found!");
        }
    }

    Serial.println("\nFor more details, run:");
    Serial.println("  i2c scan -v    (Detailed scan with timing)");
    Serial.println("  i2c test -v    (Test all devices)");
    Serial.println("  i2c stats      (Show error statistics)");
}

// ============================================================================
// MAIN I2C DISPATCHER
// ============================================================================

void cmd_i2c_main(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("\n[I2C] Usage: i2c <command> [options]");
        Serial.println("\nCommands:");
        Serial.println("  scan [options]      - Scan for I2C devices");
        Serial.println("                        Options: -v (verbose), --save, --compare");
        Serial.println("  test [options]      - Test I2C devices");
        Serial.println("                        Options: -v (verbose), --stress");
        Serial.println("  stats [options]     - Show I2C statistics");
        Serial.println("                        Options: --reset, --export (JSON)");
        Serial.println("  recover             - Recover stuck I2C bus");
        Serial.println("  monitor [options]   - Monitor I2C bus");
        Serial.println("                        Options: --alert, -t <seconds>");
        Serial.println("  benchmark [-n N]    - Benchmark I2C performance");
        Serial.println("  health              - Quick health check");
        Serial.println("  selftest            - Comprehensive system test");
        Serial.println("  troubleshoot [addr] - Interactive troubleshooting");
        return;
    }

    const char* subcmd = argv[1];

    if (strcmp(subcmd, "scan") == 0) cmd_i2c_scan(argc - 1, argv + 1);
    else if (strcmp(subcmd, "test") == 0) cmd_i2c_test(argc - 1, argv + 1);
    else if (strcmp(subcmd, "stats") == 0) cmd_i2c_stats(argc - 1, argv + 1);
    else if (strcmp(subcmd, "recover") == 0) cmd_i2c_recover(argc - 1, argv + 1);
    else if (strcmp(subcmd, "monitor") == 0) cmd_i2c_monitor(argc - 1, argv + 1);
    else if (strcmp(subcmd, "benchmark") == 0) cmd_i2c_benchmark(argc - 1, argv + 1);
    else if (strcmp(subcmd, "health") == 0) cmd_i2c_health(argc - 1, argv + 1);
    else if (strcmp(subcmd, "selftest") == 0) cmd_i2c_selftest(argc - 1, argv + 1);
    else if (strcmp(subcmd, "troubleshoot") == 0) cmd_i2c_troubleshoot(argc - 1, argv + 1);
    else Serial.printf("[I2C] Unknown command: %s\n", subcmd);
}

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterI2CCommands() {
    // Replace old i2c command registration in cli_diag.cpp
    cliRegisterCommand("i2c", "I2C bus diagnostics and management", cmd_i2c_main);
}
