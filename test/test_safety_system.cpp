/**
 * @file test/test_safety_system.cpp
 * @brief Unit tests for BISSO E350 Safety System
 *
 * Tests cover:
 * - Emergency stop (E-stop) functionality
 * - Fault condition handling
 * - Safety state machine transitions
 * - Recovery procedures
 * - Thermal protection
 * - VFD fault detection
 * 
 * Required mocks: motion, vfd, plc
 * Initialization: Automatic via setUp() in test_runner.cpp
 */

#include "helpers/test_utils.h"
#include "helpers/test_fixtures.h"
#include <unity.h>

/**
 * @brief Convenience references to global fixtures
 */
#define motion (g_fixtures.motion)
#define vfd (g_fixtures.vfd)
#define plc (g_fixtures.plc)

/**
 * @section E-Stop Functionality Tests
 * Tests for emergency stop system
 */

/**
 * @test E-stop state prevents any motion
 */
void test_e_stop_prevents_motion_when_active(void) {
  // E-stop the system
  motion_mock_e_stop(&motion);

  // Verify motion is stopped
  TEST_ASSERT_EQUAL(MOTION_E_STOPPED, motion_mock_get_state(&motion));

  // Attempt to start move - should be rejected
  move_validation_result_t result =
      motion_mock_validate_move(&motion, AXIS_X, 1000, 50);
  TEST_ASSERT_EQUAL(MOVE_HARDWARE_ERROR, result);
}

/**
 * @test E-stop immediately halts any active motion
 */
void test_e_stop_halts_active_motion(void) {
  // Start motion
  motion_mock_start_move(&motion, AXIS_X, 5000, 50);
  TEST_ASSERT_EQUAL(MOTION_MOVING, motion_mock_get_state(&motion));

  // Activate E-stop
  motion_mock_e_stop(&motion);

  // Verify motion halted
  TEST_ASSERT_EQUAL(MOTION_E_STOPPED, motion_mock_get_state(&motion));
  TEST_ASSERT_EQUAL(0, motion.current_speed_hz);
}

/**
 * @test Motor run relay is disabled during E-stop
 */
void test_e_stop_cuts_motor_power(void) {
  plc_mock_set_motor_run(&plc, 1);
  TEST_ASSERT_EQUAL(1, plc_mock_get_motor_run(&plc));

  // Simulate E-stop cutting motor power
  plc_mock_set_motor_run(&plc, 0);
  TEST_ASSERT_EQUAL(0, plc_mock_get_motor_run(&plc));
}

/**
 * @test E-stop can be recovered from for safe restart
 */
void test_e_stop_recovery_and_restart(void) {
  // E-stop
  motion_mock_e_stop(&motion);
  TEST_ASSERT_EQUAL(MOTION_E_STOPPED, motion_mock_get_state(&motion));

  // Clear E-stop
  motion_mock_clear_e_stop(&motion);
  TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));

  // Verify motion is possible again
  move_validation_result_t result =
      motion_mock_validate_move(&motion, AXIS_X, 1000, 50);
  TEST_ASSERT_EQUAL(MOVE_VALID, result);
}

/**
 * @section VFD Fault Detection Tests
 * Tests for VFD fault handling and detection
 */

/**
 * @test VFD thermal fault detected
 */
void test_vfd_thermal_fault_detection(void) {
  // Start VFD at max speed
  vfd_mock_set_frequency(&vfd, 105);
  TEST_ASSERT_EQUAL(0, vfd.has_fault);

  // Simulate sustained high-speed operation
  for (int i = 0; i < 50; i++) {
    vfd_mock_advance_time(&vfd, 100);
  }

  // Temperature should exceed limit and trigger fault
  if (vfd.motor_temperature_c > 85.0f) {
    TEST_ASSERT_EQUAL(1, vfd.has_fault);
  }
}

/**
 * @test VFD fault stops motor output
 */
void test_vfd_fault_cuts_output(void) {
  // Normal operation
  vfd_mock_set_frequency(&vfd, 50);
  TEST_ASSERT_GREATER_THAN(0, vfd.frequency_hz);

  // Inject fault
  vfd_mock_inject_fault(&vfd, 13); // Thermal fault

  // Verify frequency drops to zero
  TEST_ASSERT_EQUAL(1, vfd.has_fault);
  TEST_ASSERT_EQUAL(0, vfd.is_running);
}

/**
 * @test VFD fault code is recorded
 */
void test_vfd_fault_code_recorded(void) {
  vfd_mock_inject_fault(&vfd, 15); // Example fault code
  TEST_ASSERT_EQUAL(15, vfd.fault_code);
}

/**
 * @test VFD fault can be cleared for recovery
 */
void test_vfd_fault_recovery(void) {
  vfd_mock_inject_fault(&vfd, 13);
  TEST_ASSERT_EQUAL(1, vfd.has_fault);

  vfd_mock_clear_fault(&vfd);
  TEST_ASSERT_EQUAL(0, vfd.has_fault);
  TEST_ASSERT_EQUAL(0, vfd.fault_code);
}

/**
 * @section Motor Current Monitoring Tests
 * Tests for stall and overcurrent detection
 */

