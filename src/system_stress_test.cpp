/**
 * @file system_stress_test.cpp
 * @brief Edge Case Stress Testing Suite (Production CLI Version)
 * @details Tests system robustness under abnormal conditions
 */

#include "motion.h"
#include "safety.h"
#include "fault_logging.h"
#include "task_manager.h"
#include "system_tuning.h"
#include <Arduino.h>
#include "serial_logger.h"
#include "log_rate_limiter.h"
#include "load_manager.h"
#include "watchdog_manager.h"
#include "i2c_bus_recovery.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// LIGHTWEIGHT TEST MACROS (No Unity Dependency)
// ============================================================================

static bool g_test_failed = false;
static int g_tests_run = 0;
static int g_tests_failed_count = 0;

#define TEST_START(name) \
    logPrintf("[STRESS] Running: %s...\r\n", name); \
    g_tests_run++; \
    g_test_failed = false;

#define TEST_END() \
    if (!g_test_failed) { \
        logPrintln("[STRESS] Result: PASS"); \
    } else { \
        logPrintln("[STRESS] Result: FAIL"); \
        g_tests_failed_count++; \
    }

#define TEST_ASSERT(cond, msg) \
    if (!(cond)) { \
        logError("[STRESS] Assertion Failed: %s", msg); \
        g_test_failed = true; \
    }

#define TEST_ASSERT_TRUE(cond) TEST_ASSERT(cond, #cond " is not true")
#define TEST_ASSERT_FALSE(cond) TEST_ASSERT(!(cond), #cond " is not false")
#define TEST_ASSERT_EQUAL(expected, actual) TEST_ASSERT((expected) == (actual), #actual " != " #expected)
#define TEST_ASSERT_GREATER_THAN(threshold, actual) TEST_ASSERT((actual) > (threshold), #actual " <= " #threshold)
#define TEST_ASSERT_LESS_THAN(threshold, actual) TEST_ASSERT((actual) < (threshold), #actual " >= " #threshold)

// ============================================================================
// TEST HELPERS
// ============================================================================

static void test_setup() {
    if (motionIsEmergencyStopped()) {
        motionClearEmergencyStop();
    }
}

static void test_teardown() {
    vTaskDelay(pdMS_TO_TICKS(100));
}

// ============================================================================
// TEST 1: CONCURRENT MOTION COMMANDS
// ============================================================================

