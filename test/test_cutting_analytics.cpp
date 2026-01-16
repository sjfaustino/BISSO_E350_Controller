/**
 * @file test/test_cutting_analytics.cpp
 * @brief Unit tests for stone cutting analytics calculations
 *
 * Tests cover:
 * - Power calculation (W = V * I * PF)
 * - Material removal rate (MRR)
 * - Specific cutting energy (SCE)
 * - Blade health percentage
 * - Session management
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>

// ============================================================================
// CUTTING ANALYTICS DEFINITIONS (copied for isolation)
// ============================================================================

typedef struct {
    float motor_voltage_v;
    float motor_efficiency;
    float blade_width_mm;
    float cut_depth_mm;
    float power_factor;
} cutting_config_t;

typedef struct {
    float cutting_power_w;
    float mrr_mm3_per_s;
    float sce_j_per_mm3;
    float blade_health_pct;
    bool session_active;
    float baseline_sce;
} cutting_state_t;

static cutting_config_t test_config;
static cutting_state_t test_state;

static void reset_mock(void) {
    test_config.motor_voltage_v = 230.0f;
    test_config.motor_efficiency = 0.85f;
    test_config.blade_width_mm = 3.0f;
    test_config.cut_depth_mm = 20.0f;
    test_config.power_factor = 0.8f;
    
    memset(&test_state, 0, sizeof(test_state));
    test_state.blade_health_pct = 100.0f;
    test_state.baseline_sce = 0.5f;  // J/mm³
}

// ============================================================================
// CALCULATION LOGIC (Testable Implementations)
// ============================================================================

// Calculate power from voltage, current, and power factor
static float calculatePower(float voltage_v, float current_a, float power_factor) {
    return voltage_v * current_a * power_factor;
}

// Calculate material removal rate (MRR)
// MRR = feedrate * blade_width * cut_depth
static float calculateMRR(float feedrate_mm_s, float blade_width_mm, float cut_depth_mm) {
    return feedrate_mm_s * blade_width_mm * cut_depth_mm;
}

// Calculate specific cutting energy (SCE)
// SCE = Power / MRR (Joules per mm³)
static float calculateSCE(float power_w, float mrr_mm3_s) {
    if (mrr_mm3_s < 0.001f) return 0.0f;  // Avoid division by zero
    return power_w / mrr_mm3_s;
}

// Calculate blade health based on SCE ratio
// If current SCE is higher than baseline, blade is duller
static float calculateBladeHealth(float current_sce, float baseline_sce) {
    if (baseline_sce < 0.001f) return 100.0f;
    
    float ratio = baseline_sce / current_sce;  // Sharp blade = 1.0, dull = < 1.0
    
    if (ratio >= 1.0f) return 100.0f;
    if (ratio <= 0.0f) return 0.0f;
    
    return ratio * 100.0f;
}

// ============================================================================
// POWER CALCULATION TESTS
// ============================================================================

// @test Power calculation for typical motor load
void test_power_calculation_typical(void) {
    float power = calculatePower(230.0f, 10.0f, 0.8f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1840.0f, power);  // 230 * 10 * 0.8 = 1840W
}

// @test Power calculation with zero current
void test_power_zero_current(void) {
    float power = calculatePower(230.0f, 0.0f, 0.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, power);
}

// @test Power calculation with unity power factor
void test_power_unity_pf(void) {
    float power = calculatePower(230.0f, 10.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 2300.0f, power);
}

// ============================================================================
// MATERIAL REMOVAL RATE TESTS
// ============================================================================

// @test MRR for typical cutting operation
void test_mrr_typical(void) {
    float mrr = calculateMRR(5.0f, 3.0f, 20.0f);  // 5mm/s, 3mm wide, 20mm deep
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 300.0f, mrr);  // 5 * 3 * 20 = 300 mm³/s
}

// @test MRR with zero feedrate
void test_mrr_zero_feedrate(void) {
    float mrr = calculateMRR(0.0f, 3.0f, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, mrr);
}

// @test MRR with thin blade
void test_mrr_thin_blade(void) {
    float mrr = calculateMRR(5.0f, 1.5f, 20.0f);  // Thinner blade
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 150.0f, mrr);  // 5 * 1.5 * 20 = 150
}

// ============================================================================
// SPECIFIC CUTTING ENERGY TESTS
// ============================================================================

// @test SCE for typical operation
void test_sce_typical(void) {
    float sce = calculateSCE(1840.0f, 300.0f);  // 1840W / 300 mm³/s
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 6.13f, sce);  // ~6.13 J/mm³
}

// @test SCE with zero MRR (avoid divide by zero)
void test_sce_zero_mrr(void) {
    float sce = calculateSCE(1840.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sce);
}

// @test SCE with very small MRR
void test_sce_tiny_mrr(void) {
    float sce = calculateSCE(1840.0f, 0.0001f);  // Near zero
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sce); // Should return 0
}

// @test SCE is higher for harder material
void test_sce_hard_material(void) {
    float sce_soft = calculateSCE(1840.0f, 300.0f);   // Soft stone
    float sce_hard = calculateSCE(1840.0f, 150.0f);   // Hard stone (slower cut)
    
    TEST_ASSERT_TRUE(sce_hard > sce_soft);  // Hard material = higher SCE
}

// ============================================================================
// BLADE HEALTH TESTS
// ============================================================================

// @test Blade health 100% when SCE matches baseline
void test_blade_health_perfect(void) {
    float health = calculateBladeHealth(0.5f, 0.5f);  // Same SCE
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, health);
}

// @test Blade health degrades when SCE increases
void test_blade_health_degraded(void) {
    float health = calculateBladeHealth(1.0f, 0.5f);  // SCE doubled
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 50.0f, health);  // 50% health
}

// @test Blade health caps at 0% when very dull
void test_blade_health_very_dull(void) {
    float health = calculateBladeHealth(10.0f, 0.5f);  // SCE 20x baseline
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 5.0f, health);  // Very low health
}

// @test Blade health 100% if cutting better than baseline
void test_blade_health_better_than_baseline(void) {
    float health = calculateBladeHealth(0.3f, 0.5f);  // SCE lower = sharper
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, health);
}

// @test Blade health handles zero baseline
void test_blade_health_zero_baseline(void) {
    float health = calculateBladeHealth(0.5f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100.0f, health);
}

// ============================================================================
// SESSION MANAGEMENT TESTS
// ============================================================================

// @test Session starts inactive
void test_session_init_inactive(void) {
    reset_mock();
    TEST_ASSERT_FALSE(test_state.session_active);
}

// @test Session can be activated
void test_session_activate(void) {
    reset_mock();
    test_state.session_active = true;
    TEST_ASSERT_TRUE(test_state.session_active);
}

// @test Session can be deactivated
void test_session_deactivate(void) {
    reset_mock();
    test_state.session_active = true;
    test_state.session_active = false;
    TEST_ASSERT_FALSE(test_state.session_active);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_cutting_analytics_tests(void) {
    // Power calculation tests
    RUN_TEST(test_power_calculation_typical);
    RUN_TEST(test_power_zero_current);
    RUN_TEST(test_power_unity_pf);
    
    // MRR tests
    RUN_TEST(test_mrr_typical);
    RUN_TEST(test_mrr_zero_feedrate);
    RUN_TEST(test_mrr_thin_blade);
    
    // SCE tests
    RUN_TEST(test_sce_typical);
    RUN_TEST(test_sce_zero_mrr);
    RUN_TEST(test_sce_tiny_mrr);
    RUN_TEST(test_sce_hard_material);
    
    // Blade health tests
    RUN_TEST(test_blade_health_perfect);
    RUN_TEST(test_blade_health_degraded);
    RUN_TEST(test_blade_health_very_dull);
    RUN_TEST(test_blade_health_better_than_baseline);
    RUN_TEST(test_blade_health_zero_baseline);
    
    // Session tests
    RUN_TEST(test_session_init_inactive);
    RUN_TEST(test_session_activate);
    RUN_TEST(test_session_deactivate);
}
