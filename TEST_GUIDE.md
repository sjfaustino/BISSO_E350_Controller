# BISSO E350 Unit Testing Guide

## Overview

This document describes the automated unit testing framework for the BISSO E350 CNC Controller. The framework uses **Unity** (a lightweight C unit test framework) combined with **mock objects** to test safety-critical components without requiring actual hardware.

**Target Coverage:**
- Motion Control System
- Safety System (E-stop, faults, recovery)
- Encoder Validation & Feedback
- Configuration Management

## Quick Start

### Running All Tests

```bash
# Run all tests using PlatformIO
pio run -e test

# Or directly:
.pio/build/native/program
```

### Building Tests Only

```bash
# Compile test environment without running
pio run -e test -t build
```

## Test Framework Architecture

### Directory Structure

```
test/
├── unity/                          # Unity framework (third-party)
│   ├── unity.h                     # Main framework header
│   ├── unity_internals.h           # Internal implementation
│   └── unity.c                     # Framework implementation
│
├── mocks/                          # Hardware mock objects
│   ├── vfd_mock.h / vfd_mock.cpp          # Altivar 31 VFD simulator
│   ├── plc_mock.h / plc_mock.cpp          # PLC contactor system
│   ├── encoder_mock.h / encoder_mock.cpp  # WJ66 optical encoder
│   └── motion_mock.h / motion_mock.cpp    # Motion controller
│
├── helpers/                        # Test utilities
│   ├── test_utils.h                # Common assertion helpers
│   └── test_utils.cpp              # Utility implementations
│
├── test_motion_control.cpp         # Motion tests
├── test_safety_system.cpp          # Safety tests
├── test_encoder_validation.cpp     # Encoder tests
├── test_configuration.cpp          # Configuration tests
│
└── test_runner.cpp                 # Main entry point
```

### Mock Objects

Each mock simulates hardware behavior for isolated unit testing:

#### VFD Mock (`vfd_mock.h`)
Simulates Altivar 31 VFD behavior:
- Frequency ramping (acceleration/deceleration)
- Motor current estimation
- Thermal modeling
- Fault injection
- Default config: HSP=105Hz, LSP=1Hz, ACC=0.6s, DEC=0.4s

```c
vfd_mock_state_t vfd = vfd_mock_init();
vfd_mock_set_frequency(&vfd, 50);      // Set target to 50 Hz
vfd_mock_advance_time(&vfd, 600);      // Simulate 600ms
// vfd.frequency_hz will be ramped from 0 to 50 Hz over 600ms
```

#### PLC Mock (`plc_mock.h`)
Simulates PLC contactor switching:
- Single active axis at a time
- Settling time simulation (~50ms)
- Contactor operation counting
- Switching error injection

```c
plc_mock_state_t plc = plc_mock_init();
plc_mock_select_axis(&plc, AXIS_X);     // Select X axis
plc_mock_advance_time(&plc, 50);        // Wait for settling
if (plc_mock_is_settled(&plc)) {
    // Contactor switched successfully
}
```

#### Encoder Mock (`encoder_mock.h`)
Simulates WJ66 optical encoder:
- Position and velocity tracking
- Calibration (PPM - pulses per mm)
- Jitter and noise injection
- Deviation from target velocity
- Communication error injection

```c
encoder_mock_state_t enc = encoder_mock_init();
encoder_mock_calibrate(&enc, 100);           // 100 PPM
encoder_mock_set_target_velocity(&enc, 15.0); // 15 mm/s
encoder_mock_inject_jitter(&enc, 0.5);       // Add wear jitter
encoder_mock_advance_time(&enc, 1000);       // Simulate 1 second
```

#### Motion Mock (`motion_mock.h`)
Simulates motion planning & validation:
- Move validation (axis, distance, speed checks)
- Soft limit enforcement
- Stall detection
- Motion quality scoring
- E-stop handling

```c
motion_mock_state_t motion = motion_mock_init();
move_validation_result_t result = motion_mock_validate_move(
    &motion, AXIS_X, 5000, 50);  // X axis, 5000 steps, 50 Hz
if (result == MOVE_VALID) {
    motion_mock_start_move(&motion, AXIS_X, 5000, 50);
}
```

