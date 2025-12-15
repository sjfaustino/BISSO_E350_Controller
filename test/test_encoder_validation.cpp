/**
 * @file test/test_encoder_validation.cpp
 * @brief Unit tests for BISSO E350 Encoder Validation System
 *
 * Tests cover:
 * - Encoder calibration (PPM - pulses per millimeter)
 * - Position tracking accuracy
 * - Velocity measurement
 * - Jitter detection (bearing wear indicator)
 * - Communication error handling
 * - Deviation from target velocity
 */

#include <unity.h>
#include "mocks/encoder_mock.h"
#include "mocks/vfd_mock.h"
#include "helpers/test_utils.h"
#include <cmath>

/**
 * @brief Test fixtures for encoder tests
 */
static encoder_mock_state_t encoder;
static vfd_mock_state_t vfd;

static void setUp(void)
{
    encoder = encoder_mock_init();
    vfd = vfd_mock_init();
}

static void tearDown(void)
{
    // Reset for next test
}

/**
 * @section Calibration Tests
 * Tests for encoder PPM (pulses per millimeter) calibration
 */

/**
 * @test Encoder begins uncalibrated
 */
void test_encoder_initial_uncalibrated_state(void)
{
    encoder_mock_state_t fresh = encoder_mock_init();
    TEST_ASSERT_EQUAL(0, encoder_mock_is_calibrated(&fresh));
}

/**
 * @test Encoder accepts calibration
 */
void test_encoder_calibration_sets_ppm(void)
{
    encoder_mock_calibrate(&encoder, 100);
    TEST_ASSERT_EQUAL(1, encoder_mock_is_calibrated(&encoder));
    TEST_ASSERT_EQUAL(100, encoder.ppm);
}

/**
 * @test Different calibration values accepted
 */
void test_encoder_accepts_various_calibration_values(void)
{
    // Common WJ66 encoding: 100 PPM
    encoder_mock_calibrate(&encoder, 100);
    TEST_ASSERT_EQUAL(100, encoder.ppm);

    // Alternative encoding: 50 PPM (coarser)
    encoder_mock_calibrate(&encoder, 50);
    TEST_ASSERT_EQUAL(50, encoder.ppm);

    // Alternative: 200 PPM (finer)
    encoder_mock_calibrate(&encoder, 200);
    TEST_ASSERT_EQUAL(200, encoder.ppm);
}

/**
 * @test Uncalibrated encoder doesn't track position
 */
void test_uncalibrated_encoder_no_position_tracking(void)
{
    // Don't calibrate
    encoder_mock_set_target_velocity(&encoder, 15.0f);
    encoder_mock_advance_time(&encoder, 1000);

    // Position should remain zero
    TEST_ASSERT_EQUAL(0, encoder_mock_get_position_pulses(&encoder));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, encoder_mock_get_position_mm(&encoder));
}

/**
 * @section Position Tracking Tests
 * Tests for accurate position measurement
 */

/**
 * @test Position tracking after calibration
 */
void test_position_tracking_after_calibration(void)
{
    encoder_mock_calibrate(&encoder, 100);  // 100 PPM

    // Move at 15 mm/s for 1 second
    encoder_mock_set_target_velocity(&encoder, 15.0f);
    encoder_mock_advance_time(&encoder, 1000);

    // Should have moved 15mm = 1500 pulses (100 PPM)
    TEST_ASSERT_EQUAL(1500, encoder_mock_get_position_pulses(&encoder));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.0f, encoder_mock_get_position_mm(&encoder));
}

/**
 * @test Position accumulates correctly over multiple steps
 */
void test_position_accumulation_multipart(void)
{
    encoder_mock_calibrate(&encoder, 100);

    // First move: 10mm at 10 mm/s
    encoder_mock_set_target_velocity(&encoder, 10.0f);
    encoder_mock_advance_time(&encoder, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 10.0f, encoder_mock_get_position_mm(&encoder));

    // Second move: another 5mm
    encoder_mock_set_target_velocity(&encoder, 5.0f);
    encoder_mock_advance_time(&encoder, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 15.0f, encoder_mock_get_position_mm(&encoder));
}

/**
 * @test Negative movement tracked correctly
 */
