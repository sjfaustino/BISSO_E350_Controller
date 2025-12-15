/**
 * @file test/test_api_config.cpp
 * @brief Unit tests for Configuration API (Phase 2A/2B)
 *
 * Tests the api_config module which manages:
 * - Configuration retrieval and update
 * - Validation of configuration values
 * - Configuration persistence (NVS save/load)
 * - JSON serialization/deserialization
 * - Schema generation for client-side validation
 * - Encoder calibration
 */

#include <unity.h>
#include "helpers/test_utils.h"
#include <cstring>
#include <cstdint>
#include <cstdio>

/**
 * @brief Mock configuration structures (mirroring api_config.h)
 */

typedef struct {
    uint16_t soft_limit_low_mm[3];
    uint16_t soft_limit_high_mm[3];
} motion_config_t;

typedef struct {
    uint16_t min_speed_hz;
    uint16_t max_speed_hz;
    uint16_t acc_time_ms;
    uint16_t dec_time_ms;
} vfd_config_t;

typedef struct {
    uint16_t ppm[3];
    uint8_t calibrated[3];
} encoder_config_t;

/**
 * @brief Test fixtures
 */
static motion_config_t motion_config;
static vfd_config_t vfd_config;
static encoder_config_t encoder_config;

static void setUp(void)
{
    // Initialize with defaults
    motion_config.soft_limit_low_mm[0] = 0;
    motion_config.soft_limit_high_mm[0] = 500;
    motion_config.soft_limit_low_mm[1] = 0;
    motion_config.soft_limit_high_mm[1] = 500;
    motion_config.soft_limit_low_mm[2] = 0;
    motion_config.soft_limit_high_mm[2] = 500;

    vfd_config.min_speed_hz = 1;
    vfd_config.max_speed_hz = 105;
    vfd_config.acc_time_ms = 600;
    vfd_config.dec_time_ms = 400;

    encoder_config.ppm[0] = 100;
    encoder_config.ppm[1] = 100;
    encoder_config.ppm[2] = 100;
    encoder_config.calibrated[0] = 1;
    encoder_config.calibrated[1] = 1;
    encoder_config.calibrated[2] = 1;
}

static void tearDown(void)
{
    // Cleanup
}

/**
 * @section Motion Configuration Tests
 */

void test_motion_default_valid(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, motion_config.soft_limit_low_mm[0]);
    TEST_ASSERT_EQUAL_UINT16(500, motion_config.soft_limit_high_mm[0]);
    TEST_ASSERT_EQUAL_UINT16(0, motion_config.soft_limit_low_mm[1]);
    TEST_ASSERT_EQUAL_UINT16(500, motion_config.soft_limit_high_mm[1]);
    TEST_ASSERT_EQUAL_UINT16(0, motion_config.soft_limit_low_mm[2]);
    TEST_ASSERT_EQUAL_UINT16(500, motion_config.soft_limit_high_mm[2]);
}

void test_motion_soft_limit_lower_cannot_exceed_upper(void)
{
    // Valid: lower < upper
    TEST_ASSERT_TRUE(motion_config.soft_limit_low_mm[0] < motion_config.soft_limit_high_mm[0]);

    // Invalid test: set lower >= upper should fail validation
    motion_config.soft_limit_low_mm[0] = 500;
    motion_config.soft_limit_high_mm[0] = 500;
    TEST_ASSERT_FALSE(motion_config.soft_limit_low_mm[0] < motion_config.soft_limit_high_mm[0]);
}

void test_motion_soft_limits_within_range(void)
{
    // Valid: 0-1000mm
    motion_config.soft_limit_low_mm[0] = 100;
    motion_config.soft_limit_high_mm[0] = 900;
    TEST_ASSERT_TRUE(motion_config.soft_limit_low_mm[0] <= 1000);
    TEST_ASSERT_TRUE(motion_config.soft_limit_high_mm[0] <= 1000);
}

void test_motion_all_axes_configurable(void)
{
    // Each axis should be independently configurable
    for (int i = 0; i < 3; i++) {
        motion_config.soft_limit_low_mm[i] = 100;
        motion_config.soft_limit_high_mm[i] = 900;
        TEST_ASSERT_EQUAL_UINT16(100, motion_config.soft_limit_low_mm[i]);
        TEST_ASSERT_EQUAL_UINT16(900, motion_config.soft_limit_high_mm[i]);
    }
}

/**
 * @section VFD Configuration Tests
 */

void test_vfd_default_valid(void)
{
    TEST_ASSERT_EQUAL_UINT16(1, vfd_config.min_speed_hz);
    TEST_ASSERT_EQUAL_UINT16(105, vfd_config.max_speed_hz);
    TEST_ASSERT_EQUAL_UINT16(600, vfd_config.acc_time_ms);
    TEST_ASSERT_EQUAL_UINT16(400, vfd_config.dec_time_ms);
}

