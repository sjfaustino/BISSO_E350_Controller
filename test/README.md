# PosiPro CNC Controller - Test Suite

## Overview

This directory contains edge case stress tests for validating system robustness under abnormal conditions.

## Test Framework

- **Framework:** Unity Test Framework
- **Platform:** ESP32-S3 (Arduino)
- **Execution:** Via Serial CLI or automated CI/CD

## Test Categories

### 1. Concurrent Operations Tests
**Purpose:** Validate system stability under simultaneous operations
**Files:** `test_stress.cpp::test_concurrent_motion_commands`

Tests rapid command streams that stress:
- Motion command queue
- Mutex contention
- Task scheduling
- Memory allocation

**Expected Behavior:**
- No crashes or deadlocks
- Graceful command rejection when queue full
- System remains responsive
- No E-STOP triggers

### 2. Resource Exhaustion Tests
**Purpose:** Validate graceful degradation when resources exhausted
**Files:** `test_stress.cpp::test_fault_queue_overflow`

Tests overflow scenarios:
- Fault ring buffer overflow
- NVS write queue saturation
- Flash wear protection activation

**Expected Behavior:**
- Ring buffer prevents data loss
- Adaptive cooldown activates during fault storms
- Oldest entries overwritten (FIFO)
- No system crashes

### 3. Timeout Recovery Tests
**Purpose:** Validate graceful failure on mutex/semaphore timeouts
**Files:** `test_stress.cpp::test_mutex_timeout_recovery`

Tests contention scenarios:
- Motion mutex held by external task
- I2C bus mutex timeout
- Config mutex timeout

**Expected Behavior:**
- Commands return false/error status
- No infinite blocking
- System recovers when mutex released
- No E-STOP on timeout alone

### 4. Stack Exhaustion Tests
**Purpose:** Validate stack overflow detection and prevention
**Files:** `test_stress.cpp::test_stack_exhaustion_detection`

Tests stack monitoring:
- All tasks have adequate stack
- Warnings trigger at thresholds
- Critical alerts logged to fault system

**Expected Behavior:**
- All tasks > STACK_CRITICAL_THRESHOLD_WORDS
- Warnings logged if approaching limits
- Fault entries created for critical stacks

### 5. Watchdog Resilience Tests
**Purpose:** Validate watchdog monitoring coverage and function
**Files:** `test_stress.cpp::test_watchdog_resilience`

Tests watchdog system:
- All critical tasks registered
- Feed intervals appropriate
- Timeout detection working

**Expected Behavior:**
- All tasks feeding watchdog
- No unhandled timeouts in normal operation
- Statistics showing healthy feed pattern

### 6. I2C Recovery Tests
**Purpose:** Validate I2C bus recovery mechanisms
**Files:** `test_stress.cpp::test_i2c_recovery_mechanism`

Tests I2C resilience:
- Shadow register synchronization
- Bus recovery retry logic
- Mutex timeout handling

**Expected Behavior:**
- Shadow register clean
- Recovery attempts before E-STOP
- Graceful degradation on persistent failures

## Running Tests

### Via Serial CLI

```
test stress all          # Run complete suite
test stress concurrent   # Run specific test
test stress faults       # Run fault overflow test
test stress mutex        # Run mutex timeout test
test stress stack        # Run stack monitoring test
test stress watchdog     # Run watchdog validation
test stress i2c          # Run I2C recovery test
```

### Expected Output

```
[TEST] Starting concurrent motion command test...
[TEST] Completed in 1234 ms
[TEST] Commands sent: 980
[TEST] Commands rejected: 20
test_concurrent_motion_commands:PASS

6 Tests 0 Failures 0 Ignored
OK
```

## Test Development Guidelines

### Writing New Tests

1. **Use Unity assertions:**
   ```cpp
   TEST_ASSERT_TRUE(condition);
   TEST_ASSERT_EQUAL(expected, actual);
   TEST_ASSERT_GREATER_THAN(threshold, value);
   ```

2. **Clean up in tearDown():**
   - Reset system state
   - Clear test artifacts
   - Allow pending operations to complete

3. **Document expected behavior:**
   - Add comment block explaining scenario
   - Define success criteria
   - Note any hardware dependencies

4. **Keep tests isolated:**
   - Don't depend on execution order
   - Initialize state in setUp()
   - Restore state in tearDown()

### Test Naming Convention

```
test_<component>_<scenario>
```

Examples:
- `test_motion_queue_overflow`
- `test_safety_concurrent_alarms`
- `test_config_nvs_corruption_recovery`

## Integration with CI/CD

### Automated Testing

```yaml
# .github/workflows/test.yml
- name: Run Stress Tests
  run: |
    platformio test -e esp32-s3
```

### Test Coverage Goals

- **Critical paths:** 100% coverage
- **Normal operations:** 90% coverage
- **Edge cases:** 80% coverage
- **Error paths:** 100% coverage

## Known Limitations

1. **Hardware dependencies:** Some tests require actual hardware (I2C, encoders)
2. **Timing sensitivity:** Real-time constraints make some tests flaky in simulation
3. **Fault injection:** Cannot easily inject hardware faults without external tools

## Future Enhancements

### Planned Test Additions

- [ ] Power loss recovery tests
- [ ] Network disconnect/reconnect tests
- [ ] EEPROM corruption recovery tests
- [ ] Encoder communication loss tests
- [ ] VFD communication timeout tests
- [ ] Emergency stop latency measurement
- [ ] G-code parser stress tests
- [ ] File system corruption tests

### Test Infrastructure

- [ ] Automated test execution on hardware
- [ ] Test result trending and analysis
- [ ] Performance regression detection
- [ ] Memory leak detection
- [ ] Code coverage reporting

## References

- Unity Test Framework: https://github.com/ThrowTheSwitch/Unity
- ESP32 Testing Guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html
- COMPREHENSIVE_AUDIT_REPORT.md Finding 7.2 (Edge Case Testing)

## Contributing

When adding new tests:
1. Follow existing test structure
2. Update this README with test description
3. Add CLI integration for manual testing
4. Document expected behavior
5. Test on actual hardware before committing