void test_position_tracking_backward_motion(void)
{
    encoder_mock_calibrate(&encoder, 100);

    // Move forward 20mm
    encoder_mock_set_target_velocity(&encoder, 20.0f);
    encoder_mock_advance_time(&encoder, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 20.0f, encoder_mock_get_position_mm(&encoder));

    // Move backward 10mm
    encoder_mock_set_target_velocity(&encoder, -10.0f);
    encoder_mock_advance_time(&encoder, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 10.0f, encoder_mock_get_position_mm(&encoder));
}

/**
 * @test Position reset function
 */
void test_position_reset_clears_tracking(void)
{
    encoder_mock_calibrate(&encoder, 100);

    encoder_mock_set_target_velocity(&encoder, 15.0f);
    encoder_mock_advance_time(&encoder, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 15.0f, encoder_mock_get_position_mm(&encoder));

    // Reset position
    encoder_mock_reset_position(&encoder);
    TEST_ASSERT_EQUAL(0, encoder_mock_get_position_pulses(&encoder));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, encoder_mock_get_position_mm(&encoder));
}

/**
 * @section Velocity Measurement Tests
 * Tests for velocity calculation accuracy
 */

/**
 * @test Velocity matches set target when no interference
 */
void test_velocity_measurement_clean_signal(void)
{
    encoder_mock_calibrate(&encoder, 100);
    float target_velocity = 15.0f;  // 15 mm/s

    encoder_mock_set_target_velocity(&encoder, target_velocity);
    encoder_mock_advance_time(&encoder, 100);

    float measured = encoder_mock_get_velocity_mms(&encoder);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, target_velocity, measured);
}

/**
 * @test Velocity zero at rest
 */
void test_velocity_zero_when_stopped(void)
{
    encoder_mock_calibrate(&encoder, 100);

    // No velocity set (defaults to 0)
    float velocity = encoder_mock_get_velocity_mms(&encoder);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, velocity);
}

/**
 * @section Jitter Detection Tests
 * Tests for bearing wear monitoring via jitter
 */

/**
 * @test Healthy encoder has no jitter
 */
void test_jitter_absent_in_healthy_encoder(void)
{
    encoder_mock_calibrate(&encoder, 100);
    encoder_mock_set_target_velocity(&encoder, 15.0f);
    encoder_mock_advance_time(&encoder, 1000);

    float jitter = encoder_mock_get_jitter_amplitude(&encoder);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, jitter);
}

/**
 * @test Jitter can be injected to simulate wear
 */
void test_jitter_injection_and_measurement(void)
{
    encoder_mock_calibrate(&encoder, 100);

    // Inject jitter (simulating bearing wear)
    float wear_jitter = 0.5f;  // 0.5 mm/s jitter
    encoder_mock_inject_jitter(&encoder, wear_jitter);

    // Set velocity
    encoder_mock_set_target_velocity(&encoder, 15.0f);
    encoder_mock_advance_time(&encoder, 500);

    float measured_jitter = encoder_mock_get_jitter_amplitude(&encoder);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, wear_jitter, measured_jitter);
}

/**
 * @test Jitter detection levels indicate wear progression
 */
void test_jitter_wear_levels(void)
{
    encoder_mock_calibrate(&encoder, 100);
    encoder_mock_set_target_velocity(&encoder, 15.0f);

    // Healthy: < 0.5 mm/s jitter
    encoder_mock_inject_jitter(&encoder, 0.2f);
    encoder_mock_advance_time(&encoder, 100);
    TEST_ASSERT_LESS_THAN(0.5f, encoder_mock_get_jitter_amplitude(&encoder) + 0.2f);

    // Warning: 0.5-1.0 mm/s jitter
    encoder_mock_reset_position(&encoder);
    encoder_mock_inject_jitter(&encoder, 0.7f);
    encoder_mock_advance_time(&encoder, 100);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 0.7f, encoder_mock_get_jitter_amplitude(&encoder));

    // Critical: > 1.0 mm/s jitter
    encoder_mock_reset_position(&encoder);
    encoder_mock_inject_jitter(&encoder, 1.5f);
    encoder_mock_advance_time(&encoder, 100);
    TEST_ASSERT_GREATER_THAN(1.0f, encoder_mock_get_jitter_amplitude(&encoder));
}

