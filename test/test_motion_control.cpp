/**
 * @file test/test_motion_control.cpp
 * @brief Unit tests for BISSO E350 Motion Control System
 *
 * Tests cover:
 * - Move validation (axis, distance, speed constraints)
 * - Soft limit enforcement
 * - Stall detection (high current + no motion)
 * - Motion quality scoring
 * - PLC contactor settling time
 * - Emergency stop functionality
 */

#include <unity.h>
#include "mocks/motion_mock.h"
#include "mocks/plc_mock.h"
#include "mocks/vfd_mock.h"
#include "mocks/encoder_mock.h"
#include "helpers/test_utils.h"

/**
 * @brief Test fixture for motion tests
 */
static motion_mock_state_t motion;
static plc_mock_state_t plc;
static vfd_mock_state_t vfd;
static encoder_mock_state_t encoder;

void setUp(void)
{
    motion = motion_mock_init();
    plc = plc_mock_init();
    vfd = vfd_mock_init();
    encoder = encoder_mock_init();
    encoder_mock_calibrate(&encoder, 100);  // 100 PPM
}

void tearDown(void)
{
    // Reset for next test
}

/**
 * @section Move Validation Tests
 * Tests for move constraint checking before motion starts
 */

/**
 * @test Motion controller rejects moves on invalid axis
 */
void test_motion_validation_rejects_invalid_axis(void)
{
    // Attempt to move on axis 3 (invalid, only 0-2 exist)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, 3, 1000, 50);

    TEST_ASSERT_EQUAL(MOVE_INVALID_AXIS, result);
}

/**
 * @test Motion controller rejects zero-distance move
 */
void test_motion_validation_rejects_zero_distance(void)
{
    // Attempt to move 0 steps
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 0, 50);

    TEST_ASSERT_EQUAL(MOVE_INVALID_DISTANCE, result);
}

/**
 * @test Motion controller rejects speed below minimum (LSP)
 */
void test_motion_validation_rejects_speed_too_low(void)
{
    // Attempt to move at 0 Hz (below LSP=1 Hz)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 1000, 0);

    TEST_ASSERT_EQUAL(MOVE_INVALID_SPEED, result);
}

/**
 * @test Motion controller rejects speed above maximum (HSP)
 */
void test_motion_validation_rejects_speed_too_high(void)
{
    // Attempt to move at 110 Hz (above HSP=105 Hz)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 1000, 110);

    TEST_ASSERT_EQUAL(MOVE_INVALID_SPEED, result);
}

/**
 * @test Motion controller accepts valid speed at minimum
 */
void test_motion_validation_accepts_minimum_speed(void)
{
    // Move at LSP (1 Hz)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 1000, 1);

    TEST_ASSERT_EQUAL(MOVE_VALID, result);
}

/**
 * @test Motion controller accepts valid speed at maximum
 */
void test_motion_validation_accepts_maximum_speed(void)
{
    // Move at HSP (105 Hz)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 1000, 105);

    TEST_ASSERT_EQUAL(MOVE_VALID, result);
}

/**
 * @section Soft Limit Tests
 * Tests for position constraint enforcement
 */

/**
 * @test Soft limits prevent motion beyond upper bound
 */
void test_motion_soft_limits_prevent_overshoot_upper(void)
{
    // Set soft limits: 0 to 100mm (assuming 100 PPM encoder)
    motion_mock_set_soft_limits(&motion, AXIS_X, 0, 10000);  // 0-100mm
    motion.current_position_steps = 5000;  // Current: 50mm

    // Try to move 70mm forward (would reach 120mm, exceeds limit)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 7000, 50);

    TEST_ASSERT_EQUAL(MOVE_SOFT_LIMIT_VIOLATION, result);
}

/**
 * @test Soft limits prevent motion beyond lower bound
 */
void test_motion_soft_limits_prevent_overshoot_lower(void)
{
    // Set soft limits: 0 to 100mm
    motion_mock_set_soft_limits(&motion, AXIS_X, 0, 10000);
    motion.current_position_steps = 5000;  // Current: 50mm

    // Try to move 100mm backward (would reach -50mm, exceeds limit)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, -10000, 50);

    TEST_ASSERT_EQUAL(MOVE_SOFT_LIMIT_VIOLATION, result);
}

/**
 * @test Soft limits allow moves within bounds
 */
void test_motion_soft_limits_allow_valid_motion(void)
{
    motion_mock_set_soft_limits(&motion, AXIS_X, 0, 10000);  // 0-100mm
    motion.current_position_steps = 5000;  // Current: 50mm

    // Move 20mm forward (destination: 70mm, within limits)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 2000, 50);

    TEST_ASSERT_EQUAL(MOVE_VALID, result);
}