### Test Utilities (`test_utils.h`)

Common assertion helpers and fixture initialization:

```c
// Floating-point assertion with tolerance
TEST_ASSERT_FLOAT_WITHIN_MESSAGE(tolerance, expected, actual, "message");

// Bitfield assertions
TEST_ASSERT_FLAGS_SET(flags, mask, expected);
TEST_ASSERT_FLAGS_CLEAR(flags, mask);

// Test logging (for debugging)
TEST_LOG("Motion quality: %.1f%%", quality_score);

// Fixture initialization
motion_test_fixture_t motion_fixture;
test_init_motion_fixture(&motion_fixture, AXIS_X);
```

## Writing Tests

### Test File Structure

Each test file follows this pattern:

```c
#include <unity.h>
#include "mocks/motion_mock.h"
#include "helpers/test_utils.h"

/**
 * @brief Setup called before each test
 */
void setUp(void)
{
    // Initialize test fixtures here
}

/**
 * @brief Teardown called after each test
 */
void tearDown(void)
{
    // Clean up after test here
}

/**
 * @brief Test case: motion validation rejects invalid axis
 */
void test_motion_validation_invalid_axis(void)
{
    motion_mock_state_t motion = motion_mock_init();

    // Attempt move on invalid axis (3)
    move_validation_result_t result = motion_mock_validate_move(
        &motion, 3, 1000, 50);

    // Verify it's rejected
    TEST_ASSERT_EQUAL(MOVE_INVALID_AXIS, result);
}

/**
 * @brief Test case: soft limits prevent out-of-range motion
 */
void test_motion_soft_limits(void)
{
    motion_mock_state_t motion = motion_mock_init();

    // Set soft limits: 100-400 steps
    motion_mock_set_soft_limits(&motion, AXIS_X, 100, 400);

    // Try to move beyond limit
    move_validation_result_t result = motion_mock_validate_move(
        &motion, AXIS_X, 500, 50);  // Would go to 500 steps

    TEST_ASSERT_EQUAL(MOVE_SOFT_LIMIT_VIOLATION, result);
}

/**
 * @brief Register all tests in this file
 * This is called from test_runner.cpp
 */
void run_motion_control_tests(void)
{
    RUN_TEST(test_motion_validation_invalid_axis);
    RUN_TEST(test_motion_soft_limits);
    // ... more tests
}
```

### Common Test Patterns

#### Pattern 1: Setup → Action → Assert

```c
void test_vfd_reaches_target_frequency(void)
{
    // SETUP
    vfd_mock_state_t vfd = vfd_mock_init();

    // ACTION
    vfd_mock_set_frequency(&vfd, 50);
    vfd_mock_advance_time(&vfd, 700);  // Longer than ACC time

    // ASSERT
    TEST_ASSERT_TRUE(vfd_mock_is_at_frequency(&vfd, 2));  // Within 2 Hz
}
```

#### Pattern 2: Fault Injection

```c
void test_vfd_thermal_fault_detected(void)
{
    vfd_mock_state_t vfd = vfd_mock_init();

    // Simulate sustained high current
    vfd_mock_set_frequency(&vfd, 105);  // Max speed
    for (int i = 0; i < 10; i++) {
        vfd_mock_advance_time(&vfd, 1000);
    }

    // Verify thermal fault was set
    TEST_ASSERT_EQUAL_UINT8(1, vfd.has_fault);
}
```

#### Pattern 3: State Machine Transitions

```c
void test_motion_state_transitions(void)
{
    motion_mock_state_t motion = motion_mock_init();
    TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));

    // Transition to MOVING
    motion_mock_start_move(&motion, AXIS_X, 1000, 50);
    TEST_ASSERT_EQUAL(MOTION_MOVING, motion_mock_get_state(&motion));

    // Simulate complete motion
    motion_mock_update(&motion, 1000, 15.0, 2.0, 1000);
    // (after motion complete)
    TEST_ASSERT_EQUAL(MOTION_IDLE, motion_mock_get_state(&motion));
}
```

## Running Specific Tests

### Run Single Test File

```bash
# Build and run only motion control tests
pio run -e test -t build
.pio/build/native/program | grep "Motion Control"
```

