/**
 * @file test/helpers/test_utils.cpp
 * @brief Test utility implementation for BISSO E350 unit tests
 */

#include "test_utils.h"
#include <cstdio>
#include <cstdlib>

/**
 * @brief Global simulated time for tests (milliseconds)
 * Allows tests to check timeout behavior without actual delays
 */
static uint32_t g_test_time_ms = 0;

void test_advance_time(uint32_t milliseconds)
{
    g_test_time_ms += milliseconds;
}

uint32_t test_get_time(void)
{
    return g_test_time_ms;
}

void test_reset_time(void)
{
    g_test_time_ms = 0;
}

void test_init_motion_fixture(motion_test_fixture_t* fixture, uint8_t axis)
{
    if (!fixture) return;

    fixture->axis = axis;
    fixture->distance_steps = 0;
    fixture->speed_hz = 20;  // Safe default: 20 Hz for stone cutting
    fixture->duration_ms = 0;
    fixture->quality_score = 100.0f;
    fixture->status = 0;  // Idle
}

void test_init_encoder_fixture(encoder_test_fixture_t* fixture)
{
    if (!fixture) return;

    fixture->ppm = 100;  // Default: 100 pulses per mm (WJ66 typical)
    fixture->position = 0;
    fixture->velocity_mms = 0.0f;
    fixture->jitter_amplitude = 0.0f;
    fixture->status = 0;  // Idle
}

void test_init_safety_fixture(safety_test_fixture_t* fixture)
{
    if (!fixture) return;

    fixture->e_stop_state = 0;  // E-stop inactive
    fixture->fault_flags = 0;   // No faults
    fixture->system_state = 0;  // Idle/safe state
    fixture->recovery_time = 0;
}

void test_init_config_fixture(config_test_fixture_t* fixture)
{
    if (!fixture) return;

    fixture->soft_limit_low_mm = 0;
    fixture->soft_limit_high_mm = 500;
    fixture->max_speed_hz = 105;      // Altivar 31 HSP
    fixture->min_speed_hz = 1;        // Altivar 31 LSP
    fixture->axis_count = 3;          // X, Y, Z
    fixture->checksum = 0xDEADBEEF;
}

void test_print_failure(const char* assertion, const char* expected, const char* actual)
{
    UnityPrint("ASSERTION FAILED: ");
    UnityPrint(assertion);
    UnityPrint("\n");
    UnityPrint("  Expected: ");
    UnityPrint(expected);
    UnityPrint("\n");
    UnityPrint("  Actual: ");
    UnityPrint(actual);
    UnityPrint("\n");
}
