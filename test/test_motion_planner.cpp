/**
 * @file test/test_motion_planner.cpp
 * @brief Unit tests for motion planning calculations
 *
 * Tests cover:
 * - Distance calculations
 * - Speed profile mapping
 * - Deceleration calculations
 * - Position limit checking
 * - Move duration estimation
 */

#include <unity.h>
#include <cstdint>
#include <cmath>

// ============================================================================
// MOTION PLANNER DEFINITIONS
// ============================================================================

// Speed profiles
#define SPEED_PROFILE_SLOW   0
#define SPEED_PROFILE_MEDIUM 1
#define SPEED_PROFILE_FAST   2

// Speed limits (Hz)
#define LSP_HZ 1    // Low speed
#define HSP_HZ 105  // High speed

// Motion parameters
#define PULSES_PER_MM 100.0f
#define DECEL_DISTANCE_MM 5.0f

// Calculate distance in mm from pulses
static float pulsesToMm(int32_t pulses) {
    return (float)pulses / PULSES_PER_MM;
}

// Calculate pulses from mm
static int32_t mmToPulses(float mm) {
    return (int32_t)(mm * PULSES_PER_MM);
}

// Map speed (mm/s) to profile
static uint8_t mapSpeedToProfile(float speed_mm_s) {
    if (speed_mm_s < 3.0f) return SPEED_PROFILE_SLOW;
    if (speed_mm_s < 8.0f) return SPEED_PROFILE_MEDIUM;
    return SPEED_PROFILE_FAST;
}

// Calculate move distance
static float calculateMoveDistance(float start_mm, float end_mm) {
    return fabsf(end_mm - start_mm);
}

// Check if position is within limits
static bool isWithinLimits(float pos_mm, float min_mm, float max_mm) {
    return pos_mm >= min_mm && pos_mm <= max_mm;
}

// Estimate move duration (simplified)
static uint32_t estimateMoveDuration(float distance_mm, float speed_mm_s) {
    if (speed_mm_s <= 0) return 0;
    return (uint32_t)((distance_mm / speed_mm_s) * 1000.0f);  // ms
}

// Calculate deceleration start position
static float calculateDecelStart(float target_mm, float direction) {
    return target_mm - (direction * DECEL_DISTANCE_MM);
}

// ============================================================================
// DISTANCE CALCULATION TESTS
// ============================================================================

// @test Positive distance calculation
void test_distance_positive(void) {
    float distance = calculateMoveDistance(10.0f, 50.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, distance);
}

// @test Negative direction distance is absolute
void test_distance_negative_direction(void) {
    float distance = calculateMoveDistance(50.0f, 10.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, distance);
}

// @test Zero distance
void test_distance_zero(void) {
    float distance = calculateMoveDistance(25.0f, 25.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, distance);
}

// ============================================================================
// UNIT CONVERSION TESTS
// ============================================================================

// @test Pulses to mm conversion
void test_pulses_to_mm(void) {
    float mm = pulsesToMm(1000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, mm);
}

// @test Mm to pulses conversion
void test_mm_to_pulses(void) {
    int32_t pulses = mmToPulses(10.0f);
    TEST_ASSERT_EQUAL_INT32(1000, pulses);
}

// @test Round trip conversion
void test_roundtrip_conversion(void) {
    float original = 25.5f;
    int32_t pulses = mmToPulses(original);
    float result = pulsesToMm(pulses);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, original, result);
}

// @test Negative pulses
void test_negative_pulses(void) {
    float mm = pulsesToMm(-500);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, mm);
}

// ============================================================================
// SPEED PROFILE MAPPING TESTS
// ============================================================================

// @test Slow speed maps to SLOW profile
void test_speed_mapping_slow(void) {
    uint8_t profile = mapSpeedToProfile(1.0f);
    TEST_ASSERT_EQUAL_UINT8(SPEED_PROFILE_SLOW, profile);
}

// @test Medium speed maps to MEDIUM profile
void test_speed_mapping_medium(void) {
    uint8_t profile = mapSpeedToProfile(5.0f);
    TEST_ASSERT_EQUAL_UINT8(SPEED_PROFILE_MEDIUM, profile);
}

// @test Fast speed maps to FAST profile
void test_speed_mapping_fast(void) {
    uint8_t profile = mapSpeedToProfile(10.0f);
    TEST_ASSERT_EQUAL_UINT8(SPEED_PROFILE_FAST, profile);
}

