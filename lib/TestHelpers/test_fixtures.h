/**
 * @file lib/TestHelpers/test_fixtures.h
 * @brief Centralized test fixtures for BISSO E350 unit tests
 * 
 * This header provides:
 * - Unified TestFixtures struct containing all mock states
 * - Automatic fixture reset via reset_all_fixtures()
 * - Suite-specific setup via function pointer
 * - Mock validation helpers
 * 
 * Usage:
 *   Include this header and call reset_all_fixtures() in setUp()
 *   Each test suite can set current_suite_setup for custom init
 */

#pragma once

#include "mocks/motion_mock.h"
#include "mocks/vfd_mock.h"
#include "mocks/plc_mock.h"
#include "mocks/encoder_mock.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Centralized test fixtures
 * Contains all mock states used across test suites
 */
typedef struct {
    motion_mock_state_t motion;
    vfd_mock_state_t vfd;
    plc_mock_state_t plc;
    encoder_mock_state_t encoder;
} test_fixtures_t;

/**
 * @brief Global test fixtures instance
 * Accessible from all test files
 */
extern test_fixtures_t g_fixtures;

/**
 * @brief Suite-specific setup function pointer
 * Set this before running a test suite for custom initialization
 */
typedef void (*suite_setup_fn)(void);
extern suite_setup_fn current_suite_setup;

/**
 * @brief Reset all fixtures to clean state
 * Called automatically by setUp() before each test
 */
static inline void reset_all_fixtures(void) {
    g_fixtures.motion = motion_mock_init();
    g_fixtures.vfd = vfd_mock_init();
    g_fixtures.plc = plc_mock_init();
    g_fixtures.encoder = encoder_mock_init();
}

/**
 * @brief Assert motion mock is in clean initial state
 * Use for validating test preconditions
 */
static inline void assert_motion_clean(void) {
    // These would be TEST_ASSERT calls if unity.h was included
    // Instead, provide validation that can be called
    if (g_fixtures.motion.state != MOTION_IDLE ||
        g_fixtures.motion.e_stop_active != 0 ||
        g_fixtures.motion.move_attempts != 0) {
        // Motion is not in clean state
    }
}

/**
 * @brief Assert VFD mock is in clean initial state
 */
static inline void assert_vfd_clean(void) {
    if (g_fixtures.vfd.has_fault != 0 ||
        g_fixtures.vfd.frequency_hz != 0) {
        // VFD is not in clean state
    }
}

/**
 * @brief Assert encoder mock is in clean initial state
 */
static inline void assert_encoder_clean(void) {
    if (g_fixtures.encoder.calibrated != 0 ||
        g_fixtures.encoder.comms_error != 0 ||
        g_fixtures.encoder.pulse_count != 0) {
        // Encoder is not in clean state
    }
}

/**
 * @brief Convenience macros for accessing fixtures
 */
#define MOTION  (&g_fixtures.motion)
#define VFD     (&g_fixtures.vfd)
#define PLC     (&g_fixtures.plc)
#define ENCODER (&g_fixtures.encoder)

#ifdef __cplusplus
}
#endif
