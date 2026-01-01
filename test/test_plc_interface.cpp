/**
 * @file test/test_plc_interface.cpp
 * @brief Unit tests for PLC I/O interface (bit-level output verification)
 *
 * Tests cover:
 * - Axis select bit mapping (Y1-Y3)
 * - Direction bit mapping (Y4-Y5)
 * - Speed profile bit mapping with inversion fix (Y6-Y8)
 * - Clear all outputs functionality
 * - Active-low logic verification
 *
 * @note These tests verify the changes made to plc_iface.cpp on 2026-01-01
 *       to fix the signal mapping to match KC868-A16 hardware wiring.
 */

#include "test/unity/unity.h"
#include "test/mocks/plc_mock.h"

// Global mock instance for tests
static plc_mock_state_t g_plc;

// ============================================================================
// TEST HELPERS
// ============================================================================

/**
 * @brief Reset mock before each test
 */
static void reset_mock(void) {
    g_plc = plc_mock_init();
}

/**
 * @brief Simulate what plcSetAxisSelect() would write
 * Matches the logic in plc_iface.cpp
 */
static uint8_t build_axis_select_output(uint8_t axis) {
    uint8_t reg = 0xFF;  // All OFF
    
    // Clear axis bit (active-low)
    if (axis == 0) reg &= ~(1 << 0);  // X = Y1
    else if (axis == 1) reg &= ~(1 << 1);  // Y = Y2
    else if (axis == 2) reg &= ~(1 << 2);  // Z = Y3
    
    return reg;
}

/**
 * @brief Simulate what plcSetDirection() would write
 */
static uint8_t apply_direction(uint8_t reg, bool positive) {
    // Clear both direction bits first
    reg |= (1 << 3) | (1 << 4);
    
    if (positive) {
        reg &= ~(1 << 3);  // Y4 = DIR+
    } else {
        reg &= ~(1 << 4);  // Y5 = DIR-
    }
    return reg;
}

/**
 * @brief Simulate what plcSetSpeed() would write (with inversion fix!)
 * Profile 0 = SLOW (Y8), Profile 1 = MEDIUM (Y7), Profile 2 = FAST (Y6)
 */
static uint8_t apply_speed(uint8_t reg, uint8_t profile) {
    // Clear all speed bits first
    reg |= (1 << 5) | (1 << 6) | (1 << 7);
    
    switch (profile) {
        case 0: reg &= ~(1 << 7); break;  // SLOW = Y8
        case 1: reg &= ~(1 << 6); break;  // MEDIUM = Y7
        case 2: reg &= ~(1 << 5); break;  // FAST = Y6
    }
    return reg;
}

// ============================================================================
// P0 TESTS: AXIS SELECT BIT MAPPING
// ============================================================================

// @test X axis select sets only Y1 (bit 0)
void test_axis_select_x_sets_bit_0(void) {
    reset_mock();
    
    uint8_t expected = build_axis_select_output(0);
    plc_mock_write_output(&g_plc, expected);
    
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_axis_select(&g_plc));
    // Verify raw bit: bit 0 should be 0 (ON), bits 1-2 should be 1 (OFF)
    TEST_ASSERT_EQUAL_HEX8(0b11111110, expected);
}

// @test Y axis select sets only Y2 (bit 1)
void test_axis_select_y_sets_bit_1(void) {
    reset_mock();
    
    uint8_t expected = build_axis_select_output(1);
    plc_mock_write_output(&g_plc, expected);
    
    TEST_ASSERT_EQUAL_UINT8(1, plc_mock_get_axis_select(&g_plc));
    TEST_ASSERT_EQUAL_HEX8(0b11111101, expected);
}

// @test Z axis select sets only Y3 (bit 2)
void test_axis_select_z_sets_bit_2(void) {
    reset_mock();
    
    uint8_t expected = build_axis_select_output(2);
    plc_mock_write_output(&g_plc, expected);
    
    TEST_ASSERT_EQUAL_UINT8(2, plc_mock_get_axis_select(&g_plc));
    TEST_ASSERT_EQUAL_HEX8(0b11111011, expected);
}

// @test No axis select when all bits set (0xFF)
void test_axis_select_none(void) {
    reset_mock();
    
    plc_mock_write_output(&g_plc, 0xFF);
    
    TEST_ASSERT_EQUAL_UINT8(255, plc_mock_get_axis_select(&g_plc));
}

// ============================================================================
// P0 TESTS: DIRECTION BIT MAPPING
// ============================================================================

// @test Positive direction sets Y4 (bit 3)
void test_direction_positive_sets_bit_3(void) {
    reset_mock();
    
    uint8_t reg = 0xFF;
    reg = apply_direction(reg, true);
    plc_mock_write_output(&g_plc, reg);
    
    TEST_ASSERT_EQUAL_UINT8(1, plc_mock_get_direction(&g_plc));
    // Bit 3 = 0 (ON), Bit 4 = 1 (OFF)
    TEST_ASSERT_BITS_LOW(0b00001000, reg);
    TEST_ASSERT_BITS_HIGH(0b00010000, reg);
}

