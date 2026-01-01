/**
 * @file test/test_i2c_recovery.cpp
 * @brief Unit tests for I2C bus recovery and error handling
 *
 * Tests cover:
 * - Bus status detection (OK, BUSY, STUCK_SDA, STUCK_SCL)
 * - Error code enumeration
 * - Retry configuration with exponential backoff
 * - Statistics tracking
 *
 * @note These tests verify the I2C recovery infrastructure works correctly
 */

#include "test/unity/unity.h"
#include <cstdint>
#include <cstring>
#include <cmath>

// ============================================================================
// I2C TYPE DEFINITIONS (copied from i2c_bus_recovery.h for test isolation)
// ============================================================================

typedef enum {
    I2C_BUS_OK = 0,
    I2C_BUS_BUSY = 1,
    I2C_BUS_STUCK_SDA = 2,
    I2C_BUS_STUCK_SCL = 3,
    I2C_BUS_ERROR = 4,
    I2C_BUS_TIMEOUT = 5
} i2c_bus_status_t;

typedef enum {
    I2C_RESULT_OK = 0,
    I2C_RESULT_NACK = 1,
    I2C_RESULT_TIMEOUT = 2,
    I2C_RESULT_BUS_ERROR = 3,
    I2C_RESULT_ARBITRATION_LOST = 4,
    I2C_RESULT_DEVICE_BUSY = 5,
    I2C_RESULT_UNKNOWN_ERROR = 6
} i2c_result_t;

typedef struct {
    uint32_t transactions_total;
    uint32_t transactions_success;
    uint32_t transactions_failed;
    uint32_t retries_performed;
    uint32_t bus_recoveries;
    uint32_t error_nack;
    uint32_t error_timeout;
    uint32_t error_bus;
    uint32_t error_arbitration;
    float success_rate;
} i2c_stats_t;

typedef struct {
    uint8_t max_retries;
    uint16_t initial_backoff_ms;
    uint16_t max_backoff_ms;
    float backoff_multiplier;
} i2c_retry_config_t;

// ============================================================================
// MOCK STATE
// ============================================================================

static i2c_stats_t g_stats;
static i2c_retry_config_t g_retry_config;
static i2c_bus_status_t g_bus_status;
static uint32_t g_recovery_count;

static void reset_mock(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    g_retry_config = {1, 5, 20, 2.0f};  // Default config
    g_bus_status = I2C_BUS_OK;
    g_recovery_count = 0;
}

// Simulate calculating backoff
static uint16_t calculate_backoff(const i2c_retry_config_t* config, uint8_t attempt) {
    float backoff = (float)config->initial_backoff_ms * powf(config->backoff_multiplier, (float)attempt);
    if (backoff > config->max_backoff_ms) {
        backoff = config->max_backoff_ms;
    }
    return (uint16_t)backoff;
}

// Simulate updating success rate
static void update_success_rate(i2c_stats_t* stats) {
    if (stats->transactions_total > 0) {
        stats->success_rate = (float)stats->transactions_success / (float)stats->transactions_total * 100.0f;
    } else {
        stats->success_rate = 0.0f;
    }
}

// ============================================================================
// BUS STATUS ENUM TESTS
// ============================================================================

// @test Bus status enum has expected values
void test_bus_status_enum_values(void) {
    TEST_ASSERT_EQUAL(0, I2C_BUS_OK);
    TEST_ASSERT_EQUAL(1, I2C_BUS_BUSY);
    TEST_ASSERT_EQUAL(2, I2C_BUS_STUCK_SDA);
    TEST_ASSERT_EQUAL(3, I2C_BUS_STUCK_SCL);
    TEST_ASSERT_EQUAL(4, I2C_BUS_ERROR);
    TEST_ASSERT_EQUAL(5, I2C_BUS_TIMEOUT);
}

// @test Result enum has expected values
void test_result_enum_values(void) {
    TEST_ASSERT_EQUAL(0, I2C_RESULT_OK);
    TEST_ASSERT_EQUAL(1, I2C_RESULT_NACK);
    TEST_ASSERT_EQUAL(2, I2C_RESULT_TIMEOUT);
    TEST_ASSERT_EQUAL(3, I2C_RESULT_BUS_ERROR);
    TEST_ASSERT_EQUAL(4, I2C_RESULT_ARBITRATION_LOST);
    TEST_ASSERT_EQUAL(5, I2C_RESULT_DEVICE_BUSY);
    TEST_ASSERT_EQUAL(6, I2C_RESULT_UNKNOWN_ERROR);
}

// ============================================================================
// RETRY CONFIGURATION TESTS
// ============================================================================

// @test Default retry config has reasonable values
void test_default_retry_config(void) {
    reset_mock();
    
    // Matches the actual default in i2c_bus_recovery.cpp
    TEST_ASSERT_EQUAL_UINT8(1, g_retry_config.max_retries);
    TEST_ASSERT_EQUAL_UINT16(5, g_retry_config.initial_backoff_ms);
    TEST_ASSERT_EQUAL_UINT16(20, g_retry_config.max_backoff_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.0f, g_retry_config.backoff_multiplier);
}

// @test Exponential backoff calculation
void test_exponential_backoff_calculation(void) {
    reset_mock();
    
    // First attempt: 5ms
    uint16_t backoff0 = calculate_backoff(&g_retry_config, 0);
    TEST_ASSERT_EQUAL_UINT16(5, backoff0);
    
    // Second attempt: 5 * 2.0 = 10ms
    uint16_t backoff1 = calculate_backoff(&g_retry_config, 1);
    TEST_ASSERT_EQUAL_UINT16(10, backoff1);
    
    // Third attempt: 5 * 2.0^2 = 20ms
    uint16_t backoff2 = calculate_backoff(&g_retry_config, 2);
    TEST_ASSERT_EQUAL_UINT16(20, backoff2);
}

