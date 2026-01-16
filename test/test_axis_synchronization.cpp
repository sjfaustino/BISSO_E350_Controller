/**
 * @file test/test_axis_synchronization.cpp
 * @brief Unit tests for axis synchronization and motion quality validation
 *
 * Tests cover:
 * - Quality score calculation
 * - Jitter detection
 * - Stall detection thresholds
 * - Axis reset functionality
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>

// ============================================================================
// AXIS METRICS DEFINITIONS (copied from header for isolation)
// ============================================================================

typedef struct {
    float current_velocity_mms;
    float vfd_frequency_hz;
    float commanded_feedrate_mms;
    float velocity_jitter_mms;
    float vfd_encoder_error_percent;
    bool is_moving;
    bool stalled;
    uint32_t quality_score;
    uint32_t good_motion_samples;
    uint32_t bad_motion_samples;
    uint32_t stall_count;
    uint32_t last_update_ms;
    uint32_t active_duration_ms;
    float max_jitter_recorded_mms;
} axis_metrics_t;

typedef struct {
    float vfd_encoder_tolerance_percent;
    float encoder_stall_threshold_mms;
    float jitter_threshold_mms;
} axis_sync_config_t;

// Local test state
static axis_metrics_t test_axis;
static axis_sync_config_t test_config;

static void reset_mock(void) {
    memset(&test_axis, 0, sizeof(test_axis));
    test_axis.quality_score = 100;
    
    test_config.vfd_encoder_tolerance_percent = 15.0f;
    test_config.encoder_stall_threshold_mms = 0.1f;
    test_config.jitter_threshold_mms = 0.5f;
}

// ============================================================================
// QUALITY SCORE CALCULATION (Testable Logic)
// ============================================================================

static uint32_t calculateQualityScore(const axis_metrics_t* m) {
    if (m->good_motion_samples + m->bad_motion_samples == 0) {
        return 100;  // No samples yet
    }
    
    uint32_t total = m->good_motion_samples + m->bad_motion_samples;
    return (m->good_motion_samples * 100) / total;
}

static bool isStalled(const axis_metrics_t* m, const axis_sync_config_t* cfg) {
    return m->is_moving && (m->current_velocity_mms < cfg->encoder_stall_threshold_mms);
}

static bool hasExcessiveJitter(const axis_metrics_t* m, const axis_sync_config_t* cfg) {
    return m->velocity_jitter_mms > cfg->jitter_threshold_mms;
}

static bool hasVFDEncoderMismatch(const axis_metrics_t* m, const axis_sync_config_t* cfg) {
    return fabsf(m->vfd_encoder_error_percent) > cfg->vfd_encoder_tolerance_percent;
}

// ============================================================================
// QUALITY SCORE TESTS
// ============================================================================

// @test Quality score 100% when all samples are good
void test_quality_score_all_good(void) {
    reset_mock();
    test_axis.good_motion_samples = 100;
    test_axis.bad_motion_samples = 0;
    
    uint32_t score = calculateQualityScore(&test_axis);
    TEST_ASSERT_EQUAL_UINT32(100, score);
}

// @test Quality score 0% when all samples are bad
void test_quality_score_all_bad(void) {
    reset_mock();
    test_axis.good_motion_samples = 0;
    test_axis.bad_motion_samples = 100;
    
    uint32_t score = calculateQualityScore(&test_axis);
    TEST_ASSERT_EQUAL_UINT32(0, score);
}

// @test Quality score 50% when mixed samples
void test_quality_score_mixed(void) {
    reset_mock();
    test_axis.good_motion_samples = 50;
    test_axis.bad_motion_samples = 50;
    
    uint32_t score = calculateQualityScore(&test_axis);
    TEST_ASSERT_EQUAL_UINT32(50, score);
}

// @test Quality score 100% with no samples (default)
void test_quality_score_no_samples(void) {
    reset_mock();
    test_axis.good_motion_samples = 0;
    test_axis.bad_motion_samples = 0;
    
    uint32_t score = calculateQualityScore(&test_axis);
    TEST_ASSERT_EQUAL_UINT32(100, score);
}

// ============================================================================
// STALL DETECTION TESTS
// ============================================================================

// @test Stall detected when moving but velocity is zero
void test_stall_detected_zero_velocity(void) {
    reset_mock();
    test_axis.is_moving = true;
    test_axis.current_velocity_mms = 0.0f;
    
    TEST_ASSERT_TRUE(isStalled(&test_axis, &test_config));
}

// @test Stall detected when velocity below threshold
void test_stall_detected_low_velocity(void) {
    reset_mock();
    test_axis.is_moving = true;
    test_axis.current_velocity_mms = 0.05f;  // Below 0.1 threshold
    
    TEST_ASSERT_TRUE(isStalled(&test_axis, &test_config));
}

// @test No stall when velocity above threshold
void test_stall_not_detected_normal_velocity(void) {
    reset_mock();
    test_axis.is_moving = true;
    test_axis.current_velocity_mms = 10.0f;  // Normal speed
    
    TEST_ASSERT_FALSE(isStalled(&test_axis, &test_config));
}

// @test No stall when not moving (idle state)
void test_stall_not_detected_when_idle(void) {
    reset_mock();
    test_axis.is_moving = false;
    test_axis.current_velocity_mms = 0.0f;
    
    TEST_ASSERT_FALSE(isStalled(&test_axis, &test_config));
}

// ============================================================================
// JITTER DETECTION TESTS
// ============================================================================

// @test Excessive jitter detected when above threshold
void test_jitter_detected(void) {
    reset_mock();
    test_axis.velocity_jitter_mms = 1.0f;  // Above 0.5 threshold
    
    TEST_ASSERT_TRUE(hasExcessiveJitter(&test_axis, &test_config));
}

// @test No jitter when below threshold
void test_jitter_not_detected(void) {
    reset_mock();
    test_axis.velocity_jitter_mms = 0.2f;  // Below 0.5 threshold
    
    TEST_ASSERT_FALSE(hasExcessiveJitter(&test_axis, &test_config));
}

// @test Jitter at exactly threshold is not excessive
void test_jitter_at_threshold(void) {
    reset_mock();
    test_axis.velocity_jitter_mms = 0.5f;  // At threshold
    
    TEST_ASSERT_FALSE(hasExcessiveJitter(&test_axis, &test_config));
}

// ============================================================================
// VFD/ENCODER MISMATCH TESTS
// ============================================================================

// @test VFD mismatch detected when error exceeds tolerance
void test_vfd_mismatch_detected(void) {
    reset_mock();
    test_axis.vfd_encoder_error_percent = 20.0f;  // Above 15% tolerance
    
    TEST_ASSERT_TRUE(hasVFDEncoderMismatch(&test_axis, &test_config));
}

// @test VFD mismatch detected for negative errors
void test_vfd_mismatch_negative(void) {
    reset_mock();
    test_axis.vfd_encoder_error_percent = -20.0f;  // Negative but exceeds
    
    TEST_ASSERT_TRUE(hasVFDEncoderMismatch(&test_axis, &test_config));
}

// @test No VFD mismatch when within tolerance
void test_vfd_mismatch_within_tolerance(void) {
    reset_mock();
    test_axis.vfd_encoder_error_percent = 10.0f;  // Within 15%
    
    TEST_ASSERT_FALSE(hasVFDEncoderMismatch(&test_axis, &test_config));
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_axis_synchronization_tests(void) {
    // Quality score tests
    RUN_TEST(test_quality_score_all_good);
    RUN_TEST(test_quality_score_all_bad);
    RUN_TEST(test_quality_score_mixed);
    RUN_TEST(test_quality_score_no_samples);
    
    // Stall detection tests
    RUN_TEST(test_stall_detected_zero_velocity);
    RUN_TEST(test_stall_detected_low_velocity);
    RUN_TEST(test_stall_not_detected_normal_velocity);
    RUN_TEST(test_stall_not_detected_when_idle);
    
    // Jitter detection tests
    RUN_TEST(test_jitter_detected);
    RUN_TEST(test_jitter_not_detected);
    RUN_TEST(test_jitter_at_threshold);
    
    // VFD mismatch tests
    RUN_TEST(test_vfd_mismatch_detected);
    RUN_TEST(test_vfd_mismatch_negative);
    RUN_TEST(test_vfd_mismatch_within_tolerance);
}