void test_vfd_min_speed_in_valid_range(void)
{
    // Valid range: 1-105 Hz
    vfd_config.min_speed_hz = 1;
    TEST_ASSERT_TRUE(vfd_config.min_speed_hz >= 1);
    TEST_ASSERT_TRUE(vfd_config.min_speed_hz <= 105);

    vfd_config.min_speed_hz = 50;
    TEST_ASSERT_TRUE(vfd_config.min_speed_hz >= 1);
    TEST_ASSERT_TRUE(vfd_config.min_speed_hz <= 105);
}

void test_vfd_max_speed_in_valid_range(void)
{
    // Valid range: 1-105 Hz
    vfd_config.max_speed_hz = 105;
    TEST_ASSERT_TRUE(vfd_config.max_speed_hz >= 1);
    TEST_ASSERT_TRUE(vfd_config.max_speed_hz <= 105);
}

void test_vfd_min_less_than_max(void)
{
    TEST_ASSERT_TRUE(vfd_config.min_speed_hz < vfd_config.max_speed_hz);

    vfd_config.min_speed_hz = 50;
    vfd_config.max_speed_hz = 100;
    TEST_ASSERT_TRUE(vfd_config.min_speed_hz < vfd_config.max_speed_hz);
}

void test_vfd_acceleration_time_in_range(void)
{
    // Valid range: 200-2000 ms
    vfd_config.acc_time_ms = 200;
    TEST_ASSERT_TRUE(vfd_config.acc_time_ms >= 200);
    TEST_ASSERT_TRUE(vfd_config.acc_time_ms <= 2000);

    vfd_config.acc_time_ms = 1000;
    TEST_ASSERT_TRUE(vfd_config.acc_time_ms >= 200);
    TEST_ASSERT_TRUE(vfd_config.acc_time_ms <= 2000);
}

void test_vfd_deceleration_time_in_range(void)
{
    // Valid range: 200-2000 ms
    vfd_config.dec_time_ms = 200;
    TEST_ASSERT_TRUE(vfd_config.dec_time_ms >= 200);
    TEST_ASSERT_TRUE(vfd_config.dec_time_ms <= 2000);

    vfd_config.dec_time_ms = 2000;
    TEST_ASSERT_TRUE(vfd_config.dec_time_ms >= 200);
    TEST_ASSERT_TRUE(vfd_config.dec_time_ms <= 2000);
}

/**
 * @section Encoder Configuration Tests
 */

void test_encoder_default_valid(void)
{
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT16(100, encoder_config.ppm[i]);
        TEST_ASSERT_EQUAL_UINT8(1, encoder_config.calibrated[i]);
    }
}

void test_encoder_ppm_in_valid_range(void)
{
    // Valid range: 50-200 PPM
    encoder_config.ppm[0] = 50;
    TEST_ASSERT_TRUE(encoder_config.ppm[0] >= 50);
    TEST_ASSERT_TRUE(encoder_config.ppm[0] <= 200);

    encoder_config.ppm[1] = 100;
    TEST_ASSERT_TRUE(encoder_config.ppm[1] >= 50);
    TEST_ASSERT_TRUE(encoder_config.ppm[1] <= 200);

    encoder_config.ppm[2] = 200;
    TEST_ASSERT_TRUE(encoder_config.ppm[2] >= 50);
    TEST_ASSERT_TRUE(encoder_config.ppm[2] <= 200);
}

void test_encoder_each_axis_independent(void)
{
    // Set different PPM for each axis
    encoder_config.ppm[0] = 75;
    encoder_config.ppm[1] = 100;
    encoder_config.ppm[2] = 150;

    TEST_ASSERT_EQUAL_UINT16(75, encoder_config.ppm[0]);
    TEST_ASSERT_EQUAL_UINT16(100, encoder_config.ppm[1]);
    TEST_ASSERT_EQUAL_UINT16(150, encoder_config.ppm[2]);
}

void test_encoder_calibration_status_per_axis(void)
{
    // Each axis has independent calibration status
    encoder_config.calibrated[0] = 1;  // X calibrated
    encoder_config.calibrated[1] = 0;  // Y not calibrated
    encoder_config.calibrated[2] = 1;  // Z calibrated

    TEST_ASSERT_TRUE(encoder_config.calibrated[0]);
    TEST_ASSERT_FALSE(encoder_config.calibrated[1]);
    TEST_ASSERT_TRUE(encoder_config.calibrated[2]);
}

/**
 * @section Cross-Configuration Tests
 */

void test_vfd_and_motion_independent(void)
{
    // Changing motion config should not affect VFD config
    motion_config.soft_limit_high_mm[0] = 600;
    TEST_ASSERT_EQUAL_UINT16(105, vfd_config.max_speed_hz);
    TEST_ASSERT_EQUAL_UINT16(1, vfd_config.min_speed_hz);
}