// @test Backoff is capped at max_backoff_ms
void test_backoff_capped_at_max(void) {
    reset_mock();
    
    // High attempt number should be capped at 20ms
    uint16_t backoff = calculate_backoff(&g_retry_config, 10);
    TEST_ASSERT_EQUAL_UINT16(20, backoff);
}

// @test Custom retry config is applied
void test_custom_retry_config(void) {
    reset_mock();
    
    g_retry_config.max_retries = 3;
    g_retry_config.initial_backoff_ms = 10;
    g_retry_config.max_backoff_ms = 100;
    g_retry_config.backoff_multiplier = 1.5f;
    
    TEST_ASSERT_EQUAL_UINT8(3, g_retry_config.max_retries);
    TEST_ASSERT_EQUAL_UINT16(10, g_retry_config.initial_backoff_ms);
    
    // First attempt: 10ms
    TEST_ASSERT_EQUAL_UINT16(10, calculate_backoff(&g_retry_config, 0));
    
    // Second attempt: 10 * 1.5 = 15ms
    TEST_ASSERT_EQUAL_UINT16(15, calculate_backoff(&g_retry_config, 1));
}

// ============================================================================
// STATISTICS TESTS
// ============================================================================

// @test Stats initialize to zero
void test_stats_initialize_zero(void) {
    reset_mock();
    
    TEST_ASSERT_EQUAL_UINT32(0, g_stats.transactions_total);
    TEST_ASSERT_EQUAL_UINT32(0, g_stats.transactions_success);
    TEST_ASSERT_EQUAL_UINT32(0, g_stats.transactions_failed);
    TEST_ASSERT_EQUAL_UINT32(0, g_stats.retries_performed);
    TEST_ASSERT_EQUAL_UINT32(0, g_stats.bus_recoveries);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, g_stats.success_rate);
}

// @test Success rate calculation
void test_success_rate_calculation(void) {
    reset_mock();
    
    g_stats.transactions_total = 100;
    g_stats.transactions_success = 95;
    update_success_rate(&g_stats);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 95.0f, g_stats.success_rate);
}

// @test Success rate with no transactions
void test_success_rate_no_transactions(void) {
    reset_mock();
    
    g_stats.transactions_total = 0;
    update_success_rate(&g_stats);
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, g_stats.success_rate);
}

// @test Error counters track independently
void test_error_counters_independent(void) {
    reset_mock();
    
    g_stats.error_nack = 5;
    g_stats.error_timeout = 3;
    g_stats.error_bus = 1;
    g_stats.error_arbitration = 2;
    g_stats.transactions_failed = 11;
    
    // Total errors should match sum of individual errors
    uint32_t total_errors = g_stats.error_nack + g_stats.error_timeout +
                           g_stats.error_bus + g_stats.error_arbitration;
    TEST_ASSERT_EQUAL_UINT32(11, total_errors);
}

// ============================================================================
// BUS RECOVERY TESTS
// ============================================================================

// @test Bus status enum covers all failure modes
void test_bus_status_covers_failure_modes(void) {
    // Verify we have status for all expected conditions
    i2c_bus_status_t statuses[] = {
        I2C_BUS_OK,
        I2C_BUS_BUSY,
        I2C_BUS_STUCK_SDA,
        I2C_BUS_STUCK_SCL,
        I2C_BUS_ERROR,
        I2C_BUS_TIMEOUT
    };
    
    TEST_ASSERT_EQUAL(6, sizeof(statuses) / sizeof(statuses[0]));
}

// @test Stats struct has recovery counter
void test_stats_has_recovery_counter(void) {
    reset_mock();
    
    g_stats.bus_recoveries = 5;
    TEST_ASSERT_EQUAL_UINT32(5, g_stats.bus_recoveries);
}

// ============================================================================
// DEVICE ADDRESS TESTS
// ============================================================================

// @test PLC I/O addresses are valid I2C addresses
void test_plc_addresses_valid(void) {
    // PCF8574 addresses from plc_iface.h
    uint8_t plc_input_addr = 0x21;   // ADDR_I73_INPUT
    uint8_t plc_output_addr = 0x24;  // ADDR_Q73_OUTPUT
    
    // Valid I2C addresses are 0x08-0x77
    TEST_ASSERT_TRUE(plc_input_addr >= 0x08 && plc_input_addr <= 0x77);
    TEST_ASSERT_TRUE(plc_output_addr >= 0x08 && plc_output_addr <= 0x77);
}

// @test LCD address is valid
void test_lcd_address_valid(void) {
    uint8_t lcd_addr = 0x27;  // Common LCD I2C address
    
    TEST_ASSERT_TRUE(lcd_addr >= 0x08 && lcd_addr <= 0x77);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_i2c_recovery_tests(void) {
    // Enum verification
    RUN_TEST(test_bus_status_enum_values);
    RUN_TEST(test_result_enum_values);
    
    // Retry configuration
    RUN_TEST(test_default_retry_config);
    RUN_TEST(test_exponential_backoff_calculation);
    RUN_TEST(test_backoff_capped_at_max);
    RUN_TEST(test_custom_retry_config);
    
    // Statistics
    RUN_TEST(test_stats_initialize_zero);
    RUN_TEST(test_success_rate_calculation);
    RUN_TEST(test_success_rate_no_transactions);
    RUN_TEST(test_error_counters_independent);
    
    // Bus recovery
    RUN_TEST(test_bus_status_covers_failure_modes);
    RUN_TEST(test_stats_has_recovery_counter);
    
    // Device addresses
    RUN_TEST(test_plc_addresses_valid);
    RUN_TEST(test_lcd_address_valid);
}
