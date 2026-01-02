/**
 * @file test/test_dashboard_metrics.cpp
 * @brief Unit tests for dashboard metrics calculations
 *
 * Tests cover:
 * - Uptime formatting
 * - Memory usage percentage
 * - CPU load calculation
 * - Cut count tracking
 * - Alarm status aggregation
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

// ============================================================================
// DASHBOARD METRICS DEFINITIONS
// ============================================================================

typedef struct {
    uint32_t uptime_ms;
    uint32_t free_heap;
    uint32_t total_heap;
    uint8_t cpu_load_percent;
    
    uint32_t cut_count_total;
    uint32_t cut_count_today;
    
    uint8_t active_alarms;
    uint8_t alarm_history_count;
    
    bool estop_active;
    bool motion_active;
    bool spindle_running;
} dashboard_metrics_t;

static dashboard_metrics_t g_metrics;

static void reset_mock(void) {
    memset(&g_metrics, 0, sizeof(g_metrics));
    g_metrics.total_heap = 327680;  // ESP32 typical
}

// Calculate memory percentage used
static uint8_t calculateMemoryUsed(uint32_t free_heap, uint32_t total_heap) {
    if (total_heap == 0) return 100;
    return 100 - (uint8_t)((free_heap * 100) / total_heap);
}

// Format uptime to string
static void formatUptime(uint32_t uptime_ms, char* buffer, size_t len) {
    uint32_t seconds = uptime_ms / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    
    if (days > 0) {
        snprintf(buffer, len, "%ud %02uh", days, hours % 24);
    } else if (hours > 0) {
        snprintf(buffer, len, "%uh %02um", hours, minutes % 60);
    } else {
        snprintf(buffer, len, "%um %02us", minutes, seconds % 60);
    }
}

// Check if system is healthy
static bool isSystemHealthy(const dashboard_metrics_t* m) {
    if (m->estop_active) return false;
    if (m->active_alarms > 0) return false;
    if (m->cpu_load_percent > 90) return false;
    if (calculateMemoryUsed(m->free_heap, m->total_heap) > 85) return false;
    return true;
}

// ============================================================================
// MEMORY CALCULATION TESTS
// ============================================================================

// @test Memory usage with 50% free
void test_memory_50_percent_used(void) {
    uint8_t used = calculateMemoryUsed(163840, 327680);  // 50% free
    TEST_ASSERT_EQUAL_UINT8(50, used);
}

// @test Memory usage with 75% free
void test_memory_25_percent_used(void) {
    uint8_t used = calculateMemoryUsed(245760, 327680);  // 75% free
    TEST_ASSERT_EQUAL_UINT8(25, used);
}

// @test Memory usage with 0 free (emergency)
void test_memory_100_percent_used(void) {
    uint8_t used = calculateMemoryUsed(0, 327680);
    TEST_ASSERT_EQUAL_UINT8(100, used);
}

// @test Memory usage with all free
void test_memory_0_percent_used(void) {
    uint8_t used = calculateMemoryUsed(327680, 327680);
    TEST_ASSERT_EQUAL_UINT8(0, used);
}

// @test Memory usage with zero total (edge case)
void test_memory_zero_total(void) {
    uint8_t used = calculateMemoryUsed(1000, 0);
    TEST_ASSERT_EQUAL_UINT8(100, used);  // Default to full
}

// ============================================================================
// UPTIME FORMATTING TESTS
// ============================================================================

// @test Uptime in seconds only
void test_uptime_seconds(void) {
    char buffer[20];
    formatUptime(45000, buffer, sizeof(buffer));  // 45 seconds
    TEST_ASSERT_NOT_NULL(strstr(buffer, "s"));
}

// @test Uptime in minutes
void test_uptime_minutes(void) {
    char buffer[20];
    formatUptime(5 * 60 * 1000 + 30000, buffer, sizeof(buffer));  // 5m 30s
    TEST_ASSERT_NOT_NULL(strstr(buffer, "m"));
}

// @test Uptime in hours
void test_uptime_hours(void) {
    char buffer[20];
    formatUptime(2 * 60 * 60 * 1000, buffer, sizeof(buffer));  // 2 hours
    TEST_ASSERT_NOT_NULL(strstr(buffer, "h"));
}

// @test Uptime in days
void test_uptime_days(void) {
    char buffer[20];
    formatUptime(48UL * 60 * 60 * 1000, buffer, sizeof(buffer));  // 2 days
    TEST_ASSERT_NOT_NULL(strstr(buffer, "d"));
}

// ============================================================================
// CUT COUNT TESTS
// ============================================================================

// @test Cut count initializes to zero
void test_cut_count_init(void) {
    reset_mock();
    TEST_ASSERT_EQUAL_UINT32(0, g_metrics.cut_count_total);
    TEST_ASSERT_EQUAL_UINT32(0, g_metrics.cut_count_today);
}

// @test Cut count increments
void test_cut_count_increment(void) {
    reset_mock();
    g_metrics.cut_count_total++;
    g_metrics.cut_count_today++;
    TEST_ASSERT_EQUAL_UINT32(1, g_metrics.cut_count_total);
}

// @test Daily count resets independently
void test_cut_count_daily_reset(void) {
    reset_mock();
    g_metrics.cut_count_total = 100;
    g_metrics.cut_count_today = 10;
    
    // Simulate daily reset
    g_metrics.cut_count_today = 0;
    
    TEST_ASSERT_EQUAL_UINT32(100, g_metrics.cut_count_total);
    TEST_ASSERT_EQUAL_UINT32(0, g_metrics.cut_count_today);
}

// ============================================================================
// SYSTEM HEALTH TESTS
// ============================================================================

// @test Healthy system with no issues
void test_system_healthy_normal(void) {
    reset_mock();
    g_metrics.free_heap = 200000;
    g_metrics.cpu_load_percent = 50;
    
    TEST_ASSERT_TRUE(isSystemHealthy(&g_metrics));
}

// @test Unhealthy with E-stop active
void test_system_unhealthy_estop(void) {
    reset_mock();
    g_metrics.estop_active = true;
    
    TEST_ASSERT_FALSE(isSystemHealthy(&g_metrics));
}

// @test Unhealthy with active alarms
void test_system_unhealthy_alarms(void) {
    reset_mock();
    g_metrics.active_alarms = 1;
    
    TEST_ASSERT_FALSE(isSystemHealthy(&g_metrics));
}

// @test Unhealthy with high CPU
void test_system_unhealthy_cpu(void) {
    reset_mock();
    g_metrics.cpu_load_percent = 95;
    g_metrics.free_heap = 200000;
    
    TEST_ASSERT_FALSE(isSystemHealthy(&g_metrics));
}

// @test Unhealthy with low memory
void test_system_unhealthy_memory(void) {
    reset_mock();
    g_metrics.free_heap = 40000;  // ~12% free = 88% used
    g_metrics.cpu_load_percent = 50;
    
    TEST_ASSERT_FALSE(isSystemHealthy(&g_metrics));
}

// ============================================================================
// STATE FLAG TESTS
// ============================================================================

// @test Motion active flag
void test_motion_active_flag(void) {
    reset_mock();
    g_metrics.motion_active = true;
    TEST_ASSERT_TRUE(g_metrics.motion_active);
}

// @test Spindle running flag
void test_spindle_running_flag(void) {
    reset_mock();
    g_metrics.spindle_running = true;
    TEST_ASSERT_TRUE(g_metrics.spindle_running);
}

// @test All flags can be true simultaneously
void test_multiple_flags(void) {
    reset_mock();
    g_metrics.motion_active = true;
    g_metrics.spindle_running = true;
    g_metrics.estop_active = false;
    
    TEST_ASSERT_TRUE(g_metrics.motion_active);
    TEST_ASSERT_TRUE(g_metrics.spindle_running);
    TEST_ASSERT_FALSE(g_metrics.estop_active);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_dashboard_metrics_tests(void) {
    // Memory calculations
    RUN_TEST(test_memory_50_percent_used);
    RUN_TEST(test_memory_25_percent_used);
    RUN_TEST(test_memory_100_percent_used);
    RUN_TEST(test_memory_0_percent_used);
    RUN_TEST(test_memory_zero_total);
    
    // Uptime formatting
    RUN_TEST(test_uptime_seconds);
    RUN_TEST(test_uptime_minutes);
    RUN_TEST(test_uptime_hours);
    RUN_TEST(test_uptime_days);
    
    // Cut counts
    RUN_TEST(test_cut_count_init);
    RUN_TEST(test_cut_count_increment);
    RUN_TEST(test_cut_count_daily_reset);
    
    // System health
    RUN_TEST(test_system_healthy_normal);
    RUN_TEST(test_system_unhealthy_estop);
    RUN_TEST(test_system_unhealthy_alarms);
    RUN_TEST(test_system_unhealthy_cpu);
    RUN_TEST(test_system_unhealthy_memory);
    
    // State flags
    RUN_TEST(test_motion_active_flag);
    RUN_TEST(test_spindle_running_flag);
    RUN_TEST(test_multiple_flags);
}