// @test Boundary: 3.0 mm/s is MEDIUM
void test_speed_boundary_medium(void) {
    uint8_t profile = mapSpeedToProfile(3.0f);
    TEST_ASSERT_EQUAL_UINT8(SPEED_PROFILE_MEDIUM, profile);
}

// @test Boundary: 8.0 mm/s is FAST
void test_speed_boundary_fast(void) {
    uint8_t profile = mapSpeedToProfile(8.0f);
    TEST_ASSERT_EQUAL_UINT8(SPEED_PROFILE_FAST, profile);
}

// ============================================================================
// POSITION LIMIT TESTS
// ============================================================================

// @test Position within limits
void test_position_within_limits(void) {
    bool valid = isWithinLimits(50.0f, 0.0f, 100.0f);
    TEST_ASSERT_TRUE(valid);
}

// @test Position at minimum limit
void test_position_at_min_limit(void) {
    bool valid = isWithinLimits(0.0f, 0.0f, 100.0f);
    TEST_ASSERT_TRUE(valid);
}

// @test Position at maximum limit
void test_position_at_max_limit(void) {
    bool valid = isWithinLimits(100.0f, 0.0f, 100.0f);
    TEST_ASSERT_TRUE(valid);
}

// @test Position below minimum
void test_position_below_min(void) {
    bool valid = isWithinLimits(-0.1f, 0.0f, 100.0f);
    TEST_ASSERT_FALSE(valid);
}

// @test Position above maximum
void test_position_above_max(void) {
    bool valid = isWithinLimits(100.1f, 0.0f, 100.0f);
    TEST_ASSERT_FALSE(valid);
}

// ============================================================================
// DURATION ESTIMATION TESTS
// ============================================================================

// @test Duration for simple move
void test_duration_simple_move(void) {
    // 100mm at 10mm/s = 10 seconds = 10000ms
    uint32_t duration = estimateMoveDuration(100.0f, 10.0f);
    TEST_ASSERT_EQUAL_UINT32(10000, duration);
}

// @test Duration for short move
void test_duration_short_move(void) {
    // 5mm at 5mm/s = 1 second = 1000ms
    uint32_t duration = estimateMoveDuration(5.0f, 5.0f);
    TEST_ASSERT_EQUAL_UINT32(1000, duration);
}

// @test Zero speed returns zero
void test_duration_zero_speed(void) {
    uint32_t duration = estimateMoveDuration(100.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT32(0, duration);
}

// ============================================================================
// DECELERATION TESTS
// ============================================================================

// @test Decel start for positive move
void test_decel_start_positive(void) {
    float decel_pos = calculateDecelStart(100.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 95.0f, decel_pos);  // 100 - 5
}

// @test Decel start for negative move
void test_decel_start_negative(void) {
    float decel_pos = calculateDecelStart(0.0f, -1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, decel_pos);  // 0 - (-5)
}

// @test Decel distance constant
void test_decel_distance_constant(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, DECEL_DISTANCE_MM);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_motion_planner_tests(void) {
    // Distance calculations
    RUN_TEST(test_distance_positive);
    RUN_TEST(test_distance_negative_direction);
    RUN_TEST(test_distance_zero);
    
    // Unit conversions
    RUN_TEST(test_pulses_to_mm);
    RUN_TEST(test_mm_to_pulses);
    RUN_TEST(test_roundtrip_conversion);
    RUN_TEST(test_negative_pulses);
    
    // Speed mapping
    RUN_TEST(test_speed_mapping_slow);
    RUN_TEST(test_speed_mapping_medium);
    RUN_TEST(test_speed_mapping_fast);
    RUN_TEST(test_speed_boundary_medium);
    RUN_TEST(test_speed_boundary_fast);
    
    // Position limits
    RUN_TEST(test_position_within_limits);
    RUN_TEST(test_position_at_min_limit);
    RUN_TEST(test_position_at_max_limit);
    RUN_TEST(test_position_below_min);
    RUN_TEST(test_position_above_max);
    
    // Duration estimation
    RUN_TEST(test_duration_simple_move);
    RUN_TEST(test_duration_short_move);
    RUN_TEST(test_duration_zero_speed);
    
    // Deceleration
    RUN_TEST(test_decel_start_positive);
    RUN_TEST(test_decel_start_negative);
    RUN_TEST(test_decel_distance_constant);
}
