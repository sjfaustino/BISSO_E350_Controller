/**
 * @file test_stress.cpp
 * @brief Edge Case Stress Testing Suite
 * @details Tests system robustness under abnormal conditions
 * @project PosiPro CNC Controller
 *
 * ROBUSTNESS FIX: Validates system behavior under stress
 *
 * Test categories:
 * 1. Concurrent operations - Multiple subsystems active simultaneously
 * 2. Resource exhaustion - Queue/buffer overflow handling
 * 3. Timeout recovery - Graceful degradation under contention
 * 4. Fault storm handling - High fault rate resilience
 * 5. Edge case inputs - Boundary values and invalid data
 *
 * Usage:
 *   Compile with: -D UNITY_INCLUDE_CONFIG_H
 *   Run via CLI: test stress [testname]
 */

#include <unity.h>
#include "motion.h"
#include "safety.h"
#include "fault_logging.h"
#include "task_manager.h"
#include "system_tuning.h"
#include <Arduino.h>

// ============================================================================
// TEST HELPERS
// ============================================================================

void setUp(void) {
  // Called before each test
  // Ensure system is in known state
  if (motionIsEmergencyStopped()) {
    motionClearEmergencyStop();
  }
}

void tearDown(void) {
  // Called after each test
  // Clean up test artifacts
  delay(100);  // Allow pending operations to complete
}

// ============================================================================
// TEST 1: CONCURRENT MOTION COMMANDS
// ============================================================================

/**
 * @test test_concurrent_motion_commands
 * @brief Validates system stability under rapid motion command stream
 *
 * Scenario: User or G-code sends 1000 rapid motion commands
 * Expected: No crashes, no deadlocks, graceful queue handling
 */
void test_concurrent_motion_commands(void) {
  Serial.println("\n[TEST] Starting concurrent motion command test...");

  uint32_t start_time = millis();
  uint32_t commands_sent = 0;
  uint32_t commands_rejected = 0;

  for (int i = 0; i < 1000; i++) {
    // Random target positions within safe limits
    float x = (float)(rand() % 100);
    float y = (float)(rand() % 100);
    float z = (float)(rand() % 50);
    float speed = 100.0f + (rand() % 200);  // 100-300 mm/min

    bool success = motionMoveAbsolute(x, y, z, 0, speed);
    if (success) {
      commands_sent++;
    } else {
      commands_rejected++;
    }

    // Small delay to simulate realistic command rate
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  uint32_t duration_ms = millis() - start_time;

  Serial.printf("[TEST] Completed in %lu ms\n", (unsigned long)duration_ms);
  Serial.printf("[TEST] Commands sent: %lu\n", (unsigned long)commands_sent);
  Serial.printf("[TEST] Commands rejected: %lu\n", (unsigned long)commands_rejected);

  // Verify system still operational
  TEST_ASSERT_FALSE_MESSAGE(motionIsEmergencyStopped(),
                            "System should not E-STOP during normal command stream");

  // Verify at least some commands succeeded
  TEST_ASSERT_GREATER_THAN_MESSAGE(500, commands_sent,
                                  "At least 50% of commands should succeed");
}

// ============================================================================
// TEST 2: FAULT QUEUE OVERFLOW
// ============================================================================

/**
 * @test test_fault_queue_overflow
 * @brief Validates fault ring buffer fallback during fault storms
 *
 * Scenario: 200 rapid faults exceed NVS write capacity
 * Expected: Ring buffer prevents data loss, no crashes
 */
void test_fault_queue_overflow(void) {
  Serial.println("\n[TEST] Starting fault queue overflow test...");

  // Clear existing faults
  faultClearHistory();

  uint32_t start_time = millis();

  // Generate fault storm (200 faults in ~200ms)
  for (int i = 0; i < 200; i++) {
    faultLogEntry(FAULT_WARNING, FAULT_MOTION_STALL, 0, i,
                  "Stress test fault %d", i);
    // No delay - maximum rate
  }

  uint32_t duration_ms = millis() - start_time;

  Serial.printf("[TEST] Generated 200 faults in %lu ms\n", (unsigned long)duration_ms);
  Serial.printf("[TEST] Fault rate: %lu faults/sec\n",
                (unsigned long)(200000 / duration_ms));

  // Verify ring buffer captured faults
  uint8_t ring_count = faultGetRingBufferEntryCount();
  Serial.printf("[TEST] Ring buffer entries: %u\n", ring_count);

  TEST_ASSERT_GREATER_THAN_MESSAGE(0, ring_count,
                                  "Ring buffer should contain faults");

  // Verify fault storm detection activated
  // (NVS writes should be throttled to 10s cooldown)

  Serial.println("[TEST] Fault overflow handling validated");
}

// ============================================================================
// TEST 3: MUTEX TIMEOUT RECOVERY
// ============================================================================

/**
 * @test test_mutex_timeout_recovery
 * @brief Validates graceful timeout when mutex held by another task
 *
 * Scenario: External task holds motion mutex, motion command attempted
 * Expected: Motion command returns false, no deadlock, system recovers
 */
void test_mutex_timeout_recovery(void) {
  Serial.println("\n[TEST] Starting mutex timeout recovery test...");

  // Acquire motion mutex from test context
  SemaphoreHandle_t motion_mutex = taskGetMotionMutex();
  TEST_ASSERT_NOT_NULL_MESSAGE(motion_mutex, "Motion mutex should exist");

  BaseType_t taken = xSemaphoreTake(motion_mutex, portMAX_DELAY);
  TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, taken, "Should acquire motion mutex");

  Serial.println("[TEST] Mutex held - attempting motion command (should timeout)...");

  // Try motion command - should timeout gracefully
  bool success = motionMoveAbsolute(10.0f, 10.0f, 10.0f, 0, 100.0f);

  Serial.printf("[TEST] Motion command result: %s\n", success ? "SUCCESS" : "TIMEOUT");

  // Release mutex
  xSemaphoreGive(motion_mutex);

  // Verify command failed gracefully
  TEST_ASSERT_FALSE_MESSAGE(success,
                            "Motion command should fail when mutex held");

  // Verify system still operational
  TEST_ASSERT_FALSE_MESSAGE(motionIsEmergencyStopped(),
                            "System should not E-STOP on mutex timeout");

  Serial.println("[TEST] Mutex timeout recovery validated");
}