/**
 * @section Communication Error Tests
 * Tests for encoder communication failure detection
 */

/**
 * @test Encoder starts with no communication errors
 */
void test_encoder_comms_healthy_initially(void)
{
    encoder_mock_calibrate(&encoder, 100);
    TEST_ASSERT_EQUAL(0, encoder_mock_has_error(&encoder));
}

/**
 * @test Communication error can be injected
 */
void test_encoder_comms_error_injection(void)
{
    encoder_mock_inject_comms_error(&encoder);
    TEST_ASSERT_EQUAL(1, encoder_mock_has_error(&encoder));
}

/**
 * @test Position tracking stops during communication error
 */
void test_position_tracking_halts_on_comms_error(void)
{
    encoder_mock_calibrate(&encoder, 100);
    encoder_mock_set_target_velocity(&encoder, 15.0f);
    encoder_mock_advance_time(&encoder, 500);

    float position_before = encoder_mock_get_position_mm(&encoder);

    // Inject communication error
    encoder_mock_inject_comms_error(&encoder);
    encoder_mock_advance_time(&encoder, 500);

    float position_after = encoder_mock_get_position_mm(&encoder);

    // Position shouldn't change during error
    TEST_ASSERT_EQUAL_FLOAT(position_before, position_after);
}

/**
 * @test Communication error can be cleared
 */
void test_encoder_comms_error_recovery(void)
{
    encoder_mock_inject_comms_error(&encoder);
    TEST_ASSERT_EQUAL(1, encoder_mock_has_error(&encoder));

    encoder_mock_clear_comms_error(&encoder);
    TEST_ASSERT_EQUAL(0, encoder_mock_has_error(&encoder));
}

/**
 * @test Encoder resumes tracking after communication recovery
 */
void test_position_tracking_resumes_after_comms_recovery(void)
{
    encoder_mock_calibrate(&encoder, 100);
    encoder_mock_set_target_velocity(&encoder, 15.0f);
    encoder_mock_advance_time(&encoder, 500);

    float position_before = encoder_mock_get_position_mm(&encoder);

    // Comms error and recovery
    encoder_mock_inject_comms_error(&encoder);
    encoder_mock_clear_comms_error(&encoder);

    // Continue moving
    encoder_mock_advance_time(&encoder, 500);
    float position_after = encoder_mock_get_position_mm(&encoder);

    // Position should have increased
    TEST_ASSERT_GREATER_THAN(position_before, position_after);
}

/**
 * @section Deviation Tracking Tests
 * Tests for velocity deviation from target (VFD vs actual speed mismatch)
 */

/**
 * @test Perfect velocity match has zero deviation
 */
void test_velocity_deviation_perfect_match(void)
{
    encoder_mock_calibrate(&encoder, 100);
    float target = 15.0f;

    encoder_mock_set_target_velocity(&encoder, target);
    encoder_mock_advance_time(&encoder, 500);

    float deviation = encoder_mock_get_deviation(&encoder);
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 0.0f, deviation);
}

/**
 * @test Deviation increases with speed mismatch
 */
void test_velocity_deviation_on_mismatch(void)
{
    encoder_mock_calibrate(&encoder, 100);
    float target = 15.0f;

    encoder_mock_set_target_velocity(&encoder, target);

    // Inject 50% deviation (actual is 50% of target)
    encoder_mock_inject_deviation(&encoder, 50.0f);
    encoder_mock_advance_time(&encoder, 500);

    float measured_deviation = encoder_mock_get_deviation(&encoder);
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 50.0f, measured_deviation);
}

/**
 * @test Motor load detected via deviation
 */
void test_load_detection_via_deviation(void)
{
    encoder_mock_calibrate(&encoder, 100);

    // Light load: 10% deviation
    encoder_mock_inject_deviation(&encoder, 10.0f);
    encoder_mock_set_target_velocity(&encoder, 20.0f);
    encoder_mock_advance_time(&encoder, 100);
    float light_load_dev = encoder_mock_get_deviation(&encoder);

    encoder_mock_reset_position(&encoder);

    // Heavy load: 30% deviation
    encoder_mock_inject_deviation(&encoder, 30.0f);
    encoder_mock_set_target_velocity(&encoder, 20.0f);
    encoder_mock_advance_time(&encoder, 100);
    float heavy_load_dev = encoder_mock_get_deviation(&encoder);

    TEST_ASSERT_LESS_THAN(heavy_load_dev, light_load_dev + 30.0f);
}