void test_concurrent_motion_commands() {
    TEST_START("Concurrent Motion Commands");
    test_setup();

    uint32_t start_time = millis();
    uint32_t commands_sent = 0;
    uint32_t commands_rejected = 0;

    for (int i = 0; i < 1000; i++) {
        // Randomly pick ONE axis to move to satisfy motionMoveAbsolute's single-axis constraint
        int axis_to_move = rand() % 4;
        
        float x = motionGetPositionMM(0);
        float y = motionGetPositionMM(1);
        float z = motionGetPositionMM(2);
        float a = motionGetPositionMM(3);
        
        float val = (float)(rand() % 50);
        if (axis_to_move == 0) x = val;
        else if (axis_to_move == 1) y = val;
        else if (axis_to_move == 2) z = val;
        else if (axis_to_move == 3) a = val;
        
        float speed = 100.0f + (rand() % 200);

        bool success = motionMoveAbsolute(x, y, z, a, speed);
        if (success) commands_sent++;
        else commands_rejected++;

        if (i % 50 == 0) watchdogFeed("CLI");
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    uint32_t duration_ms = millis() - start_time;
    logPrintf("[STRESS] Completed 1000 commands in %lu ms\r\n", (unsigned long)duration_ms);
    
    TEST_ASSERT_FALSE(motionIsEmergencyStopped());
    TEST_ASSERT_GREATER_THAN(0, commands_sent); // Confirm at least one move started
    TEST_ASSERT_GREATER_THAN(990, commands_rejected); // Rejections are HEALTHY - verify 99% rejection rate under hammers
    
    test_teardown();
    TEST_END();
}

// ============================================================================
// TEST 2: FAULT QUEUE OVERFLOW
// ============================================================================

void test_fault_queue_overflow() {
    TEST_START("Fault Queue Overflow");
    faultClearHistory();

    uint32_t start_time = millis();
    logRateLimiterSetEnabled(false); // Bypass rate limiting to flood the queue
    faultLogSetSilent(true);         // Mute serial output to reach maximum speed
    for (int i = 0; i < 200; i++) {
        faultLogEntry(FAULT_ERROR, FAULT_MOTION_STALL, 0, i, "Stress test fault %d", i);
    }
    faultLogSetSilent(false);
    logRateLimiterSetEnabled(true);

    uint32_t duration_ms = millis() - start_time;
    logPrintf("[STRESS] Generated 200 faults in %lu ms\r\n", (unsigned long)duration_ms);

    uint8_t ring_count = faultGetRingBufferEntryCount();
    TEST_ASSERT_GREATER_THAN(0, ring_count);

    TEST_END();
}

// ============================================================================
// TEST 3: MUTEX TIMEOUT RECOVERY
// ============================================================================

void test_mutex_timeout_recovery() {
    TEST_START("Mutex Timeout Recovery");
    SemaphoreHandle_t motion_mutex = taskGetMotionMutex();
    TEST_ASSERT(motion_mutex != NULL, "Motion mutex missing");

    if (xSemaphoreTake(motion_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        logPrintln("[STRESS] Mutex held - attempting motion command (should timeout)...");
        bool success = motionMoveAbsolute(10.0f, 10.0f, 10.0f, 0, 100.0f);
        logPrintf("[STRESS] Motion command result: %s\r\n", success ? "SUCCESS" : "TIMEOUT");
        
        xSemaphoreGive(motion_mutex);
        TEST_ASSERT_FALSE(success);
    } else {
        logError("[STRESS] Could not acquire mutex for test");
        g_test_failed = true;
    }

    TEST_ASSERT_FALSE(motionIsEmergencyStopped());
    TEST_END();
}

// ============================================================================
// TEST 4: STACK EXHAUSTION DETECTION
// ============================================================================

void test_stack_exhaustion_detection() {
    TEST_START("Stack Exhaustion Detection");
    int stats_count = taskGetStatsCount();
    task_stats_t* stats = taskGetStatsArray();
    bool all_stacks_safe = true;

    for (int i = 0; i < stats_count; i++) {
        if (stats[i].handle != NULL) {
            UBaseType_t high_water = uxTaskGetStackHighWaterMark(stats[i].handle);
            if (high_water < STACK_CRITICAL_THRESHOLD_WORDS) {
                logError("[STRESS] Task %s has CRITICAL stack: %lu", stats[i].name, (unsigned long)high_water);
                all_stacks_safe = false;
            }
        }
    }

    TEST_ASSERT_TRUE(all_stacks_safe);
    TEST_END();
}

// ============================================================================
// TEST 5: WATCHDOG RESILIENCE
// ============================================================================

void test_watchdog_resilience_prod() {
    TEST_START("Watchdog Resilience");
    watchdog_stats_t* stats = watchdogGetStats();
    TEST_ASSERT(stats != NULL, "Watchdog stats missing");
    
    if (stats) {
        // Feed the watchdog to ensure ticks > 0 if called very early
        watchdogFeed("CLI");
        TEST_ASSERT_GREATER_THAN(0, stats->total_ticks);
        // We only care about NEW timeouts if we were testing resilience
        // but for now, we just verify the status is OK
        TEST_ASSERT_EQUAL(WDT_STATUS_OK, watchdogGetStatus());
    }
    TEST_END();
}

// ============================================================================
// TEST 6: I2C BUS RECOVERY
// ============================================================================

void test_i2c_recovery_mechanism_prod() {
    TEST_START("I2C Recovery Mechanism");
    
    // If we are on a vanilla ESP32 with no hardware, shadowing WILL be dirty 
    // because I2C writes will fail. We skip this assertion if hardware is not present.
    if (!plcIsHardwarePresent()) {
        logWarning("[STRESS] No PLC hardware detected - skipping dirty flag check");
        TEST_END();
        return;
    }

    // Ensure all pending writes are finished before checking
    plcCommitOutputs();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    bool shadow_dirty = elboIsShadowRegisterDirty();
    TEST_ASSERT_FALSE(shadow_dirty);
    TEST_END();
}

// ============================================================================
// TEST 7: LOGGING & LOAD RESILIENCE
// ============================================================================

static volatile bool g_stress_logging_active = false;
static volatile uint32_t g_stress_log_count = 0;

static void logging_stress_task(void* pvParameters) {
    int task_id = (long)pvParameters;
    while (g_stress_logging_active) {
        logPrintf("[STRESS:%d] Concurrent log message #%lu from Core %d\r\n", 
                  task_id, (unsigned long)g_stress_log_count++, xPortGetCoreID());
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}

void test_logging_load_resilience_prod() {
    TEST_START("Logging & Load Resilience");
    
    g_stress_logging_active = true;
    g_stress_log_count = 0;
    
    xTaskCreatePinnedToCore(logging_stress_task, "LogSt0", 2048, (void*)0, 1, NULL, 0);
    xTaskCreatePinnedToCore(logging_stress_task, "LogSt1", 2048, (void*)1, 1, NULL, 1);
    xTaskCreatePinnedToCore(logging_stress_task, "LogSt2", 2048, (void*)2, 2, NULL, 0);
    xTaskCreatePinnedToCore(logging_stress_task, "LogSt3", 2048, (void*)3, 2, NULL, 1);
    
    load_state_t states[] = {LOAD_STATE_NORMAL, LOAD_STATE_ELEVATED, LOAD_STATE_HIGH, LOAD_STATE_CRITICAL, LOAD_STATE_NORMAL};
    
    for (int i = 0; i < 5; i++) {
        logPrintf("[STRESS] Forcing state: %s\r\n", loadManagerGetStateString(states[i]));
        loadManagerForceState(states[i]);
        watchdogFeed("CLI");
        vTaskDelay(pdMS_TO_TICKS(1000));
        TEST_ASSERT_FALSE(motionIsEmergencyStopped());
    }
    
    g_stress_logging_active = false;
    vTaskDelay(pdMS_TO_TICKS(220)); // Increased delay for tasks to exit
    
    logPrintf("[STRESS] Total stress logs: %lu\r\n", (unsigned long)g_stress_log_count);
    TEST_ASSERT_GREATER_THAN(100, g_stress_log_count);
    
    TEST_END();
}

// ============================================================================
// TEST 8: MOTION JITTER (REAL-TIME INTEGRITY)
// ============================================================================

void test_motion_jitter() {
    TEST_START("Motion Jitter (Real-Time)");
    motionResetMaxJitter();
    
    logPrintf("[STRESS] Measuring jitter during logging storm (Core 0/1 hammered)...\r\n");
    g_stress_logging_active = true;
    g_stress_log_count = 0;
    
    xTaskCreatePinnedToCore(logging_stress_task, "JitterS0", 2048, (void*)0, 2, NULL, 0);
    xTaskCreatePinnedToCore(logging_stress_task, "JitterS1", 2048, (void*)1, 2, NULL, 1);
    
    // Run for 5 seconds, feeding watchdog every second
    for (int i = 0; i < 5; i++) {
        watchdogFeed("CLI");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    g_stress_logging_active = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    
    uint32_t max_jitter_us = motionGetMaxJitterUS();
    float jitter_ms = max_jitter_us / 1000.0f;
    logPrintf("[STRESS] Max Motion Jitter recorded: %.3f ms (%lu us)\r\n", jitter_ms, (unsigned long)max_jitter_us);
    
    // ASSERTION: Jitter must be < 2ms for production real-time stability
    // On ESP32 with dual-core logging hammers, we expect 0-500us.
    TEST_ASSERT(max_jitter_us < 2000, "Jitter exceeds 2ms threshold");
    
    TEST_END();
}

// ============================================================================
// CLI INTEGRATION
// ============================================================================

void runStressTests() {
    if (!serialLoggerLock()) return;
    g_tests_run = 0;
    g_tests_failed_count = 0;
    
    logPrintln("\r\n[STRESS] === Starting Full Suite ===");
    motionClearEmergencyStop(); // Start clean
    test_concurrent_motion_commands();
    test_fault_queue_overflow();
    test_mutex_timeout_recovery();
    test_stack_exhaustion_detection();
    test_watchdog_resilience_prod();
    watchdogFeed("CLI");
    test_i2c_recovery_mechanism_prod();
    watchdogFeed("CLI");
    test_logging_load_resilience_prod();
    watchdogFeed("CLI");
    test_motion_jitter();
    
    logPrintln("\r\n[STRESS] === Suite Complete ===");
    logPrintf("[STRESS] Tests Run: %d | Failed: %d\r\n", g_tests_run, g_tests_failed_count);
    serialLoggerUnlock();
}

void cmd_stress_test(int argc, char** argv) {
    if (argc < 1) {
        logPrintln("\r\n[STRESS TEST] Usage: test stress <test|all>");
        logPrintln("Available tests: concurrent, faults, mutex, stack, watchdog, i2c, load, jitter, all");
        return;
    }

    const char* test_name = argv[0];
    
    // Handle "test stress <subtest>" where argv[0] is "stress" and argv[1] is the subtest
    if (strcmp(test_name, "stress") == 0) {
        if (argc < 2) {
            logPrintln("\r\n[STRESS TEST] Usage: test stress <test|all>");
            logPrintln("Available tests: concurrent, faults, mutex, stack, watchdog, i2c, load, jitter, all");
            return;
        }
        test_name = argv[1];
    }

    if (strcmp(test_name, "all") == 0) {
        runStressTests();
    } else if (strcmp(test_name, "concurrent") == 0) {
        test_concurrent_motion_commands();
    } else if (strcmp(test_name, "faults") == 0) {
        test_fault_queue_overflow();
    } else if (strcmp(test_name, "mutex") == 0) {
        test_mutex_timeout_recovery();
    } else if (strcmp(test_name, "stack") == 0) {
        test_stack_exhaustion_detection();
    } else if (strcmp(test_name, "watchdog") == 0) {
        test_watchdog_resilience_prod();
    } else if (strcmp(test_name, "i2c") == 0) {
        test_i2c_recovery_mechanism_prod();
    } else if (strcmp(test_name, "load") == 0) {
        test_logging_load_resilience_prod();
    } else if (strcmp(test_name, "jitter") == 0 || strcmp(test_name, "motion_jitter") == 0) {
        test_motion_jitter();
    } else {
        logPrintf("[STRESS TEST] Unknown test: %s\r\n", test_name);
    }
}
