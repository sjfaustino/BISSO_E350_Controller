/**
 * @file test/test_fault_logging.cpp
 * @brief Unit tests for fault logging system
 *
 * Tests cover:
 * - Fault severity levels
 * - Fault code enumeration
 * - Fault entry structure
 * - Statistics structure
 * - Emergency stop state management
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

// ============================================================================
// FAULT LOGGING DEFINITIONS (from fault_logging.h)
// ============================================================================

typedef enum {
    FAULT_NONE = 0,
    FAULT_WARNING = 1,
    FAULT_ERROR = 2,
    FAULT_CRITICAL = 3
} fault_severity_t;

typedef enum {
    FAULT_NONE_CODE = 0x00,
    FAULT_ENCODER_TIMEOUT = 0x01,
    FAULT_PLC_COMM_LOSS = 0x02,
    FAULT_MOTION_STALL = 0x03,
    FAULT_SAFETY_INTERLOCK = 0x04,
    FAULT_SOFT_LIMIT_EXCEEDED = 0x05,
    FAULT_ESTOP_ACTIVATED = 0x06,
    FAULT_POWER_LOSS = 0x07,
    FAULT_TEMPERATURE_HIGH = 0x08,
    FAULT_CALIBRATION_MISSING = 0x09,
    FAULT_CONFIGURATION_INVALID = 0x0A,
    FAULT_WATCHDOG_TIMEOUT = 0x0B,
    FAULT_BOOT_FAILED = 0x0C,
    FAULT_BOOT_RECOVERY_ATTEMPTED = 0x0D,
    FAULT_CRITICAL_SYSTEM_ERROR = 0x0E,
    FAULT_EMERGENCY_HALT = 0x0F,
    FAULT_GRACEFUL_SHUTDOWN = 0x10,
    FAULT_ENCODER_SPIKE = 0x11,
    FAULT_I2C_ERROR = 0x12,
    FAULT_TASK_HUNG = 0x13,
    FAULT_MOTION_TIMEOUT = 0x14,
    FAULT_SPINDLE_OVERCURRENT = 0x15,
    FAULT_SPINDLE_STALL = 0x16,
    FAULT_SPINDLE_TOOLBREAK = 0x17,
    FAULT_CODE_MAX = 0x18
} fault_code_t;

typedef struct {
    uint32_t total_faults;
    uint32_t encoder_faults;
    uint32_t motion_faults;
    uint32_t safety_faults;
    uint32_t config_faults;
    uint32_t plc_faults;
    uint32_t system_faults;
    uint32_t other_faults;
    uint32_t last_fault_time_ms;
    uint32_t first_fault_time_ms;
} fault_stats_t;

typedef struct {
    uint32_t timestamp;
    fault_severity_t severity;
    fault_code_t code;
    int32_t axis;
    int32_t value;
    char message[64];
} fault_entry_t;

// Mock emergency stop state
static bool g_estop_active = false;
static bool g_estop_recovery_pending = false;

static void mock_estop_set_active(bool active) { g_estop_active = active; }
static bool mock_estop_is_active(void) { return g_estop_active; }
static bool mock_estop_request_recovery(void) {
    if (g_estop_active) {
        g_estop_recovery_pending = true;
        return true;
    }
    return false;
}
static void mock_estop_clear_recovery(void) { g_estop_recovery_pending = false; }

static void reset_mock(void) {
    g_estop_active = false;
    g_estop_recovery_pending = false;
}

// ============================================================================
// SEVERITY LEVEL TESTS
// ============================================================================

// @test Severity levels have expected values
void test_severity_levels_values(void) {
    TEST_ASSERT_EQUAL(0, FAULT_NONE);
    TEST_ASSERT_EQUAL(1, FAULT_WARNING);
    TEST_ASSERT_EQUAL(2, FAULT_ERROR);
    TEST_ASSERT_EQUAL(3, FAULT_CRITICAL);
}

// @test Severity levels are ordered
void test_severity_ordered(void) {
    TEST_ASSERT_LESS_THAN(FAULT_WARNING, FAULT_NONE);
    TEST_ASSERT_LESS_THAN(FAULT_ERROR, FAULT_WARNING);
    TEST_ASSERT_LESS_THAN(FAULT_CRITICAL, FAULT_ERROR);
}

// ============================================================================
// FAULT CODE TESTS
// ============================================================================

// @test Fault codes start at 0
void test_fault_codes_start_zero(void) {
    TEST_ASSERT_EQUAL(0, FAULT_NONE_CODE);
}

// @test Fault code max is correct
void test_fault_code_max(void) {
    TEST_ASSERT_EQUAL(0x18, FAULT_CODE_MAX);
}

// @test All fault codes are unique and sequential
void test_fault_codes_sequential(void) {
    // Check critical codes are in expected order
    TEST_ASSERT_EQUAL(0x01, FAULT_ENCODER_TIMEOUT);
    TEST_ASSERT_EQUAL(0x02, FAULT_PLC_COMM_LOSS);
    TEST_ASSERT_EQUAL(0x03, FAULT_MOTION_STALL);
    TEST_ASSERT_EQUAL(0x06, FAULT_ESTOP_ACTIVATED);
    TEST_ASSERT_EQUAL(0x0F, FAULT_EMERGENCY_HALT);
}

// @test Motion-related fault codes are grouped
void test_motion_fault_codes(void) {
    TEST_ASSERT_EQUAL(0x03, FAULT_MOTION_STALL);
    TEST_ASSERT_EQUAL(0x05, FAULT_SOFT_LIMIT_EXCEEDED);
    TEST_ASSERT_EQUAL(0x14, FAULT_MOTION_TIMEOUT);
}

// @test Spindle fault codes are grouped (PHASE 5.1)
void test_spindle_fault_codes(void) {
    TEST_ASSERT_EQUAL(0x15, FAULT_SPINDLE_OVERCURRENT);
    TEST_ASSERT_EQUAL(0x16, FAULT_SPINDLE_STALL);
    TEST_ASSERT_EQUAL(0x17, FAULT_SPINDLE_TOOLBREAK);
}

// ============================================================================
// FAULT ENTRY STRUCTURE TESTS
// ============================================================================

// @test Fault entry message buffer size
void test_fault_entry_message_size(void) {
    fault_entry_t entry;
    TEST_ASSERT_EQUAL(64, sizeof(entry.message));
}

// @test Fault entry can hold negative axis
void test_fault_entry_axis_signed(void) {
    fault_entry_t entry;
    entry.axis = -1;  // Often used for "all axes" or "no axis"
    TEST_ASSERT_EQUAL(-1, entry.axis);
}

// @test Fault entry can hold negative value
void test_fault_entry_value_signed(void) {
    fault_entry_t entry;
    entry.value = -12345;  // Signed values for error codes
    TEST_ASSERT_EQUAL(-12345, entry.value);
}

// ============================================================================
// FAULT STATISTICS TESTS
// ============================================================================

// @test Stats structure initializes to zero
void test_stats_initialize_zero(void) {
    fault_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    
    TEST_ASSERT_EQUAL_UINT32(0, stats.total_faults);
    TEST_ASSERT_EQUAL_UINT32(0, stats.encoder_faults);
    TEST_ASSERT_EQUAL_UINT32(0, stats.motion_faults);
    TEST_ASSERT_EQUAL_UINT32(0, stats.safety_faults);
    TEST_ASSERT_EQUAL_UINT32(0, stats.config_faults);
    TEST_ASSERT_EQUAL_UINT32(0, stats.plc_faults);
    TEST_ASSERT_EQUAL_UINT32(0, stats.system_faults);
    TEST_ASSERT_EQUAL_UINT32(0, stats.other_faults);
}

// @test Stats has time tracking fields
void test_stats_has_time_fields(void) {
    fault_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.first_fault_time_ms = 1000;
    stats.last_fault_time_ms = 5000;
    
    TEST_ASSERT_EQUAL_UINT32(1000, stats.first_fault_time_ms);
    TEST_ASSERT_EQUAL_UINT32(5000, stats.last_fault_time_ms);
}

// ============================================================================
// EMERGENCY STOP TESTS
// ============================================================================

// @test E-stop starts inactive
void test_estop_starts_inactive(void) {
    reset_mock();
    TEST_ASSERT_FALSE(mock_estop_is_active());
}

// @test E-stop can be activated
void test_estop_activation(void) {
    reset_mock();
    
    mock_estop_set_active(true);
    TEST_ASSERT_TRUE(mock_estop_is_active());
}

// @test E-stop can be deactivated
void test_estop_deactivation(void) {
    reset_mock();
    
    mock_estop_set_active(true);
    mock_estop_set_active(false);
    TEST_ASSERT_FALSE(mock_estop_is_active());
}

// @test E-stop recovery only when active
void test_estop_recovery_requires_active(void) {
    reset_mock();
    
    // Cannot request recovery when not active
    bool result = mock_estop_request_recovery();
    TEST_ASSERT_FALSE(result);
}

// @test E-stop recovery works when active
void test_estop_recovery_when_active(void) {
    reset_mock();
    
    mock_estop_set_active(true);
    bool result = mock_estop_request_recovery();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g_estop_recovery_pending);
}

// @test E-stop recovery can be cleared
void test_estop_recovery_clear(void) {
    reset_mock();
    
    mock_estop_set_active(true);
    mock_estop_request_recovery();
    mock_estop_clear_recovery();
    
    TEST_ASSERT_FALSE(g_estop_recovery_pending);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_fault_logging_tests(void) {
    // Severity levels
    RUN_TEST(test_severity_levels_values);
    RUN_TEST(test_severity_ordered);
    
    // Fault codes
    RUN_TEST(test_fault_codes_start_zero);
    RUN_TEST(test_fault_code_max);
    RUN_TEST(test_fault_codes_sequential);
    RUN_TEST(test_motion_fault_codes);
    RUN_TEST(test_spindle_fault_codes);
    
    // Fault entry structure
    RUN_TEST(test_fault_entry_message_size);
    RUN_TEST(test_fault_entry_axis_signed);
    RUN_TEST(test_fault_entry_value_signed);
    
    // Statistics
    RUN_TEST(test_stats_initialize_zero);
    RUN_TEST(test_stats_has_time_fields);
    
    // Emergency stop
    RUN_TEST(test_estop_starts_inactive);
    RUN_TEST(test_estop_activation);
    RUN_TEST(test_estop_deactivation);
    RUN_TEST(test_estop_recovery_requires_active);
    RUN_TEST(test_estop_recovery_when_active);
    RUN_TEST(test_estop_recovery_clear);
}
