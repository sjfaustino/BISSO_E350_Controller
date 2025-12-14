/**
 * @file test/helpers/test_utils.h
 * @brief Test utility functions and helpers for BISSO E350 unit tests
 *
 * Provides common assertion helpers, logging utilities, and test fixtures
 * for use across all test suites.
 */

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>

/**
 * @brief Test fixture for motion control tests
 * Represents the state of a motion command during testing
 */
typedef struct {
    uint8_t axis;           // X=0, Y=1, Z=2
    int32_t distance_steps; // Target distance in encoder steps
    uint16_t speed_hz;      // VFD speed in Hz
    uint32_t duration_ms;   // Expected move duration
    float quality_score;    // Motion quality (0-100%)
    uint8_t status;         // Motion status (idle, moving, complete, error)
} motion_test_fixture_t;

/**
 * @brief Test fixture for encoder validation tests
 * Represents encoder state during testing
 */
typedef struct {
    uint16_t ppm;           // Pulses per millimeter (calibration value)
    int32_t position;       // Current position in encoder pulses
    float velocity_mms;     // Velocity in mm/s
    float jitter_amplitude; // Jitter amplitude in mm/s
    uint8_t status;         // Encoder status (idle, active, error)
} encoder_test_fixture_t;

/**
 * @brief Test fixture for safety system tests
 * Represents safety state during testing
 */
typedef struct {
    uint8_t e_stop_state;   // 0=inactive, 1=active
    uint8_t fault_flags;    // Fault condition flags
    uint8_t system_state;   // System state machine
    uint32_t recovery_time; // Time since fault onset
} safety_test_fixture_t;

/**
 * @brief Test fixture for configuration tests
 * Represents configuration state during testing
 */
typedef struct {
    uint16_t soft_limit_low_mm;
    uint16_t soft_limit_high_mm;
    uint16_t max_speed_hz;
    uint16_t min_speed_hz;
    uint8_t axis_count;
    uint32_t checksum;
} config_test_fixture_t;

/**
 * @brief Common assertion helper for floating-point comparisons with tolerance
 *
 * @param expected Expected value
 * @param actual Actual value
 * @param tolerance Allowable difference
 * @param line_num Line number for error reporting
 */
static inline void TEST_ASSERT_FLOAT_WITHIN_MESSAGE(
    float tolerance,
    float expected,
    float actual,
    const char* message)
{
    if (std::isnan(actual) || std::isnan(expected)) {
        UNITY_TEST_FAIL(__LINE__, "NaN value in float comparison");
        return;
    }

    float diff = std::fabs(expected - actual);
    if (diff > tolerance) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                "Expected %f within %f of %f (diff: %f) - %s",
                actual, tolerance, expected, diff, message);
        UNITY_TEST_FAIL(__LINE__, buffer);
    }
}

/**
 * @brief Assert that two memory blocks are equal
 *
 * @param expected Expected memory block
 * @param actual Actual memory block
 * @param size Number of bytes to compare
 * @param message Custom message on failure
 */
static inline void TEST_ASSERT_MEMORY_EQUAL_MESSAGE(
    const void* expected,
    const void* actual,
    size_t size,
    const char* message)
{
    if (std::memcmp(expected, actual, size) != 0) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                "Memory blocks not equal (size: %zu bytes) - %s",
                size, message);
        UNITY_TEST_FAIL(__LINE__, buffer);
    }
}

/**
 * @brief Assert that a value is within a range (inclusive)
 *
 * @param value The value to check
 * @param min Minimum acceptable value
 * @param max Maximum acceptable value
 * @param message Custom message on failure
 */
static inline void TEST_ASSERT_IN_RANGE_MESSAGE(
    int32_t value,
    int32_t min,
    int32_t max,
    const char* message)
{
    if (value < min || value > max) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                "Value %ld not in range [%ld, %ld] - %s",
                value, min, max, message);
        UNITY_TEST_FAIL(__LINE__, buffer);
    }
}

/**
 * @brief Log message during test execution (for debugging)
 *
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
#define TEST_LOG(format, ...) \
    do { \
        char log_buf[256]; \
        snprintf(log_buf, sizeof(log_buf), format, ##__VA_ARGS__); \
        UnityPrint("[TEST] "); \
        UnityPrint(log_buf); \
        UnityPrintChar('\n'); \
    } while(0)

/**
 * @brief Assert that a bitfield has specific flags set
 *
 * @param flags The bitfield value
 * @param mask The bits to check
 * @param expected_value The expected bits (0 or mask)
 */
#define TEST_ASSERT_FLAGS_SET(flags, mask, expected) \
    TEST_ASSERT_EQUAL_UINT32((flags) & (mask), (expected))

/**
 * @brief Assert that a bitfield does NOT have specific flags set
 *
 * @param flags The bitfield value
 * @param mask The bits to check
 */
#define TEST_ASSERT_FLAGS_CLEAR(flags, mask) \
    TEST_ASSERT_EQUAL_UINT32((flags) & (mask), 0)

/**
 * @brief Simulate time passing in tests
 * Used by tests that need to check timeout behavior, delays, etc.
 *
 * @param milliseconds Time to advance
 */
extern void test_advance_time(uint32_t milliseconds);

/**
 * @brief Get simulated time for tests
 *
 * @return Current simulated time in milliseconds
 */
extern uint32_t test_get_time(void);

/**
 * @brief Reset simulated time to zero
 */
extern void test_reset_time(void);

/**
 * @brief Initialize motion test fixture with default values
 *
 * @param fixture Pointer to fixture structure
 * @param axis Axis to test (0=X, 1=Y, 2=Z)
 */
extern void test_init_motion_fixture(motion_test_fixture_t* fixture, uint8_t axis);

/**
 * @brief Initialize encoder test fixture with default values
 *
 * @param fixture Pointer to fixture structure
 */
extern void test_init_encoder_fixture(encoder_test_fixture_t* fixture);

/**
 * @brief Initialize safety test fixture with default values
 *
 * @param fixture Pointer to fixture structure
 */
extern void test_init_safety_fixture(safety_test_fixture_t* fixture);

/**
 * @brief Initialize configuration test fixture with default values
 *
 * @param fixture Pointer to fixture structure
 */
extern void test_init_config_fixture(config_test_fixture_t* fixture);

/**
 * @brief Print detailed assertion failure information
 *
 * @param assertion The assertion that failed
 * @param expected Expected value description
 * @param actual Actual value description
 */
extern void test_print_failure(const char* assertion, const char* expected, const char* actual);

#endif // TEST_UTILS_H