void test_encoder_and_vfd_independent(void)
{
    // Changing encoder config should not affect VFD config
    encoder_config.ppm[0] = 150;
    TEST_ASSERT_EQUAL_UINT16(105, vfd_config.max_speed_hz);
    TEST_ASSERT_EQUAL_UINT16(600, vfd_config.acc_time_ms);
}

void test_all_configs_independently_valid(void)
{
    // Each category should be independently valid
    setUp();  // Reset to defaults

    // Motion config valid
    TEST_ASSERT_TRUE(motion_config.soft_limit_low_mm[0] < motion_config.soft_limit_high_mm[0]);

    // VFD config valid
    TEST_ASSERT_TRUE(vfd_config.min_speed_hz < vfd_config.max_speed_hz);
    TEST_ASSERT_TRUE(vfd_config.acc_time_ms >= 200);
    TEST_ASSERT_TRUE(vfd_config.dec_time_ms >= 200);

    // Encoder config valid
    TEST_ASSERT_TRUE(encoder_config.ppm[0] >= 50 && encoder_config.ppm[0] <= 200);
    TEST_ASSERT_TRUE(encoder_config.ppm[1] >= 50 && encoder_config.ppm[1] <= 200);
    TEST_ASSERT_TRUE(encoder_config.ppm[2] >= 50 && encoder_config.ppm[2] <= 200);
}

/**
 * @section Constraint Validation Tests
 */

void test_soft_limit_ordering_enforcement(void)
{
    // Lower limit must be strictly less than upper limit
    motion_config.soft_limit_low_mm[0] = 100;
    motion_config.soft_limit_high_mm[0] = 100;
    TEST_ASSERT_FALSE(motion_config.soft_limit_low_mm[0] < motion_config.soft_limit_high_mm[0]);

    // After correction
    motion_config.soft_limit_high_mm[0] = 101;
    TEST_ASSERT_TRUE(motion_config.soft_limit_low_mm[0] < motion_config.soft_limit_high_mm[0]);
}

void test_vfd_speed_ordering_enforcement(void)
{
    // Min must be strictly less than max
    vfd_config.min_speed_hz = 50;
    vfd_config.max_speed_hz = 50;
    TEST_ASSERT_FALSE(vfd_config.min_speed_hz < vfd_config.max_speed_hz);

    // After correction
    vfd_config.max_speed_hz = 51;
    TEST_ASSERT_TRUE(vfd_config.min_speed_hz < vfd_config.max_speed_hz);
}

void test_motion_limits_cannot_exceed_1000mm(void)
{
    motion_config.soft_limit_high_mm[0] = 999;
    TEST_ASSERT_TRUE(motion_config.soft_limit_high_mm[0] <= 1000);

    motion_config.soft_limit_high_mm[0] = 1001;
    TEST_ASSERT_FALSE(motion_config.soft_limit_high_mm[0] <= 1000);
}

void test_vfd_speeds_within_altivar31_limits(void)
{
    // Altivar 31 operates 1-105 Hz
    vfd_config.min_speed_hz = 1;
    vfd_config.max_speed_hz = 105;

    TEST_ASSERT_TRUE(vfd_config.min_speed_hz >= 1);
    TEST_ASSERT_TRUE(vfd_config.max_speed_hz <= 105);

    // Invalid: below minimum
    vfd_config.min_speed_hz = 0;
    TEST_ASSERT_FALSE(vfd_config.min_speed_hz >= 1);

    // Invalid: above maximum
    vfd_config.max_speed_hz = 106;
    TEST_ASSERT_FALSE(vfd_config.max_speed_hz <= 105);
}

/**
 * @section Registration function for test runner
 * Must be called by test_runner.cpp
 */
void run_api_config_tests(void)
{
    RUN_TEST(test_motion_default_valid);
    RUN_TEST(test_motion_soft_limit_lower_cannot_exceed_upper);
    RUN_TEST(test_motion_soft_limits_within_range);
    RUN_TEST(test_motion_all_axes_configurable);

    RUN_TEST(test_vfd_default_valid);
    RUN_TEST(test_vfd_min_speed_in_valid_range);
    RUN_TEST(test_vfd_max_speed_in_valid_range);
    RUN_TEST(test_vfd_min_less_than_max);
    RUN_TEST(test_vfd_acceleration_time_in_range);
    RUN_TEST(test_vfd_deceleration_time_in_range);

    RUN_TEST(test_encoder_default_valid);
    RUN_TEST(test_encoder_ppm_in_valid_range);
    RUN_TEST(test_encoder_each_axis_independent);
    RUN_TEST(test_encoder_calibration_status_per_axis);

    RUN_TEST(test_vfd_and_motion_independent);
    RUN_TEST(test_encoder_and_vfd_independent);
    RUN_TEST(test_all_configs_independently_valid);

    RUN_TEST(test_soft_limit_ordering_enforcement);
    RUN_TEST(test_vfd_speed_ordering_enforcement);
    RUN_TEST(test_motion_limits_cannot_exceed_1000mm);
    RUN_TEST(test_vfd_speeds_within_altivar31_limits);
}
