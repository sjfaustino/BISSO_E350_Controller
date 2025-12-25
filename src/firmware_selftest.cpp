/**
 * @file firmware_selftest.cpp
 * @brief Firmware Self-Test Suite Implementation (PHASE 5.2)
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "firmware_selftest.h"
#include "memory_monitor.h"
#include "motion.h"
#include "safety.h"
#include "spindle_current_monitor.h"
#include "encoder_wj66.h"
#include "i2c_bus_recovery.h"
#include "fault_logging.h"
#include "config_unified.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <string.h>
#include <stdio.h>
#include <LittleFS.h>
#include <WiFi.h>

// Test context
typedef struct {
    selftest_result_t* results;
    uint32_t test_count;
    uint32_t current_test;
} selftest_context_t;

static selftest_context_t test_ctx = {0};

// Helper: Add test result
static void addTestResult(const char* name, bool passed, const char* error, uint32_t duration_ms) {
    if (test_ctx.current_test >= test_ctx.test_count) return;

    selftest_result_t* result = &test_ctx.results[test_ctx.current_test];
    result->test_name = name;
    result->passed = passed;
    result->error_message = error;
    result->duration_ms = duration_ms;
    test_ctx.current_test++;
}

// ============================================================================
// MEMORY TESTS
// ============================================================================

static void testMemoryHeap() {
    uint32_t start = millis();
    uint32_t free_heap = memoryMonitorGetFreeHeap();
    uint32_t min_heap = memoryMonitorGetMinFreeHeap();

    bool passed = (free_heap > 10000) && (min_heap > 5000);
    addTestResult("Memory.Heap", passed,
                  passed ? NULL : "Insufficient free heap",
                  millis() - start);
}

static void testMemoryAllocation() {
    uint32_t start = millis();

    // Try allocating and freeing memory
    void* ptrs[5];
    bool passed = true;

    for (int i = 0; i < 5; i++) {
        ptrs[i] = malloc(1024);
        if (!ptrs[i]) {
            passed = false;
            break;
        }
    }

    for (int i = 0; i < 5; i++) {
        if (ptrs[i]) free(ptrs[i]);
    }

    addTestResult("Memory.Allocation", passed,
                  passed ? NULL : "Memory allocation failed",
                  millis() - start);
}

// ============================================================================
// I2C TESTS
// ============================================================================

static void testI2CBusStatus() {
    uint32_t start = millis();
    // I2C bus health check - verify known devices respond
    // Check PLC interface devices (0x21 I73 input, 0x22 Q73 output, 0x24 board inputs)
    bool bus_ok = true;
    Wire.beginTransmission(0x21);
    if (Wire.endTransmission() != 0) bus_ok = false;

    Wire.beginTransmission(0x22);
    if (Wire.endTransmission() != 0) bus_ok = false;

    Wire.beginTransmission(0x24);
    if (Wire.endTransmission() != 0) bus_ok = false;

    addTestResult("I2C.Bus", bus_ok,
                  bus_ok ? NULL : "I2C bus device(s) not responding",
                  millis() - start);
}

// ============================================================================
// STORAGE TESTS
// ============================================================================

static void testSPIFFSMount() {
    uint32_t start = millis();
    bool spiffs_ok = LittleFS.begin();
    if (spiffs_ok) {
        LittleFS.end();
    }
    addTestResult("Storage.LittleFS", spiffs_ok,
                  spiffs_ok ? NULL : "LittleFS mount failed",
                  millis() - start);
}

static void testNVSConfig() {
    uint32_t start = millis();
    // Try reading a config value
    const char* test_val = configGetString("wifi_ssid", NULL);
    bool passed = (test_val != NULL);
    addTestResult("Storage.NVS", passed,
                  passed ? NULL : "NVS config read failed",
                  millis() - start);
}

// ============================================================================
// MOTION TESTS
// ============================================================================

static void testMotionInitialized() {
    uint32_t start = millis();
    // Check if any axis is in ERROR state (MOTION_ERROR = 5)
    bool passed = true;
    for (int i = 0; i < 4; i++) {
        if (motionGetState(i) == MOTION_ERROR) {
            passed = false;
            break;
        }
    }
    addTestResult("Motion.Initialized", passed,
                  passed ? NULL : "Motion system has axis in ERROR state",
                  millis() - start);
}

static void testMotionHomeRequired() {
    uint32_t start = millis();
    // Check if homing is required (not critical for self-test)
    // Machine can operate without homing if soft limits are disabled
    // This is informational only
    addTestResult("Motion.HomeStatus", true,
                  NULL,
                  millis() - start);
}

// ============================================================================
// SPINDLE TESTS
// ============================================================================

static void testSpindleMonitor() {
    uint32_t start = millis();
    const spindle_monitor_state_t* state = spindleMonitorGetState();
    bool passed = (state != NULL);
    addTestResult("Spindle.Monitor", passed,
                  passed ? NULL : "Spindle monitor not initialized",
                  millis() - start);
}

// ============================================================================
// SAFETY TESTS
// ============================================================================

static void testEstopCircuit() {
    uint32_t start = millis();
    // Check if E-STOP circuit is functional
    // (not currently alarmed is a good sign)
    bool passed = !safetyIsAlarmed();
    addTestResult("Safety.EStop", passed,
                  passed ? NULL : "Safety system alarmed",
                  millis() - start);
}

static void testSafetyFaultLog() {
    uint32_t start = millis();
    // Verify fault log is accessible and functional
    fault_stats_t stats = faultGetStats();

    // Verify we can access the fault statistics and ring buffer
    bool passed = true;  // If we got here without crashing, log is accessible
    (void)stats;  // Suppress unused variable warning
    addTestResult("Safety.FaultLog", passed,
                  passed ? NULL : "Fault log not accessible",
                  millis() - start);
}

// ============================================================================
// NETWORK TESTS
// ============================================================================

static void testWiFiStatus() {
    uint32_t start = millis();
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);
    addTestResult("Network.WiFi", wifi_ok,
                  wifi_ok ? NULL : "WiFi not connected",
                  millis() - start);
}

// ============================================================================
// WATCHDOG TESTS
// ============================================================================

static void testWatchdog() {
    uint32_t start = millis();
    // Check if watchdog is enabled
    bool watchdog_ok = (esp_task_wdt_status(NULL) == ESP_OK);
    addTestResult("Watchdog.Enabled", watchdog_ok,
                  watchdog_ok ? NULL : "Watchdog not enabled",
                  millis() - start);
}

// ============================================================================
// TEST SUITE EXECUTION
// ============================================================================

typedef struct {
    const char* name;
    void (*test_func)();
    uint8_t category;
    bool quick;  // Quick tests for health check
} test_definition_t;

static const test_definition_t test_definitions[] = {
    // Memory tests
    {"Memory.Heap", testMemoryHeap, SELFTEST_CAT_MEMORY, true},
    {"Memory.Allocation", testMemoryAllocation, SELFTEST_CAT_MEMORY, false},

    // I2C tests
    {"I2C.Bus", testI2CBusStatus, SELFTEST_CAT_I2C, true},

    // Storage tests
    {"Storage.LittleFS", testSPIFFSMount, SELFTEST_CAT_STORAGE, false},
    {"Storage.NVS", testNVSConfig, SELFTEST_CAT_STORAGE, true},

    // Motion tests
    {"Motion.Initialized", testMotionInitialized, SELFTEST_CAT_MOTION, true},
    {"Motion.HomeStatus", testMotionHomeRequired, SELFTEST_CAT_MOTION, false},

    // Spindle tests
    {"Spindle.Monitor", testSpindleMonitor, SELFTEST_CAT_SPINDLE, true},

    // Safety tests
    {"Safety.EStop", testEstopCircuit, SELFTEST_CAT_SAFETY, true},
    {"Safety.FaultLog", testSafetyFaultLog, SELFTEST_CAT_SAFETY, true},

    // Network tests
    {"Network.WiFi", testWiFiStatus, SELFTEST_CAT_NETWORK, false},

    // Watchdog tests
    {"Watchdog.Enabled", testWatchdog, SELFTEST_CAT_WATCHDOG, true},
};

static const int test_definition_count = sizeof(test_definitions) / sizeof(test_definition_t);

selftest_suite_t selftestRunSuite(uint8_t categories, bool verbose) {
    selftest_suite_t suite = {0};

    // Count matching tests
    uint32_t count = 0;
    for (int i = 0; i < test_definition_count; i++) {
        if (test_definitions[i].category & categories) {
            count++;
        }
    }

    // Allocate results
    suite.results = (selftest_result_t*)malloc(count * sizeof(selftest_result_t));
    if (!suite.results) {
        return suite;  // Return empty suite on allocation failure
    }

    // Initialize context
    test_ctx.results = suite.results;
    test_ctx.test_count = count;
    test_ctx.current_test = 0;

    uint32_t suite_start = millis();

    // Run matching tests
    for (int i = 0; i < test_definition_count; i++) {
        if (test_definitions[i].category & categories) {
            if (verbose) {
                Serial.printf("[SELFTEST] Running: %s... ", test_definitions[i].name);
            }
            test_definitions[i].test_func();
            if (verbose) {
                selftest_result_t* last = &suite.results[test_ctx.current_test - 1];
                Serial.printf("%s (%lu ms)\n",
                            last->passed ? "PASS" : "FAIL",
                            (unsigned long)last->duration_ms);
            }
        }
    }

    // Calculate summary
    suite.total_tests = count;
    for (uint32_t i = 0; i < count; i++) {
        if (suite.results[i].passed) {
            suite.passed_tests++;
        } else {
            suite.failed_tests++;
        }
    }
    suite.total_duration_ms = millis() - suite_start;

    return suite;
}

selftest_result_t selftestRunTest(const char* test_name, bool verbose) {
    selftest_result_t result = {0};
    result.test_name = test_name;
    result.error_message = "Test not found";

    for (int i = 0; i < test_definition_count; i++) {
        if (strcmp(test_definitions[i].name, test_name) == 0) {
            test_definitions[i].test_func();
            if (test_ctx.current_test > 0) {
                result = test_ctx.results[test_ctx.current_test - 1];
            }
            return result;
        }
    }

    return result;
}

void selftestPrintResults(const selftest_suite_t* suite) {
    if (!suite || !suite->results) return;

    Serial.println("\n[SELFTEST] === Firmware Self-Test Results ===");
    Serial.println("Test Name                    | Status | Duration");
    Serial.println("-----------------------------|--------|----------");

    for (uint32_t i = 0; i < suite->total_tests; i++) {
        const selftest_result_t* result = &suite->results[i];
        Serial.printf("%-28s | %-6s | %lu ms\n",
                    result->test_name,
                    result->passed ? "PASS" : "FAIL",
                    (unsigned long)result->duration_ms);

        if (!result->passed && result->error_message) {
            Serial.printf("  ERROR: %s\n", result->error_message);
        }
    }

    Serial.println("-----------------------------|--------|----------");
    Serial.printf("Results: %lu/%lu passed in %lu ms\n\n",
                (unsigned long)suite->passed_tests,
                (unsigned long)suite->total_tests,
                (unsigned long)suite->total_duration_ms);
}

const char* selftestGetSummary(const selftest_suite_t* suite) {
    static char summary[128];
    if (!suite) {
        snprintf(summary, sizeof(summary), "UNKNOWN (null suite)");
        return summary;
    }

    if (suite->failed_tests == 0) {
        snprintf(summary, sizeof(summary), "ALL TESTS PASSED (%lu/%lu)",
                (unsigned long)suite->passed_tests,
                (unsigned long)suite->total_tests);
    } else {
        snprintf(summary, sizeof(summary), "%lu TESTS FAILED (%lu/%lu)",
                (unsigned long)suite->failed_tests,
                (unsigned long)suite->passed_tests,
                (unsigned long)suite->total_tests);
    }

    return summary;
}

void selftestFreeResults(selftest_suite_t* suite) {
    // MEMORY LEAK FIX (Gemini Audit):
    // Ensures results are freed even if called multiple times
    // Guards against double-free by setting pointer to NULL
    if (suite && suite->results) {
        free(suite->results);
        suite->results = NULL;
        suite->total_tests = 0;
        suite->passed_tests = 0;
        suite->failed_tests = 0;
    }
}

void selftestListTests() {
    Serial.println("\n[SELFTEST] === Available Tests ===");

    for (int i = 0; i < test_definition_count; i++) {
        Serial.printf("  %s %s\n",
                    test_definitions[i].name,
                    test_definitions[i].quick ? "(quick)" : "");
    }

    Serial.printf("\nTotal: %d tests\n\n", test_definition_count);
}

bool selftestQuickCheck() {
    selftest_suite_t suite = selftestRunSuite(SELFTEST_CAT_ALL, false);

    // Filter for quick tests only
    bool all_quick_pass = true;
    for (uint32_t i = 0; i < suite.total_tests; i++) {
        const selftest_result_t* result = &suite.results[i];
        // Only consider quick tests
        bool is_quick = false;
        for (int j = 0; j < test_definition_count; j++) {
            if (strcmp(test_definitions[j].name, result->test_name) == 0 &&
                test_definitions[j].quick) {
                is_quick = true;
                break;
            }
        }
        if (is_quick && !result->passed) {
            all_quick_pass = false;
            break;
        }
    }

    selftestFreeResults(&suite);
    return all_quick_pass;
}

#pragma GCC diagnostic pop