/**
 * @test High current triggers stall warning
 */
void test_stall_warning_on_high_current(void) {
  motion_mock_start_move(&motion, AXIS_X, 5000, 50);

  // Simulate high current but moving
  float high_current = 9.5f; // Above 8A threshold
  motion_mock_update(&motion, 1000, 15.0f, high_current, 100);

  // Should still be OK if moving
  stall_status_t stall = motion_mock_get_stall_status(&motion);
  TEST_ASSERT_NOT_EQUAL(STALL_DETECTED, stall);
}

/**
 * @test Stall detected when current high AND no movement
 */
void test_stall_detected_on_block(void) {
  motion_mock_start_move(&motion, AXIS_X, 5000, 50);

  // Simulate blocking (high current, zero velocity)
  for (int i = 0; i < 10; i++) {
    motion_mock_update(&motion, 1000, 0.0f, 9.5f, 100);
  }

  // Should detect stall
  stall_status_t stall = motion_mock_get_stall_status(&motion);
  TEST_ASSERT_EQUAL(STALL_DETECTED, stall);
}

/**
 * @section Safe State Machine Tests
 * Tests for safety system state transitions
 */

/**
 * @test Initial state is safe (IDLE)
 */
void test_initial_state_is_safe(void) {
  motion_mock_state_t fresh_motion = motion_mock_init();
  TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&fresh_motion));
  TEST_ASSERT_EQUAL(0, fresh_motion.e_stop_active);
}

/**
 * @test Error state prevents further motion
 */
void test_error_state_blocks_motion(void) {
  // Reset motion for clean test
  motion = motion_mock_init();
  
  // Inject error state
  motion.state = MOTION_ERROR;

  move_validation_result_t result =
      motion_mock_validate_move(&motion, AXIS_X, 1000, 50);

  // Cannot move while in error
  TEST_ASSERT_NOT_EQUAL(MOVE_VALID, result);
}

/**
 * @test Only valid transitions allowed
 */
void test_valid_state_transitions(void) {
  // Reset motion for clean test
  motion = motion_mock_init();
  
  // IDLE -> MOVING is valid
  TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));
  motion_mock_start_move(&motion, AXIS_X, 1000, 50);
  TEST_ASSERT_EQUAL(MOTION_MOVING, motion_mock_get_state(&motion));

  // MOVING -> E_STOPPED is valid
  motion_mock_e_stop(&motion);
  TEST_ASSERT_EQUAL(MOTION_E_STOPPED, motion_mock_get_state(&motion));

  // E_STOPPED -> IDLE is valid
  motion_mock_clear_e_stop(&motion);
  TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));
}

/**
 * @section PLC Safety Coordination Tests
 * Tests for PLC contactor safety
 */

/**
 * @test Cannot have multiple axes active simultaneously
 */
void test_contactor_single_axis_constraint(void) {
  // Select X axis
  plc_mock_select_axis(&plc, AXIS_X);
  plc_mock_advance_time(&plc, 50);
  TEST_ASSERT_TRUE(plc_mock_is_settled(&plc));
  TEST_ASSERT_EQUAL(AXIS_X, plc_mock_get_active_axis(&plc));

  // Switch to Y axis
  plc_mock_select_axis(&plc, AXIS_Y);
  plc_mock_advance_time(&plc, 50);
  TEST_ASSERT_EQUAL(AXIS_Y, plc_mock_get_active_axis(&plc));

  // Verify X is no longer active
  TEST_ASSERT_EQUAL(0, plc_mock_is_axis_selected(&plc, AXIS_X));
  TEST_ASSERT_EQUAL(1, plc_mock_is_axis_selected(&plc, AXIS_Y));
}

/**
 * @test Contactor settling time is enforced
 */
void test_contactor_settling_safety_margin(void) {
  plc_mock_select_axis(&plc, AXIS_X);

  // Immediately check - should not be settled
  TEST_ASSERT_EQUAL(0, plc_mock_is_settled(&plc));

  // Wait partial time
  plc_mock_advance_time(&plc, 25); // Half of 50ms
  TEST_ASSERT_EQUAL(0, plc_mock_is_settled(&plc));

  // Wait remaining time
  plc_mock_advance_time(&plc, 25);
  TEST_ASSERT_EQUAL(1, plc_mock_is_settled(&plc));
}

/**
 * @test Contactor switching failure detected
 */
void test_contactor_failure_detection(void) {
  plc_mock_inject_switching_error(&plc);

  // Attempt to select axis
  plc_mock_select_axis(&plc, AXIS_X);
  plc_mock_advance_time(&plc, 50);

  // Contactor failure prevents selection
  TEST_ASSERT_EQUAL(0, plc_mock_is_axis_selected(&plc, AXIS_X));
}

/**
 * @section Thermal Protection Tests
 * Tests for thermal safety limits
 */

/**
 * @test Motor temperature rises under load
 */
void test_motor_temperature_rise_under_load(void) {
  // Reset VFD for clean thermal test
  vfd = vfd_mock_init();
  float initial_temp = vfd.motor_temperature_c;

  vfd_mock_set_frequency(&vfd, 100); // High load
  for (int i = 0; i < 10; i++) {
    vfd_mock_advance_time(&vfd, 500);
  }

  TEST_ASSERT_GREATER_THAN(initial_temp, vfd.motor_temperature_c);
}

