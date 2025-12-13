/**
 * @file firmware_selftest.h
 * @brief Comprehensive Firmware Self-Test Suite (PHASE 5.2)
 * @details Automated hardware validation and diagnostics
 * @project BISSO E350 Controller
 */

#ifndef FIRMWARE_SELFTEST_H
#define FIRMWARE_SELFTEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Individual test result
 */
typedef struct {
    const char* test_name;
    bool passed;
    const char* error_message;
    uint32_t duration_ms;
} selftest_result_t;

/**
 * Overall test suite results
 */
typedef struct {
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t total_duration_ms;
    selftest_result_t* results;
} selftest_suite_t;

/**
 * Test categories
 */
typedef enum {
    SELFTEST_CAT_MEMORY = 0x01,
    SELFTEST_CAT_I2C = 0x02,
    SELFTEST_CAT_STORAGE = 0x04,
    SELFTEST_CAT_MOTION = 0x08,
    SELFTEST_CAT_SPINDLE = 0x10,
    SELFTEST_CAT_SAFETY = 0x20,
    SELFTEST_CAT_NETWORK = 0x40,
    SELFTEST_CAT_WATCHDOG = 0x80,
    SELFTEST_CAT_ALL = 0xFF
} selftest_category_t;

/**
 * Run firmware self-test suite
 * @param categories Bitmask of test categories to run
 * @param verbose If true, print detailed output
 * @return Test results (caller must free with selftestFreeResults)
 */
selftest_suite_t selftestRunSuite(uint8_t categories, bool verbose);

/**
 * Run specific test by name
 * @param test_name Name of test to run
 * @param verbose If true, print detailed output
 * @return Single test result
 */
selftest_result_t selftestRunTest(const char* test_name, bool verbose);

/**
 * Print test results to serial
 * @param suite Test results
 */
void selftestPrintResults(const selftest_suite_t* suite);

/**
 * Get summary of test results
 * @param suite Test results
 * @return Pointer to summary string (valid until next call)
 */
const char* selftestGetSummary(const selftest_suite_t* suite);

/**
 * Free test results memory
 * @param suite Test results to free
 */
void selftestFreeResults(selftest_suite_t* suite);

/**
 * List all available tests
 */
void selftestListTests();

/**
 * Quick health check (fast tests only)
 * @return true if all quick tests pass
 */
bool selftestQuickCheck();

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_SELFTEST_H