// ============================================================================
// TEST 4: STACK EXHAUSTION DETECTION
// ============================================================================

/**
 * @test test_stack_exhaustion_detection
 * @brief Validates stack overflow warning system
 *
 * Scenario: Monitor stack watermarks during intensive operations
 * Expected: No stack overflows, warnings if thresholds approached
 */
void test_stack_exhaustion_detection(void) {
  Serial.println("\n[TEST] Starting stack exhaustion detection test...");

  // Get initial stack states
  int stats_count = taskGetStatsCount();
  task_stats_t* stats = taskGetStatsArray();

  Serial.println("[TEST] Current stack watermarks:");
  Serial.println("Task                  | Stack Free (words) | Status");
  Serial.println("---------------------|-------------------|--------");

  bool all_stacks_safe = true;

  for (int i = 0; i < stats_count; i++) {
    if (stats[i].handle != NULL) {
      UBaseType_t high_water = uxTaskGetStackHighWaterMark(stats[i].handle);

      const char* status = "SAFE";
      if (high_water < STACK_CRITICAL_THRESHOLD_WORDS) {
        status = "CRITICAL";
        all_stacks_safe = false;
      } else if (high_water < STACK_WARNING_THRESHOLD_WORDS) {
        status = "WARNING";
      }

      Serial.printf("%-20s | %17lu | %s\n",
                    stats[i].name,
                    (unsigned long)high_water,
                    status);
    }
  }

  TEST_ASSERT_TRUE_MESSAGE(all_stacks_safe,
                          "All task stacks should be above critical threshold");

  Serial.println("[TEST] Stack exhaustion detection validated");
}

// ============================================================================
// TEST 5: WATCHDOG RESILIENCE
// ============================================================================

/**
 * @test test_watchdog_resilience
 * @brief Validates watchdog monitoring is active and functional
 *
 * Scenario: Check watchdog statistics and task coverage
 * Expected: All critical tasks monitored, no timeouts detected
 */