/**
 * @test Maximum deviation tracking
 */
void test_maximum_deviation_history(void)
{
    encoder_mock_calibrate(&encoder, 100);
    encoder_mock_set_target_velocity(&encoder, 20.0f);

    // First deviation: 10%
    encoder_mock_inject_deviation(&encoder, 10.0f);
    encoder_mock_advance_time(&encoder, 100);

    // Second deviation: 25%
    encoder_mock_inject_deviation(&encoder, 25.0f);
    encoder_mock_advance_time(&encoder, 100);

    // Max should be at least 25%
    float max_dev = encoder.max_deviation_seen;
    TEST_ASSERT_GREATER_THAN(20.0f, max_dev);
}

/**
 * @section Integration Tests
 * Tests combining multiple encoder features
 */

/**
 * @test Complete motion profile with encoder validation
 */
void test_complete_motion_with_encoder(void)
{
    encoder_mock_calibrate(&encoder, 100);

    // Accelerate
    encoder_mock_set_target_velocity(&encoder, 15.0f);
    for (int i = 0; i < 5; i++) {
        encoder_mock_advance_time(&encoder, 100);
    }
    float position_mid = encoder_mock_get_position_mm(&encoder);

    // Decelerate to stop
    encoder_mock_set_target_velocity(&encoder, 0.0f);
    encoder_mock_advance_time(&encoder, 500);

    float position_final = encoder_mock_get_position_mm(&encoder);
    TEST_ASSERT_GREATER_THAN(position_mid, position_final);
}

/**
 * @test Encoder health check (calibration + no comms errors)
 */
void test_encoder_health_check(void)
{
    encoder_mock_calibrate(&encoder, 100);

    // Verify health criteria
    TEST_ASSERT_EQUAL(1, encoder_mock_is_calibrated(&encoder));
    TEST_ASSERT_EQUAL(0, encoder_mock_has_error(&encoder));
    TEST_ASSERT_EQUAL(100, encoder.ppm);

    // Ready for motion
    TEST_LOG("Encoder healthy: calibrated=1, comms_ok=1, ppm=100");
}

/**
 * @brief Register all encoder validation tests
 * Called from test_runner.cpp
 */
void run_encoder_validation_tests(void)
{
    // Calibration tests
    RUN_TEST(test_encoder_initial_uncalibrated_state);
    RUN_TEST(test_encoder_calibration_sets_ppm);
    RUN_TEST(test_encoder_accepts_various_calibration_values);
    RUN_TEST(test_uncalibrated_encoder_no_position_tracking);

    // Position tracking tests
    RUN_TEST(test_position_tracking_after_calibration);
    RUN_TEST(test_position_accumulation_multipart);
    RUN_TEST(test_position_tracking_backward_motion);
    RUN_TEST(test_position_reset_clears_tracking);

    // Velocity measurement tests
    RUN_TEST(test_velocity_measurement_clean_signal);
    RUN_TEST(test_velocity_zero_when_stopped);

    // Jitter detection tests
    RUN_TEST(test_jitter_absent_in_healthy_encoder);
    RUN_TEST(test_jitter_injection_and_measurement);
    RUN_TEST(test_jitter_wear_levels);

    // Communication error tests
    RUN_TEST(test_encoder_comms_healthy_initially);
    RUN_TEST(test_encoder_comms_error_injection);
    RUN_TEST(test_position_tracking_halts_on_comms_error);
    RUN_TEST(test_encoder_comms_error_recovery);
    RUN_TEST(test_position_tracking_resumes_after_comms_recovery);

    // Deviation tracking tests
    RUN_TEST(test_velocity_deviation_perfect_match);
    RUN_TEST(test_velocity_deviation_on_mismatch);
    RUN_TEST(test_load_detection_via_deviation);
    RUN_TEST(test_maximum_deviation_history);

    // Integration tests
    RUN_TEST(test_complete_motion_with_encoder);
    RUN_TEST(test_encoder_health_check);
}
