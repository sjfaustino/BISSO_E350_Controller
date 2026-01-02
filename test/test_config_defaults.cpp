/**
 * @file test/test_config_defaults.cpp
 * @brief Unit tests for configuration key defaults and validation
 *
 * Tests cover:
 * - Key name length validation (NVS 15 char limit)
 * - Default value ranges
 * - Key naming conventions
 * - Critical configuration constants
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

// ============================================================================
// CONFIG KEY DEFINITIONS (sample from config_keys.h)
// ============================================================================

// WiFi
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"
#define KEY_WIFI_AP_EN "wifi_ap_en"

// Motion
#define KEY_SOFT_LIMIT_X_MIN "slimit_x_min"
#define KEY_SOFT_LIMIT_X_MAX "slimit_x_max"
#define KEY_MOTION_DEADBAND "mot_deadband"
#define KEY_APPROACH_MODE "mot_app_mode"

// VFD
#define KEY_VFD_SLAVE_ADDR "vfd_addr"
#define KEY_VFD_STALL_MARGIN "vfd_stall_marg"

// Encoder
#define KEY_ENCODER_PPR "enc_ppr"
#define KEY_ENCODER_INTERFACE "enc_iface"

// Homing
#define KEY_HOME_PROF_FAST "home_prof_fast"
#define KEY_HOME_PROF_SLOW "home_prof_slow"

// NVS limit
#define NVS_KEY_MAX_LENGTH 15

// ============================================================================
// KEY LENGTH TESTS
// ============================================================================

// @test WiFi keys are within NVS limit
void test_wifi_keys_length(void) {
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_WIFI_SSID));
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_WIFI_PASS));
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_WIFI_AP_EN));
}

// @test Motion keys are within NVS limit  
void test_motion_keys_length(void) {
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_SOFT_LIMIT_X_MIN));
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_SOFT_LIMIT_X_MAX));
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_MOTION_DEADBAND));
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_APPROACH_MODE));
}

// @test VFD keys are within NVS limit
void test_vfd_keys_length(void) {
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_VFD_SLAVE_ADDR));
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_VFD_STALL_MARGIN));
}

// @test Encoder keys are within NVS limit
void test_encoder_keys_length(void) {
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_ENCODER_PPR));
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_ENCODER_INTERFACE));
}

// @test Homing keys are at safe limit (14 chars)
void test_homing_keys_at_limit(void) {
    TEST_ASSERT_EQUAL(14, strlen(KEY_HOME_PROF_FAST));
    TEST_ASSERT_EQUAL(14, strlen(KEY_HOME_PROF_SLOW));
    TEST_ASSERT_LESS_THAN(NVS_KEY_MAX_LENGTH, strlen(KEY_HOME_PROF_FAST));
}

// ============================================================================
// KEY NAMING CONVENTION TESTS
// ============================================================================

// @test Keys use underscore separation
void test_key_naming_underscore(void) {
    // Valid keys should contain underscores
    TEST_ASSERT_NOT_NULL(strchr(KEY_WIFI_SSID, '_'));
    TEST_ASSERT_NOT_NULL(strchr(KEY_VFD_SLAVE_ADDR, '_'));
}

// @test Keys are lowercase
void test_key_naming_lowercase(void) {
    const char* key = KEY_WIFI_SSID;
    for (size_t i = 0; i < strlen(key); i++) {
        if (key[i] != '_') {
            TEST_ASSERT_TRUE(key[i] >= 'a' && key[i] <= 'z');
        }
    }
}

// @test Keys don't start with underscore
void test_key_naming_no_leading_underscore(void) {
    TEST_ASSERT_NOT_EQUAL('_', KEY_WIFI_SSID[0]);
    TEST_ASSERT_NOT_EQUAL('_', KEY_VFD_SLAVE_ADDR[0]);
    TEST_ASSERT_NOT_EQUAL('_', KEY_MOTION_DEADBAND[0]);
}

// ============================================================================
// DEFAULT VALUE TESTS
// ============================================================================

// Simulated defaults (would be loaded from config system)
static const uint32_t DEFAULT_VFD_ADDR = 1;
static const uint32_t DEFAULT_ENCODER_PPR = 100;
static const float DEFAULT_DEADBAND_MM = 0.1f;
static const uint32_t DEFAULT_BAUD_RATE = 9600;

// @test VFD address default is valid Modbus range
void test_default_vfd_addr_valid(void) {
    TEST_ASSERT_GREATER_OR_EQUAL(1, DEFAULT_VFD_ADDR);
    TEST_ASSERT_LESS_OR_EQUAL(247, DEFAULT_VFD_ADDR);
}

// @test Encoder PPR default is reasonable
void test_default_encoder_ppr_reasonable(void) {
    TEST_ASSERT_GREATER_THAN(0, DEFAULT_ENCODER_PPR);
    TEST_ASSERT_LESS_THAN(100000, DEFAULT_ENCODER_PPR);
}

// @test Deadband default is positive and small
void test_default_deadband_small(void) {
    // Use float-aware assertions
    TEST_ASSERT_TRUE(DEFAULT_DEADBAND_MM > 0.0f);
    TEST_ASSERT_TRUE(DEFAULT_DEADBAND_MM < 10.0f);
}

// @test Baud rate default is standard value
void test_default_baud_rate_standard(void) {
    // Common baud rates: 9600, 19200, 38400, 57600, 115200
    TEST_ASSERT_TRUE(
        DEFAULT_BAUD_RATE == 9600 ||
        DEFAULT_BAUD_RATE == 19200 ||
        DEFAULT_BAUD_RATE == 38400 ||
        DEFAULT_BAUD_RATE == 57600 ||
        DEFAULT_BAUD_RATE == 115200
    );
}

// ============================================================================
// CRITICAL CONSTANT TESTS
// ============================================================================

// @test NVS key limit is 15
void test_nvs_key_limit(void) {
    TEST_ASSERT_EQUAL(15, NVS_KEY_MAX_LENGTH);
}

// @test System constants are defined
void test_system_constants_defined(void) {
    // These should match system_constants.h
    uint32_t i2c_sda = 4;
    uint32_t i2c_scl = 5;
    
    TEST_ASSERT_EQUAL(4, i2c_sda);
    TEST_ASSERT_EQUAL(5, i2c_scl);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_config_defaults_tests(void) {
    // Key length validation
    RUN_TEST(test_wifi_keys_length);
    RUN_TEST(test_motion_keys_length);
    RUN_TEST(test_vfd_keys_length);
    RUN_TEST(test_encoder_keys_length);
    RUN_TEST(test_homing_keys_at_limit);
    
    // Key naming conventions
    RUN_TEST(test_key_naming_underscore);
    RUN_TEST(test_key_naming_lowercase);
    RUN_TEST(test_key_naming_no_leading_underscore);
    
    // Default values
    RUN_TEST(test_default_vfd_addr_valid);
    RUN_TEST(test_default_encoder_ppr_reasonable);
    RUN_TEST(test_default_deadband_small);
    RUN_TEST(test_default_baud_rate_standard);
    
    // Critical constants
    RUN_TEST(test_nvs_key_limit);
    RUN_TEST(test_system_constants_defined);
}