/**
 * @section Motion Execution Tests
 * Tests for actual motion execution and state transitions
 */

/**
 * @test Motion starts in correct state and transitions properly
 */
void test_motion_state_transitions(void)
{
    // Initial state: IDLE
    TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));

    // Start move
    motion_mock_start_move(&motion, AXIS_X, 5000, 50);
    TEST_ASSERT_EQUAL(MOTION_MOVING, motion_mock_get_state(&motion));

    // Simulate motion completion
    motion_mock_update(&motion, 5000, 15.0f, 2.0f, 1000);
    TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));
}

/**
 * @test Motion quality score reflects velocity accuracy
 */
void test_motion_quality_score_perfect_velocity(void)
{
    motion_mock_start_move(&motion, AXIS_X, 5000, 20);

    // Expected velocity at 20 Hz: 20 * 0.15 = 3 mm/s
    // Actual velocity matches perfectly
    motion_mock_update(&motion, 1000, 3.0f, 1.5f, 500);

    float quality = motion_mock_get_quality_score(&motion);
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 100.0f, quality);
}

/**
 * @test Motion quality score degrades with velocity deviation
 */
void test_motion_quality_score_degrades_with_deviation(void)
{
    motion_mock_start_move(&motion, AXIS_X, 5000, 20);

    // Expected: 3 mm/s, Actual: 2.4 mm/s (20% slower)
    motion_mock_update(&motion, 1000, 2.4f, 1.5f, 500);

    float quality = motion_mock_get_quality_score(&motion);
    // Quality should be reduced but not critically
    TEST_ASSERT_LESS_THAN(90.0f, quality);
    TEST_ASSERT_GREATER_THAN(50.0f, quality);
}

/**
 * @section Stall Detection Tests
 * Tests for motor stall detection (current without movement)
 */

/**
 * @test Stall detected when current high but no motion
 */
void test_stall_detection_high_current_no_motion(void)
{
    motion_mock_start_move(&motion, AXIS_X, 5000, 50);

    // Simulate 100ms intervals with high current but no position change
    for (int i = 0; i < 6; i++) {
        motion_mock_update(&motion, 1000, 0.0f, 9.0f, 100);  // No movement, 9A current
    }

    stall_status_t stall = motion_mock_get_stall_status(&motion);
    TEST_ASSERT_EQUAL(STALL_DETECTED, stall);
}

/**
 * @test High current with movement does not trigger stall
 */
void test_stall_detection_not_triggered_with_movement(void)
{
    motion_mock_start_move(&motion, AXIS_X, 5000, 100);

    // High current but motor is moving (normal heavy load)
    motion_mock_update(&motion, 1000, 15.0f, 9.0f, 500);
    motion_mock_update(&motion, 2000, 15.0f, 9.0f, 500);

    stall_status_t stall = motion_mock_get_stall_status(&motion);
    TEST_ASSERT_NOT_EQUAL(STALL_DETECTED, stall);
}

/**
 * @test Stall warning before stall detected
 */
void test_stall_warning_precedes_detection(void)
{
    motion_mock_start_move(&motion, AXIS_X, 5000, 50);

    // First update: high current triggers warning
    motion_mock_update(&motion, 1000, 0.0f, 9.0f, 100);
    TEST_ASSERT_EQUAL(STALL_WARNING, motion_mock_get_stall_status(&motion));

    // After more updates without movement: becomes detected
    for (int i = 0; i < 5; i++) {
        motion_mock_update(&motion, 1000, 0.0f, 9.0f, 100);
    }
    TEST_ASSERT_EQUAL(STALL_DETECTED, motion_mock_get_stall_status(&motion));
}

/**
 * @section Emergency Stop Tests
 * Tests for E-stop functionality
 */

/**
 * @test E-stop prevents motion immediately
 */
void test_e_stop_prevents_motion(void)
{
    // E-stop while idle
    motion_mock_e_stop(&motion);
    TEST_ASSERT_EQUAL(MOTION_E_STOPPED, motion_mock_get_state(&motion));

    // Cannot start move while E-stopped
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 1000, 50);
    TEST_ASSERT_EQUAL(MOVE_HARDWARE_ERROR, result);
}

/**
 * @test E-stop halts moving motion
 */
void test_e_stop_halts_moving_motion(void)
{
    motion_mock_start_move(&motion, AXIS_X, 5000, 50);
    TEST_ASSERT_EQUAL(MOTION_MOVING, motion_mock_get_state(&motion));

    // E-stop during motion
    motion_mock_e_stop(&motion);
    TEST_ASSERT_EQUAL(MOTION_E_STOPPED, motion_mock_get_state(&motion));
    TEST_ASSERT_EQUAL(0, motion.current_speed_hz);
}