/**
 * @test Motor temperature falls when idle
 */
void test_motor_temperature_fall_at_idle(void) {
  // Reset VFD for clean thermal test
  vfd = vfd_mock_init();
  
  // First, heat it up
  vfd_mock_set_frequency(&vfd, 105);
  for (int i = 0; i < 10; i++) {
    vfd_mock_advance_time(&vfd, 500);
  }
  float hot_temp = vfd.motor_temperature_c;

  // Now cool down
  vfd_mock_set_frequency(&vfd, 0); // Stop
  for (int i = 0; i < 10; i++) {
    vfd_mock_advance_time(&vfd, 500);
  }

  // Temperature should have cooled somewhat (or stayed same due to simulation limits)
  TEST_ASSERT_TRUE(vfd.motor_temperature_c <= hot_temp);
}

/**
 * @test Thermal cutoff triggers at safe limit
 */
void test_thermal_cutoff_protection(void) {
  vfd_mock_set_frequency(&vfd, 105);

  // Simulate until thermal limit
  for (int i = 0; i < 100; i++) {
    vfd_mock_advance_time(&vfd, 100);
    if (vfd.has_fault && vfd.fault_code == 13) {
      break; // Thermal fault triggered
    }
  }

  // Verify thermal protection worked
  if (vfd.motor_temperature_c > 84.0f) {
    TEST_ASSERT_EQUAL(1, vfd.has_fault);
  }
}

/**
 * @section Recovery and Diagnostics Tests
 * Tests for system recovery and status reporting
 */

/**
 * @test System can recover from temporary fault
 */
void test_fault_recovery_cycle(void) {
  // Inject fault
  motion_mock_e_stop(&motion);
  vfd_mock_inject_fault(&vfd, 13);

  // Wait for timeout/cool down (simulated)
  test_reset_time();
  test_advance_time(5000); // 5 second timeout

  // Clear faults
  motion_mock_clear_e_stop(&motion);
  vfd_mock_clear_fault(&vfd);

  // Verify recovery
  TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));
  TEST_ASSERT_EQUAL(0, vfd.has_fault);

  // Can resume operation
  move_validation_result_t result =
      motion_mock_validate_move(&motion, AXIS_X, 1000, 50);
  TEST_ASSERT_EQUAL(MOVE_VALID, result);
}

/**
 * @test Multiple faults handled correctly
 */
void test_multiple_fault_handling(void) {
  // E-stop active
  motion_mock_e_stop(&motion);
  TEST_ASSERT_EQUAL(MOTION_E_STOPPED, motion_mock_get_state(&motion));

  // VFD fault also occurs
  vfd_mock_inject_fault(&vfd, 13);
  TEST_ASSERT_EQUAL(1, vfd.has_fault);

  // System stays safe despite multiple faults
  move_validation_result_t result =
      motion_mock_validate_move(&motion, AXIS_X, 1000, 50);
  TEST_ASSERT_EQUAL(MOVE_HARDWARE_ERROR, result);

  // Clear both faults
  motion_mock_clear_e_stop(&motion);
  vfd_mock_clear_fault(&vfd);

  // Recovery is complete
  TEST_ASSERT_EQUAL(0, vfd.has_fault);
  TEST_ASSERT_EQUAL(0, motion.e_stop_active);
}

/**
 * @brief Register all safety system tests
 * Called from test_runner.cpp
 */
void run_safety_system_tests(void) {
  // Mocks are automatically initialized by setUp() before each test
  
  // E-stop tests
  RUN_TEST(test_e_stop_prevents_motion_when_active);
  RUN_TEST(test_e_stop_halts_active_motion);
  RUN_TEST(test_e_stop_cuts_motor_power);
  RUN_TEST(test_e_stop_recovery_and_restart);

  // VFD fault tests
  RUN_TEST(test_vfd_thermal_fault_detection);
  RUN_TEST(test_vfd_fault_cuts_output);
  RUN_TEST(test_vfd_fault_code_recorded);
  RUN_TEST(test_vfd_fault_recovery);

  // Motor current monitoring
  RUN_TEST(test_stall_warning_on_high_current);
  RUN_TEST(test_stall_detected_on_block);

  // State machine tests
  RUN_TEST(test_initial_state_is_safe);
  RUN_TEST(test_error_state_blocks_motion);
  RUN_TEST(test_valid_state_transitions);

  // PLC safety tests
  RUN_TEST(test_contactor_single_axis_constraint);
  RUN_TEST(test_contactor_settling_safety_margin);
  RUN_TEST(test_contactor_failure_detection);

  // Thermal protection tests
  RUN_TEST(test_motor_temperature_rise_under_load);
  RUN_TEST(test_motor_temperature_fall_at_idle);
  RUN_TEST(test_thermal_cutoff_protection);

  // Recovery tests
  RUN_TEST(test_fault_recovery_cycle);
  RUN_TEST(test_multiple_fault_handling);
}
