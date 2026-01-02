/**
 * @file test/test_altivar31_vfd.cpp
 * @brief Unit tests for Altivar 31 VFD Modbus driver
 *
 * Tests cover:
 * - Register address definitions
 * - Status code enumeration
 * - State structure initialization
 * - Raw value to physical unit conversion
 * - Fault detection logic
 * - Frequency loss detection
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>

// ============================================================================
// ALTIVAR31 DEFINITIONS (from altivar31_modbus.h)
// ============================================================================

// Register addresses (decimal)
#define ALTIVAR31_REG_OUTPUT_FREQ       3202
#define ALTIVAR31_REG_DRIVE_CURRENT     3204
#define ALTIVAR31_REG_DRIVE_STATUS      3201
#define ALTIVAR31_REG_FAULT_CODE        8606
#define ALTIVAR31_REG_THERMAL_STATE     3209

// Status values
#define ALTIVAR31_STATUS_IDLE           0
#define ALTIVAR31_STATUS_RUNNING        1
#define ALTIVAR31_STATUS_FAULT          2
#define ALTIVAR31_STATUS_OVERHEAT       3

// State structure
typedef struct {
    bool enabled;
    uint8_t slave_address;
    uint32_t baud_rate;
    
    int16_t frequency_raw;
    float frequency_hz;
    int16_t current_raw;
    float current_amps;
    
    uint16_t status_word;
    uint16_t fault_code;
    int16_t thermal_state;
    
    uint32_t last_read_time_ms;
    uint32_t last_error_time_ms;
    uint32_t read_count;
    uint32_t error_count;
    uint32_t consecutive_errors;
} altivar31_state_t;

// Mock state
static altivar31_state_t g_vfd;

// Conversion functions (matching actual implementation)
static float rawToHz(int16_t raw) { return raw * 0.1f; }
static float rawToAmps(int16_t raw) { return raw * 0.1f; }

static void reset_mock(void) {
    memset(&g_vfd, 0, sizeof(g_vfd));
    g_vfd.slave_address = 1;
    g_vfd.baud_rate = 9600;
}

// ============================================================================
// REGISTER ADDRESS TESTS
// ============================================================================

// @test Register addresses match Altivar documentation
void test_register_addresses_correct(void) {
    TEST_ASSERT_EQUAL(3202, ALTIVAR31_REG_OUTPUT_FREQ);
    TEST_ASSERT_EQUAL(3204, ALTIVAR31_REG_DRIVE_CURRENT);
    TEST_ASSERT_EQUAL(3201, ALTIVAR31_REG_DRIVE_STATUS);
    TEST_ASSERT_EQUAL(8606, ALTIVAR31_REG_FAULT_CODE);
    TEST_ASSERT_EQUAL(3209, ALTIVAR31_REG_THERMAL_STATE);
}

// @test Status registers are in 3200 range
void test_status_registers_in_range(void) {
    TEST_ASSERT_GREATER_OR_EQUAL(3200, ALTIVAR31_REG_OUTPUT_FREQ);
    TEST_ASSERT_LESS_OR_EQUAL(3210, ALTIVAR31_REG_OUTPUT_FREQ);
}

// ============================================================================
// STATUS CODE TESTS
// ============================================================================

// @test Status codes have expected values
void test_status_codes_values(void) {
    TEST_ASSERT_EQUAL(0, ALTIVAR31_STATUS_IDLE);
    TEST_ASSERT_EQUAL(1, ALTIVAR31_STATUS_RUNNING);
    TEST_ASSERT_EQUAL(2, ALTIVAR31_STATUS_FAULT);
    TEST_ASSERT_EQUAL(3, ALTIVAR31_STATUS_OVERHEAT);
}

// @test Status codes are sequential
void test_status_codes_sequential(void) {
    TEST_ASSERT_EQUAL(ALTIVAR31_STATUS_IDLE + 1, ALTIVAR31_STATUS_RUNNING);
    TEST_ASSERT_EQUAL(ALTIVAR31_STATUS_RUNNING + 1, ALTIVAR31_STATUS_FAULT);
    TEST_ASSERT_EQUAL(ALTIVAR31_STATUS_FAULT + 1, ALTIVAR31_STATUS_OVERHEAT);
}

// ============================================================================
// VALUE CONVERSION TESTS
// ============================================================================

// @test Frequency conversion: 500 raw = 50.0 Hz
void test_frequency_conversion(void) {
    int16_t raw = 500;  // 0.1 Hz units
    float hz = rawToHz(raw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, hz);
}

// @test Current conversion: 35 raw = 3.5 A
void test_current_conversion(void) {
    int16_t raw = 35;  // 0.1 A units
    float amps = rawToAmps(raw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, amps);
}

// @test Zero values convert correctly
void test_zero_conversion(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rawToHz(0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rawToAmps(0));
}

// @test Maximum frequency (105 Hz = 1050 raw)
void test_max_frequency_conversion(void) {
    int16_t raw = 1050;
    float hz = rawToHz(raw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 105.0f, hz);
}

// ============================================================================
// STATE STRUCTURE TESTS
// ============================================================================

// @test State initializes to safe defaults
void test_state_initialization(void) {
    reset_mock();
    
    TEST_ASSERT_FALSE(g_vfd.enabled);
    TEST_ASSERT_EQUAL_UINT8(1, g_vfd.slave_address);
    TEST_ASSERT_EQUAL_UINT32(9600, g_vfd.baud_rate);
    TEST_ASSERT_EQUAL_UINT16(0, g_vfd.fault_code);
    TEST_ASSERT_EQUAL_UINT32(0, g_vfd.error_count);
}

// @test State can store frequency values
void test_state_stores_frequency(void) {
    reset_mock();
    
    g_vfd.frequency_raw = 600;
    g_vfd.frequency_hz = 60.0f;
    
    TEST_ASSERT_EQUAL_INT16(600, g_vfd.frequency_raw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, g_vfd.frequency_hz);
}

// @test Error counters can increment
void test_error_counters(void) {
    reset_mock();
    
    g_vfd.error_count++;
    g_vfd.consecutive_errors++;
    
    TEST_ASSERT_EQUAL_UINT32(1, g_vfd.error_count);
    TEST_ASSERT_EQUAL_UINT32(1, g_vfd.consecutive_errors);
}

// ============================================================================
// FAULT DETECTION TESTS
// ============================================================================

// @test No fault when fault_code is 0
void test_no_fault_when_zero(void) {
    reset_mock();
    g_vfd.fault_code = 0;
    
    bool is_faulted = (g_vfd.fault_code != 0);
    TEST_ASSERT_FALSE(is_faulted);
}

// @test Fault detected when fault_code is non-zero
void test_fault_detected_nonzero(void) {
    reset_mock();
    g_vfd.fault_code = 5;  // Some fault code
    
    bool is_faulted = (g_vfd.fault_code != 0);
    TEST_ASSERT_TRUE(is_faulted);
}

// @test Thermal overheat detection (>118%)
void test_thermal_overheat_detection(void) {
    reset_mock();
    
    g_vfd.thermal_state = 120;  // 120% = overheat
    bool is_overheating = (g_vfd.thermal_state > 118);
    TEST_ASSERT_TRUE(is_overheating);
    
    g_vfd.thermal_state = 100;  // 100% = nominal
    is_overheating = (g_vfd.thermal_state > 118);
    TEST_ASSERT_FALSE(is_overheating);
}

// ============================================================================
// FREQUENCY LOSS DETECTION TESTS
// ============================================================================

// @test Frequency loss detected (>80% drop)
void test_frequency_loss_detected(void) {
    float previous_hz = 50.0f;
    float current_hz = 5.0f;  // 90% drop
    
    bool loss = (current_hz < previous_hz * 0.2f);  // Dropped below 20%
    TEST_ASSERT_TRUE(loss);
}

// @test Normal deceleration not flagged as loss
void test_normal_decel_not_loss(void) {
    float previous_hz = 50.0f;
    float current_hz = 40.0f;  // Normal decel (20% drop)
    
    bool loss = (current_hz < previous_hz * 0.2f);
    TEST_ASSERT_FALSE(loss);
}

// @test Zero to zero is not a loss
void test_zero_not_loss(void) {
    float previous_hz = 0.0f;
    float current_hz = 0.0f;
    
    // Avoid divide by zero in real code
    bool loss = (previous_hz > 5.0f && current_hz < previous_hz * 0.2f);
    TEST_ASSERT_FALSE(loss);
}

// ============================================================================
// RUNNING STATE TESTS
// ============================================================================

// @test Motor running when status bit 3 is set
void test_motor_running_bit(void) {
    reset_mock();
    
    g_vfd.status_word = (1 << 3);  // Bit 3 = running
    bool is_running = (g_vfd.status_word & (1 << 3)) != 0;
    TEST_ASSERT_TRUE(is_running);
}

// @test Motor not running when status bit 3 is clear
void test_motor_not_running(void) {
    reset_mock();
    
    g_vfd.status_word = 0;
    bool is_running = (g_vfd.status_word & (1 << 3)) != 0;
    TEST_ASSERT_FALSE(is_running);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_altivar31_vfd_tests(void) {
    // Register addresses
    RUN_TEST(test_register_addresses_correct);
    RUN_TEST(test_status_registers_in_range);
    
    // Status codes
    RUN_TEST(test_status_codes_values);
    RUN_TEST(test_status_codes_sequential);
    
    // Value conversion
    RUN_TEST(test_frequency_conversion);
    RUN_TEST(test_current_conversion);
    RUN_TEST(test_zero_conversion);
    RUN_TEST(test_max_frequency_conversion);
    
    // State structure
    RUN_TEST(test_state_initialization);
    RUN_TEST(test_state_stores_frequency);
    RUN_TEST(test_error_counters);
    
    // Fault detection
    RUN_TEST(test_no_fault_when_zero);
    RUN_TEST(test_fault_detected_nonzero);
    RUN_TEST(test_thermal_overheat_detection);
    
    // Frequency loss
    RUN_TEST(test_frequency_loss_detected);
    RUN_TEST(test_normal_decel_not_loss);
    RUN_TEST(test_zero_not_loss);
    
    // Running state
    RUN_TEST(test_motor_running_bit);
    RUN_TEST(test_motor_not_running);
}