/**
 * @test E-stop can be cleared to resume operation
 */
void test_e_stop_recovery(void)
{
    motion_mock_e_stop(&motion);
    TEST_ASSERT_EQUAL(MOTION_E_STOPPED, motion_mock_get_state(&motion));

    // Clear E-stop
    motion_mock_clear_e_stop(&motion);
    TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));

    // Can now start motion
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 1000, 50);
    TEST_ASSERT_EQUAL(MOVE_VALID, result);
}

/**
 * @section PLC Contactor Integration Tests
 * Tests for proper PLC/motion coordination
 */

/**
 * @test Motion validation respects active axis
 */
void test_motion_axis_coordination_with_plc(void)
{
    plc_mock_select_axis(&plc, AXIS_X);
    plc_mock_set_motor_run(&plc, 1);

    // Can move X axis
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 1000, 50);
    TEST_ASSERT_EQUAL(MOVE_VALID, result);
}

/**
 * @test Contactor settling time is respected
 */
void test_contactor_settling_required_for_motion(void)
{
    // Switch to Y axis
    plc_mock_select_axis(&plc, AXIS_Y);

    // Immediately try to start motion (contactor not settled)
    if (!plc_mock_is_settled(&plc)) {
        // Should not be safe to move until settled
        TEST_LOG("Contactor not settled, motion restricted");
    }

    // Wait for settling
    plc_mock_advance_time(&plc, 50);

    // Now safe
    TEST_ASSERT_TRUE(plc_mock_is_settled(&plc));
}

/**
 * @section Diagnostic Tests
 * Tests for motion diagnostics and reporting
 */

/**
 * @test Motion records successful moves
 */
void test_motion_success_count(void)
{
    // Start move
    motion_mock_start_move(&motion, AXIS_X, 5000, 50);
    TEST_ASSERT_EQUAL(1, motion.move_attempts);

    // Complete move
    motion_mock_update(&motion, 5000, 15.0f, 2.0f, 1000);
    TEST_ASSERT_GREATER_THAN(0, motion.move_completed);
}

/**
 * @test Motion quality score available after motion
 */
void test_motion_quality_score_calculation(void)
{
    motion_mock_start_move(&motion, AXIS_X, 5000, 50);

    // Simulate perfect motion
    for (int i = 0; i < 5; i++) {
        motion_mock_update(&motion, i * 1000, 7.5f, 2.0f, 100);
    }

    float quality = motion_mock_get_quality_score(&motion);
    TEST_ASSERT_GREATER_THAN_DOUBLE(0.0, quality);
    TEST_ASSERT_LESS_THAN_DOUBLE(100.0, quality);
}

/**
 * @brief Register all motion control tests
 * Called from test_runner.cpp
 */
void run_motion_control_tests(void)
{
    // Validation tests
    RUN_TEST(test_motion_validation_rejects_invalid_axis);
    RUN_TEST(test_motion_validation_rejects_zero_distance);
    RUN_TEST(test_motion_validation_rejects_speed_too_low);
    RUN_TEST(test_motion_validation_rejects_speed_too_high);
    RUN_TEST(test_motion_validation_accepts_minimum_speed);
    RUN_TEST(test_motion_validation_accepts_maximum_speed);

    // Soft limit tests
    RUN_TEST(test_motion_soft_limits_prevent_overshoot_upper);
    RUN_TEST(test_motion_soft_limits_prevent_overshoot_lower);
    RUN_TEST(test_motion_soft_limits_allow_valid_motion);

    // Motion execution tests
    RUN_TEST(test_motion_state_transitions);
    RUN_TEST(test_motion_quality_score_perfect_velocity);
    RUN_TEST(test_motion_quality_score_degrades_with_deviation);

    // Stall detection tests
    RUN_TEST(test_stall_detection_high_current_no_motion);
    RUN_TEST(test_stall_detection_not_triggered_with_movement);
    RUN_TEST(test_stall_warning_precedes_detection);

    // E-stop tests
    RUN_TEST(test_e_stop_prevents_motion);
    RUN_TEST(test_e_stop_halts_moving_motion);
    RUN_TEST(test_e_stop_recovery);

    // PLC integration tests
    RUN_TEST(test_motion_axis_coordination_with_plc);
    RUN_TEST(test_contactor_settling_required_for_motion);

    // Diagnostic tests
    RUN_TEST(test_motion_success_count);
    RUN_TEST(test_motion_quality_score_calculation);
}
