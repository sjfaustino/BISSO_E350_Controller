/**
 * @file test/test_runner.cpp
 * @brief Main test runner for BISSO E350 unit tests
 *
 * This file serves as the entry point for all unit tests. It initializes
 * the Unity framework, manages test fixtures, and runs all test suites.
 * 
 * The setUp() function automatically resets all mock fixtures before each
 * test to ensure clean state. Suite-specific setup can be registered via
 * the current_suite_setup function pointer.
 */

#include <cstdio>
#include <cstdlib>
#include <unity.h>
#include "helpers/test_fixtures.h"

/**
 * @brief Global test fixtures instance
 * All mock states are stored here and reset before each test
 */
test_fixtures_t g_fixtures;

/**
 * @brief Current suite setup function
 * Set this in each run_*_tests() function for custom initialization
 */
suite_setup_fn current_suite_setup = nullptr;

/**
 * @brief Forward declarations for test suite functions
 * Each test file implements these to register its tests
 */
extern void run_motion_control_tests(void);
extern void run_safety_system_tests(void);
extern void run_encoder_validation_tests(void);
extern void run_configuration_tests(void);
extern void run_api_config_tests(void);
extern void run_api_endpoints_tests(void);
extern void run_openapi_tests(void);

/**
 * @brief setUp() - called before each test
 * Automatically resets all mock fixtures to clean state
 */
void setUp(void) {
  // Reset all mock fixtures to clean state
  reset_all_fixtures();
  
  // Call suite-specific setup if registered
  if (current_suite_setup) {
    current_suite_setup();
  }
}

/**
 * @brief tearDown() - called after each test
 * Required by Unity framework
 */
void tearDown(void) {
  // Called after each individual test
  // Currently no cleanup needed - fixtures are reset in setUp()
}

/**
 * @brief suiteSetUp() - called once at start of all tests
 * Optional, but useful for global initialization
 */
void suiteSetUp(void) {
  UnityPrint("\n");
  UnityPrint("========================================\n");
  UnityPrint("BISSO E350 Unit Test Suite\n");
  UnityPrint("========================================\n");
  UnityPrint("Initializing test framework...\n\n");
}

/**
 * @brief suiteTearDown() - called once at end of all tests
 * Optional, but useful for global cleanup and reporting
 */
int suiteTearDown(int num_failures) {
  UnityPrint("\n");
  UnityPrint("========================================\n");
  if (num_failures == 0) {
    UnityPrint("✓ ALL TESTS PASSED\n");
  } else {
    UnityPrint("✗ TESTS FAILED: ");
    UnityPrintNumber(num_failures);
    UnityPrint(" failures\n");
  }
  UnityPrint("========================================\n\n");

  return (num_failures == 0) ? 0 : 1; // Return exit code
}

/**
 * @brief main() - Test runner entry point
 *
 * This is the executable that gets compiled and run for testing.
 * It initializes Unity and executes all registered tests.
 */
int main(int argc, char *argv[]) {
  (void)argc; // Suppress unused parameter warning

  // Initialize Unity
  UnityBegin(argv[0]);

  // Run test suites
  UnityPrint("Running Motion Control Tests...\n");
  run_motion_control_tests();

  UnityPrint("\nRunning Safety System Tests...\n");
  run_safety_system_tests();

  UnityPrint("\nRunning Encoder Validation Tests...\n");
  run_encoder_validation_tests();

  UnityPrint("\nRunning Configuration Tests...\n");
  run_configuration_tests();

  UnityPrint("\nRunning API Configuration Tests...\n");
  run_api_config_tests();

  UnityPrint("\nRunning API Endpoints Tests...\n");
  run_api_endpoints_tests();

  UnityPrint("\nRunning OpenAPI Specification Tests...\n");
  run_openapi_tests();

  // Finish and report
  return UnityEnd();
}