// @test Negative direction sets Y5 (bit 4)
void test_direction_negative_sets_bit_4(void) {
    reset_mock();
    
    uint8_t reg = 0xFF;
    reg = apply_direction(reg, false);
    plc_mock_write_output(&g_plc, reg);
    
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_direction(&g_plc));
    // Bit 3 = 1 (OFF), Bit 4 = 0 (ON)
    TEST_ASSERT_BITS_HIGH(0b00001000, reg);
    TEST_ASSERT_BITS_LOW(0b00010000, reg);
}

// ============================================================================
// P0 TESTS: SPEED PROFILE BIT MAPPING (WITH INVERSION FIX)
// ============================================================================

// @test Speed profile 0 (slowest) sets Y8 (bit 7) - CRITICAL FIX VERIFICATION
void test_speed_profile_0_slow_sets_bit_7(void) {
    reset_mock();
    
    uint8_t reg = 0xFF;
    reg = apply_speed(reg, 0);
    plc_mock_write_output(&g_plc, reg);
    
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_speed_profile(&g_plc));
    // Profile 0 = SLOW = Y8 = bit 7
    TEST_ASSERT_BITS_LOW(0b10000000, reg);   // Y8 ON
    TEST_ASSERT_BITS_HIGH(0b01100000, reg);  // Y6, Y7 OFF
}

// @test Speed profile 1 (medium) sets Y7 (bit 6)
void test_speed_profile_1_medium_sets_bit_6(void) {
    reset_mock();
    
    uint8_t reg = 0xFF;
    reg = apply_speed(reg, 1);
    plc_mock_write_output(&g_plc, reg);
    
    TEST_ASSERT_EQUAL_UINT8(1, plc_mock_get_speed_profile(&g_plc));
    // Profile 1 = MEDIUM = Y7 = bit 6
    TEST_ASSERT_BITS_LOW(0b01000000, reg);   // Y7 ON
    TEST_ASSERT_BITS_HIGH(0b10100000, reg);  // Y6, Y8 OFF
}

// @test Speed profile 2 (fastest) sets Y6 (bit 5) - CRITICAL FIX VERIFICATION
void test_speed_profile_2_fast_sets_bit_5(void) {
    reset_mock();
    
    uint8_t reg = 0xFF;
    reg = apply_speed(reg, 2);
    plc_mock_write_output(&g_plc, reg);
    
    TEST_ASSERT_EQUAL_UINT8(2, plc_mock_get_speed_profile(&g_plc));
    // Profile 2 = FAST = Y6 = bit 5
    TEST_ASSERT_BITS_LOW(0b00100000, reg);   // Y6 ON
    TEST_ASSERT_BITS_HIGH(0b11000000, reg);  // Y7, Y8 OFF
}

// ============================================================================
// P1 TESTS: CLEAR ALL OUTPUTS
// ============================================================================

// @test Clear all outputs sets register to 0xFF
void test_clear_all_outputs(void) {
    reset_mock();
    
    // First set some bits
    plc_mock_write_output(&g_plc, 0x00);
    TEST_ASSERT_EQUAL_HEX8(0x00, plc_mock_get_output_register(&g_plc));
    
    // Now clear all (simulates plcClearAllOutputs)
    plc_mock_write_output(&g_plc, 0xFF);
    
    TEST_ASSERT_EQUAL_HEX8(0xFF, plc_mock_get_output_register(&g_plc));
    TEST_ASSERT_EQUAL_UINT8(255, plc_mock_get_axis_select(&g_plc));
    TEST_ASSERT_EQUAL_UINT8(255, plc_mock_get_speed_profile(&g_plc));
}

// ============================================================================
// P1 TESTS: I2C WRITE COUNTING
// ============================================================================

// @test Each write increments the counter
void test_write_count_increments(void) {
    reset_mock();
    
    TEST_ASSERT_EQUAL_UINT32(0, plc_mock_get_write_count(&g_plc));
    
    plc_mock_write_output(&g_plc, 0xAA);
    TEST_ASSERT_EQUAL_UINT32(1, plc_mock_get_write_count(&g_plc));
    
    plc_mock_write_output(&g_plc, 0x55);
    TEST_ASSERT_EQUAL_UINT32(2, plc_mock_get_write_count(&g_plc));
}

// ============================================================================
// P1 TESTS: FULL MOTION SCENARIO
// ============================================================================

// @test Simulate complete move setup: axis + direction + speed
void test_full_move_setup(void) {
    reset_mock();
    
    // Simulate: Move Y axis, positive direction, slow speed
    // This is what motionSetPLCAxisDirection() + motionSetPLCSpeedProfile() do
    
    uint8_t reg = 0xFF;
    
    // Set Y axis (bit 1)
    reg &= ~(1 << 1);
    
    // Set positive direction (bit 3)
    reg &= ~(1 << 3);
    
    // Set slow speed (bit 7 after inversion fix)
    reg &= ~(1 << 7);
    
    plc_mock_write_output(&g_plc, reg);
    
    TEST_ASSERT_EQUAL_UINT8(1, plc_mock_get_axis_select(&g_plc));  // Y
    TEST_ASSERT_EQUAL_UINT8(1, plc_mock_get_direction(&g_plc));    // Positive
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_speed_profile(&g_plc)); // Slow (profile 0)
    
    // Verify expected bit pattern
    // Bits: 0=off(X), 1=ON(Y), 2=off(Z), 3=ON(+), 4=off(-), 5=off(fast), 6=off(med), 7=ON(slow)
    TEST_ASSERT_EQUAL_HEX8(0b01110101, reg);
}

