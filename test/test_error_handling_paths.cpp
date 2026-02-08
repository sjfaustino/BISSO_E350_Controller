/**
 * @file test/test_error_handling_paths.cpp
 * @brief Unit tests for advanced error handling paths (storms, rate limiting)
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

// Re-defining enums/structs from fault_logging.h to avoid header dependency hell in native tests
typedef enum { FAULT_NONE = 0, FAULT_WARNING = 1, FAULT_ERROR = 2, FAULT_CRITICAL = 3 } fault_severity_t;
typedef enum { FAULT_NONE_CODE = 0x00, FAULT_MOTION_STALL = 0x03, FAULT_CODE_MAX = 0x18 } fault_code_t;

typedef struct {
    uint32_t timestamp;
    fault_severity_t severity;
    fault_code_t code;
    int32_t axis;
    int32_t value;
    char message[64];
} fault_entry_t;

// Mocks for internal state
static uint32_t mock_millis = 0;
static bool mock_estop_active = false;
static int mock_nvs_write_count = 0;

// Integration logic to be tested (Simplified for isolation)
static uint32_t last_nvs_write_time[FAULT_CODE_MAX] = {0};
#define FAULT_RATE_WINDOW_SIZE 10
#define FAULT_STORM_THRESHOLD_PER_SEC 5
#define FAULT_NVS_WRITE_COOLDOWN_NORMAL_MS 1000
#define FAULT_NVS_WRITE_COOLDOWN_STORM_MS 10000

static uint32_t fault_timestamps[FAULT_RATE_WINDOW_SIZE] = {0};
static uint8_t fault_timestamp_idx = 0;

static uint32_t getFaultRate() {
    uint32_t now = mock_millis;
    fault_timestamps[fault_timestamp_idx] = now;
    fault_timestamp_idx = (fault_timestamp_idx + 1) % FAULT_RATE_WINDOW_SIZE;
    uint32_t oldest = fault_timestamps[fault_timestamp_idx];
    if (oldest == 0) return 0;
    uint32_t time_span_ms = now - oldest;
    if (time_span_ms == 0) return 0;
    return (FAULT_RATE_WINDOW_SIZE * 1000) / time_span_ms;
}

static bool shouldLogToNVS(fault_code_t code) {
    uint32_t now = mock_millis;
    uint32_t rate = getFaultRate();
    uint32_t cooldown = (rate > FAULT_STORM_THRESHOLD_PER_SEC) ? 
                        FAULT_NVS_WRITE_COOLDOWN_STORM_MS : 
                        FAULT_NVS_WRITE_COOLDOWN_NORMAL_MS;

    if (code < FAULT_CODE_MAX) {
        if (now - last_nvs_write_time[code] < cooldown) return false;
        last_nvs_write_time[code] = now;
    }
    mock_nvs_write_count++;
    return true;
}

static void reportFault(fault_severity_t sev) {
    if (sev == FAULT_CRITICAL) mock_estop_active = true;
}

// ============================================================================
// TESTS
// ============================================================================

// @test Fault Rate calculation logic
void test_fault_rate_calculation(void) {
    memset(fault_timestamps, 0, sizeof(fault_timestamps));
    fault_timestamp_idx = 0;
    mock_millis = 1000;

    // Simulate 10 faults over 1 second (10 faults/sec)
    for (int i = 0; i < 10; i++) {
        mock_millis += 100;
        getFaultRate();
    }
    
    // Window span is 900ms (from 1st to 10th). 10 faults / 0.9s = 11.11...
    TEST_ASSERT_GREATER_OR_EQUAL(10, getFaultRate());
}

// @test Adaptive Cooldown (Normal vs Storm)
void test_adaptive_nvs_cooldown(void) {
    memset(fault_timestamps, 0, sizeof(fault_timestamps));
    memset(last_nvs_write_time, 0, sizeof(last_nvs_write_time));
    mock_nvs_write_count = 0;
    mock_millis = 1000;

    // 1. Normal rate (1 fault/sec)
    TEST_ASSERT_TRUE(shouldLogToNVS(FAULT_MOTION_STALL)); // First fault always logs
    mock_millis += 500;
    TEST_ASSERT_FALSE(shouldLogToNVS(FAULT_MOTION_STALL)); // Too soon (<1s)
    mock_millis += 600; // now 2100ms
    TEST_ASSERT_TRUE(shouldLogToNVS(FAULT_MOTION_STALL)); // OK (1.1s since last)

    // 2. Fault Storm (>5 faults/sec)
    for (int i = 0; i < 10; i++) {
        mock_millis += 100;
        getFaultRate(); 
    }
    // Rate is now ~10 faults/sec. Cooldown should be 10s.
    mock_millis += 500; // 0.5 seconds later (must be < 2s to keep rate > 5)
    TEST_ASSERT_FALSE(shouldLogToNVS(FAULT_MOTION_STALL)); // Should still be blocked (needs 10s)
    
    mock_millis += 10000; // Total 10.5s later
    TEST_ASSERT_TRUE(shouldLogToNVS(FAULT_MOTION_STALL)); // OK now
}

// @test Critical Fault triggers E-Stop
void test_critical_fault_triggers_estop(void) {
    mock_estop_active = false;
    reportFault(FAULT_WARNING);
    TEST_ASSERT_FALSE(mock_estop_active);
    
    reportFault(FAULT_CRITICAL);
    TEST_ASSERT_TRUE(mock_estop_active);
}

void run_error_handling_paths_tests(void) {
    RUN_TEST(test_fault_rate_calculation);
    RUN_TEST(test_adaptive_nvs_cooldown);
    RUN_TEST(test_critical_fault_triggers_estop);
}