### Run Tests with Verbose Output

```bash
# Use PlatformIO's verbose flag
pio run -e test -v
```

### Debug Failed Tests

```bash
# Tests include detailed logging
# Check test output for:
# - Assertion failures with expected vs actual
# - Test log messages (from TEST_LOG macro)
# - Mock object status strings (from *_get_status functions)

# Add logging to understand failures:
TEST_LOG("VFD frequency: %u Hz", vfd.frequency_hz);
TEST_LOG("Encoder position: %.1f mm", enc.position_mm);
```

## Test Coverage Goals

### Motion Control Tests
- [ ] Move validation (axis, distance, speed)
- [ ] Soft limit enforcement
- [ ] Contactor settling time
- [ ] Stall detection (high current + no motion)
- [ ] Motion quality scoring

### Safety System Tests
- [ ] E-stop immediate halt
- [ ] Fault condition handling
- [ ] Recovery procedures
- [ ] State machine integrity
- [ ] Critical timeout detection

### Encoder Validation Tests
- [ ] Calibration (PPM calculation accuracy)
- [ ] Position tracking (cumulative error)
- [ ] Velocity measurement accuracy
- [ ] Jitter detection (wear monitoring)
- [ ] Communication error handling
- [ ] Deviation from target velocity

### Configuration Tests
- [ ] Schema validation
- [ ] Value range constraints
- [ ] Persistence (save/load cycle)
- [ ] Migration (version upgrades)
- [ ] Default configuration

## Continuous Integration

Tests are automatically run on every push via GitHub Actions. See `.github/workflows/ci.yml` for configuration.

### CI Pipeline
1. **Build**: Compile firmware and tests
2. **Unit Tests**: Run all test suites
3. **Coverage**: Generate code coverage report
4. **Artifacts**: Store build outputs

To run CI locally:

```bash
# Simulate GitHub Actions workflow
./run_ci.sh
```

## Extending Tests

### Adding a New Test File

1. Create `test/test_new_system.cpp`
2. Add includes and fixtures
3. Write test functions
4. Implement `run_new_system_tests()` function
5. Update `test_runner.cpp` to call your tests
6. Update `platformio.ini` if new mocks are needed

### Adding Mock Functionality

1. Add function declarations to mock header
2. Implement in mock .cpp file
3. Use in test via include

Example: Adding PLC voltage monitoring to PLC mock:

```c
// In plc_mock.h
extern float plc_mock_get_supply_voltage(plc_mock_state_t* plc);

// In plc_mock.cpp
float plc_mock_get_supply_voltage(plc_mock_state_t* plc)
{
    return 380.0f;  // Simulate 380V 3-phase
}

// In test
TEST_ASSERT_FLOAT_WITHIN(5.0, 380.0, plc_mock_get_supply_voltage(&plc));
```

## Troubleshooting

### Tests Won't Compile

```bash
# Ensure all includes are correct
# Check that mock header files are in test/mocks/
# Verify platformio.ini has correct include paths

# Try clean rebuild:
pio run -e test -t clean
pio run -e test
```

### Test Failures

1. Check assertion message for expected vs actual
2. Enable TEST_LOG statements for diagnostics
3. Verify mock initial state (check *_mock_init functions)
4. Check time advancement (some tests require simulation)

### Performance Issues

- Use `--max-parallel` flag to control test parallelization
- Disable `test_build_project_src` if only testing mocks
- Profile with `--profile` option

## Hardware Testing (Future)

Once framework is mature:
1. Replace mocks with actual hardware drivers
2. Add integration tests (firmware + hardware)
3. Benchmark real performance vs simulated
4. Validate safety system on actual equipment

## References

- **Unity Framework**: https://github.com/ThrowTheSwitch/Unity
- **Altivar 31 Manual**: VFD configuration documentation
- **BISSO E350 Architecture**: See OPERATOR_QUICKSTART.md for hardware overview
- **PlatformIO Native**: https://docs.platformio.org/en/latest/platforms/native.html

## Support

For questions about the test framework:
1. Check test examples in `test/test_*.cpp`
2. Review mock documentation in headers
3. Enable TEST_LOG output for diagnostics
4. Consult Unity framework documentation