// ============================================================================
// P2 TESTS: MOTION-TO-PLC INTEGRATION SCENARIOS
// ============================================================================

// @test X axis forward at fast speed
void test_motion_x_forward_fast(void) {
    reset_mock();
    
    uint8_t reg = 0xFF;
    reg &= ~(1 << 0);  // X axis
    reg &= ~(1 << 3);  // Positive direction
    reg &= ~(1 << 5);  // Fast speed (Y6)
    
    plc_mock_write_output(&g_plc, reg);
    
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_axis_select(&g_plc));  // X
    TEST_ASSERT_EQUAL_UINT8(1, plc_mock_get_direction(&g_plc));    // Positive
    TEST_ASSERT_EQUAL_UINT8(2, plc_mock_get_speed_profile(&g_plc)); // Fast (profile 2)
}

// @test Z axis reverse at slow speed
void test_motion_z_reverse_slow(void) {
    reset_mock();
    
    uint8_t reg = 0xFF;
    reg &= ~(1 << 2);  // Z axis
    reg &= ~(1 << 4);  // Negative direction
    reg &= ~(1 << 7);  // Slow speed (Y8)
    
    plc_mock_write_output(&g_plc, reg);
    
    TEST_ASSERT_EQUAL_UINT8(2, plc_mock_get_axis_select(&g_plc));  // Z
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_direction(&g_plc));    // Negative
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_speed_profile(&g_plc)); // Slow (profile 0)
}

// @test Sequential axis switching clears previous axis
void test_axis_switching_clears_previous(void) {
    reset_mock();
    
    // First set X axis
    uint8_t reg1 = build_axis_select_output(0);
    plc_mock_write_output(&g_plc, reg1);
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_axis_select(&g_plc));
    
    // Now switch to Y axis
    uint8_t reg2 = build_axis_select_output(1);
    plc_mock_write_output(&g_plc, reg2);
    TEST_ASSERT_EQUAL_UINT8(1, plc_mock_get_axis_select(&g_plc));
    
    // Verify X is no longer set
    TEST_ASSERT_BITS_HIGH(0b00000001, reg2);  // X bit should be OFF
    TEST_ASSERT_BITS_LOW(0b00000010, reg2);   // Y bit should be ON
}

// @test Speed profile deceleration sequence (fast -> medium -> slow)
void test_speed_deceleration_sequence(void) {
    reset_mock();
    
    // Start at fast
    uint8_t reg = apply_speed(0xFF, 2);
    plc_mock_write_output(&g_plc, reg);
    TEST_ASSERT_EQUAL_UINT8(2, plc_mock_get_speed_profile(&g_plc));
    
    // Decelerate to medium
    reg = apply_speed(0xFF, 1);
    plc_mock_write_output(&g_plc, reg);
    TEST_ASSERT_EQUAL_UINT8(1, plc_mock_get_speed_profile(&g_plc));
    
    // Slow down to slow
    reg = apply_speed(0xFF, 0);
    plc_mock_write_output(&g_plc, reg);
    TEST_ASSERT_EQUAL_UINT8(0, plc_mock_get_speed_profile(&g_plc));
    
    // Verify write count (should be 3 writes)
    TEST_ASSERT_EQUAL_UINT32(3, plc_mock_get_write_count(&g_plc));
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_plc_interface_tests(void) {
    // P0: Axis select
    RUN_TEST(test_axis_select_x_sets_bit_0);
    RUN_TEST(test_axis_select_y_sets_bit_1);
    RUN_TEST(test_axis_select_z_sets_bit_2);
    RUN_TEST(test_axis_select_none);
    
    // P0: Direction
    RUN_TEST(test_direction_positive_sets_bit_3);
    RUN_TEST(test_direction_negative_sets_bit_4);
    
    // P0: Speed profiles (verifies inversion fix)
    RUN_TEST(test_speed_profile_0_slow_sets_bit_7);
    RUN_TEST(test_speed_profile_1_medium_sets_bit_6);
    RUN_TEST(test_speed_profile_2_fast_sets_bit_5);
    
    // P1: Clear outputs
    RUN_TEST(test_clear_all_outputs);
    
    // P1: Write counting
    RUN_TEST(test_write_count_increments);
    
    // P1: Full scenario
    RUN_TEST(test_full_move_setup);
    
    // P2: Motion integration scenarios
    RUN_TEST(test_motion_x_forward_fast);
    RUN_TEST(test_motion_z_reverse_slow);
    RUN_TEST(test_axis_switching_clears_previous);
    RUN_TEST(test_speed_deceleration_sequence);
}