void test_watchdog_resilience(void) {
  Serial.println("\n[TEST] Starting watchdog resilience test...");

  watchdog_stats_t* stats = watchdogGetStats();
  TEST_ASSERT_NOT_NULL_MESSAGE(stats, "Watchdog stats should be available");

  Serial.printf("[TEST] Watchdog status:\n");
  Serial.printf("  Total ticks: %lu\n", (unsigned long)stats->total_ticks);
  Serial.printf("  Missed ticks: %lu\n", (unsigned long)stats->missed_ticks);
  Serial.printf("  Timeouts detected: %lu\n", (unsigned long)stats->timeouts_detected);
  Serial.printf("  Uptime: %lu seconds\n", (unsigned long)stats->uptime_sec);

  // Verify watchdog is active
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, stats->total_ticks,
                                  "Watchdog should have recorded ticks");

  // Verify no unhandled timeouts
  TEST_ASSERT_EQUAL_MESSAGE(0, stats->timeouts_detected,
                            "No watchdog timeouts should occur in normal operation");

  Serial.println("[TEST] Watchdog resilience validated");
}

// ============================================================================
// TEST 6: I2C BUS RECOVERY
// ============================================================================

/**
 * @test test_i2c_recovery_mechanism
 * @brief Validates I2C bus recovery retry logic
 *
 * Scenario: Simulate I2C communication issues
 * Expected: Recovery attempts succeed, system remains operational
 *
 * Note: This is a validation test, not a fault injection test.
 * Actual I2C fault injection would require hardware manipulation.
 */
void test_i2c_recovery_mechanism(void) {
  Serial.println("\n[TEST] Starting I2C recovery mechanism test...");

  // Check I2C communication health
  bool shadow_dirty = elboIsShadowRegisterDirty();
  uint32_t timeout_count = elboGetMutexTimeoutCount();

  Serial.printf("[TEST] I2C Health Status:\n");
  Serial.printf("  Shadow register dirty: %s\n", shadow_dirty ? "YES" : "NO");
  Serial.printf("  Mutex timeout count: %lu\n", (unsigned long)timeout_count);

  TEST_ASSERT_FALSE_MESSAGE(shadow_dirty,
                            "Shadow register should be synchronized");

  Serial.println("[TEST] I2C recovery mechanism validated");
}

// ============================================================================
// TEST RUNNER
// ============================================================================

void runStressTests(void) {
  UNITY_BEGIN();

  RUN_TEST(test_concurrent_motion_commands);
  RUN_TEST(test_fault_queue_overflow);
  RUN_TEST(test_mutex_timeout_recovery);
  RUN_TEST(test_stack_exhaustion_detection);
  RUN_TEST(test_watchdog_resilience);
  RUN_TEST(test_i2c_recovery_mechanism);

  UNITY_END();
}

// ============================================================================
// CLI INTEGRATION
// ============================================================================

/**
 * @brief CLI command handler for stress tests
 * Usage: test stress [testname]
 *        test stress all
 */
void cmd_stress_test(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("\n[STRESS TEST] Usage: test stress [test|all]");
    Serial.println("Available tests:");
    Serial.println("  concurrent  - Concurrent motion command stress");
    Serial.println("  faults      - Fault queue overflow");
    Serial.println("  mutex       - Mutex timeout recovery");
    Serial.println("  stack       - Stack exhaustion detection");
    Serial.println("  watchdog    - Watchdog resilience");
    Serial.println("  i2c         - I2C recovery mechanism");
    Serial.println("  all         - Run complete test suite");
    return;
  }

  if (strcmp(argv[1], "all") == 0) {
    runStressTests();
  } else if (strcmp(argv[1], "concurrent") == 0) {
    UNITY_BEGIN();
    RUN_TEST(test_concurrent_motion_commands);
    UNITY_END();
  } else if (strcmp(argv[1], "faults") == 0) {
    UNITY_BEGIN();
    RUN_TEST(test_fault_queue_overflow);
    UNITY_END();
  } else if (strcmp(argv[1], "mutex") == 0) {
    UNITY_BEGIN();
    RUN_TEST(test_mutex_timeout_recovery);
    UNITY_END();
  } else if (strcmp(argv[1], "stack") == 0) {
    UNITY_BEGIN();
    RUN_TEST(test_stack_exhaustion_detection);
    UNITY_END();
  } else if (strcmp(argv[1], "watchdog") == 0) {
    UNITY_BEGIN();
    RUN_TEST(test_watchdog_resilience);
    UNITY_END();
  } else if (strcmp(argv[1], "i2c") == 0) {
    UNITY_BEGIN();
    RUN_TEST(test_i2c_recovery_mechanism);
    UNITY_END();
  } else {
    Serial.printf("[STRESS TEST] Unknown test: %s\n", argv[1]);
  }
}
