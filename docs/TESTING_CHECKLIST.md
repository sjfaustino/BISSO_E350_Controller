# BISSO E350 Controller - Comprehensive Testing Checklist

**Version:** Gemini v3.5.22+
**Date:** 2025-12-21
**Purpose:** Production readiness validation and regression testing

---

## 1. Pre-Deployment Testing

### 1.1 Hardware Verification

- [ ] **Power Supply Stability**
  - Verify 24V DC power supply is stable (±5% tolerance)
  - Test controller startup under low voltage (22V minimum)
  - Verify no brownout resets under full load

- [ ] **I2C Bus Integrity**
  - Verify PCF8574 I/O expanders respond at 0x20 and 0x21
  - Test I2C bus recovery after simulated bus lockup
  - Verify no I2C errors in `debug` output after 1 hour runtime

- [ ] **Serial Port Configuration**
  - WJ66 encoder responds on configured serial port (RS232 or RS485)
  - Baud rate matches encoder configuration (9600/19200/38400/115200)
  - Test encoder reads at 20Hz without timeouts

- [ ] **Physical E-Stop Button**
  - **CRITICAL:** Verify mushroom button physically cuts motor power
  - Test E-Stop during motion (all axes should halt immediately)
  - Verify motor contactors open (measure voltage at motor terminals = 0V)

---

## 2. RS485 Bus Conflict Verification (CRITICAL)

**Gemini Fix:** RS485 multiplexer state machine (Phase 5.7)
**Files:** `src/tasks_telemetry.cpp`, `src/rs485_mux.cpp`

### 2.1 Encoder-Only Operation (Baseline)

- [ ] Disable VFD polling in `config_unified.cpp` (`spindle_enabled = 0`)
- [ ] Run encoder task for 10 minutes
- [ ] Verify ZERO encoder timeouts in logs
- [ ] Record baseline encoder read latency (should be <50ms)

### 2.2 VFD + Encoder Simultaneous Operation

- [ ] Enable VFD polling (`spindle_enabled = 1`)
- [ ] Enable VFD logging: `logInfo("[VFD] State: %d", vfd_poll_state);`
- [ ] Run for 1 hour with active motion
- [ ] **PASS CRITERIA:**
  - Zero encoder timeouts
  - Zero VFD Modbus CRC errors
  - All VFD state transitions logged correctly
  - Multiplexer switches between encoder/spindle cleanly

### 2.3 Bus Conflict Detection

- [ ] **Intentional Conflict Test:** Comment out multiplexer switch in `tasks_telemetry.cpp`
- [ ] Verify encoder timeouts appear in logs (proves test is working)
- [ ] **Re-enable multiplexer** and verify timeouts disappear
- [ ] **FAIL CRITERIA:** Any encoder timeout indicates bus conflict

### 2.4 Inter-Frame Delay Validation

- [ ] Verify `rs485MuxUpdate()` enforces 10ms delay between device switches
- [ ] Use oscilloscope to measure actual TX gap (should be ≥10ms)
- [ ] Test worst-case: encoder read immediately after VFD poll

---

## 3. NVS Flash Wear Prevention

**Gemini Fix:** 1-second cooldown per fault type (Phase 5.7)
**Files:** `src/fault_logging.cpp`

### 3.1 Cooldown Mechanism

- [ ] Trigger same fault type rapidly (e.g., simulated encoder timeout)
- [ ] Monitor NVS write rate using `nvs_stats()`
- [ ] **PASS CRITERIA:** Maximum 1 write per second per fault type
- [ ] Verify fault still logged to serial (not suppressed)

### 3.2 Flash Endurance Calculation

- [ ] Calculate worst-case writes per day:
  - Fault types: 20
  - Max writes per fault: 86,400 per day (1/sec)
  - Total: 1,728,000 writes/day
  - NVS endurance: 100,000 cycles → **Failure in <6 days without cooldown**
  - **With cooldown:** 100,000 days = 274 years ✓

### 3.3 Multi-Fault Scenario

- [ ] Trigger 3 different fault types simultaneously (e.g., encoder timeout, I2C error, stall)
- [ ] Verify each fault type has independent cooldown
- [ ] Verify all 3 faults logged to NVS (not blocked by shared cooldown)

---

## 4. E-Stop Latency Validation (Safety Critical)

**Gemini Fix:** E-Stop latency monitoring (Phase 5.7)
**Files:** `src/motion_control.cpp`

### 4.1 Software E-Stop Latency

- [ ] Trigger E-Stop via CLI: `estop`
- [ ] Check serial logs for latency measurement:
  ```
  [MOTION] [CRITICAL] EMERGENCY STOP ACTIVATED (Latency: X.X ms)
  ```
- [ ] **PASS CRITERIA:** Latency < 50ms (ISO 13849 PLd requirement)
- [ ] **TARGET:** Latency < 20ms (typical measured value)

### 4.2 Latency Under Load

- [ ] Trigger E-Stop during active motion (all 4 axes moving)
- [ ] Trigger E-Stop during I2C transaction (PLC write in progress)
- [ ] Trigger E-Stop during encoder read (serial transaction in progress)
- [ ] **PASS CRITERIA:** All scenarios < 50ms

### 4.3 Priority Inversion Test

- [ ] Run encoder task (priority 20) and motion task (priority 22) simultaneously
- [ ] Verify E-Stop latency does NOT increase due to priority inheritance
- [ ] Monitor FreeRTOS task switches using `xTaskGetSchedulerState()`
- [ ] **FAIL CRITERIA:** Latency > 50ms indicates priority inversion issue

### 4.4 Hardware E-Stop Verification

- [ ] **CRITICAL:** Test physical mushroom button during motion
- [ ] Measure motor power cutoff time (should be <10ms - hardware relay)
- [ ] Verify software E-Stop is NOT primary safety mechanism
- [ ] Document that hardware E-Stop is PLd-compliant (physical interlock)

---

## 5. Memory Usage Verification

**Gemini Fix:** Ghost task removal (Phase 5.7)
**Files:** `src/task_manager.cpp`, `src/tasks_plc.cpp`

### 5.1 RAM Optimization

- [ ] Run `debug` command and check "Memory Usage" section
- [ ] **PASS CRITERIA (Ghost Task Removed):**
  - Free heap: > 200 KB (ESP32-S3 has 512KB SRAM)
  - Minimum free heap: > 150 KB (no fragmentation)
  - PLC task handle: NULL (task disabled)
  - PLC task stack usage: 0 bytes

### 5.2 Task Count Verification

- [ ] Check `xTaskGetNumberOfTasks()` output
- [ ] **Expected count:** 5 tasks (Safety, Motion, Encoder, Monitor, Web)
- [ ] **OLD count (before fix):** 6 tasks (PLC task was #6)
- [ ] Verify watchdog feed for "PLC" now comes from Motion task

### 5.3 Heap Fragmentation

- [ ] Run for 24 hours with active web requests + motion
- [ ] Monitor `heap_caps_get_largest_free_block()`
- [ ] **PASS CRITERIA:** Largest free block remains > 50% of total free heap
- [ ] **FAIL CRITERIA:** Largest block < 20% indicates fragmentation

---

## 6. Motion Accuracy & Consistency

### 6.1 Position Accuracy

- [ ] Move X axis to 0.0mm, then to 100.0mm, then back to 0.0mm
- [ ] Repeat 100 times
- [ ] **PASS CRITERIA:**
  - Final position error < ±0.05mm (encoder resolution)
  - No cumulative drift after 100 cycles

### 6.2 Speed Profile Calibration

- [ ] Calibrate all 4 axes using `calibrate speed` command
- [ ] Verify actual speed matches commanded speed (±5% tolerance)
- [ ] Test SLOW, MEDIUM, FAST profiles on each axis

### 6.3 Multi-Axis Coordination

- [ ] Simultaneous move: `move X 100 50` and `move Y 100 50`
- [ ] **FAIL CRITERIA:** Only 1 axis should move (motion arbitration enforced)
- [ ] Verify motion queue handles sequential moves correctly

---

## 7. Safety Compliance Checks

### 7.1 Stall Detection

- [ ] Block axis mechanically (simulate jam)
- [ ] Command motion on blocked axis
- [ ] **PASS CRITERIA:**
  - Stall alarm triggered within configured timeout
  - Motion stops immediately
  - Fault logged to NVS

### 7.2 Soft Limit Enforcement

- [ ] Set soft limits: `limits X 0.0 100.0`
- [ ] Attempt move beyond limits: `move X 150.0 50.0`
- [ ] **PASS CRITERIA:** Move rejected, alarm logged

### 7.3 Encoder Deviation Detection

- [ ] Simulate encoder fault (disconnect encoder cable)
- [ ] Command motion
- [ ] **PASS CRITERIA:**
  - Encoder deviation alarm within 2 seconds
  - Motion E-Stop triggered
  - Fault logged with axis and deviation value

---

## 8. Web Interface & API Testing

### 8.1 Security Validation

- [ ] Verify web interface requires authentication (HTTP Basic Auth)
- [ ] Test with wrong credentials (should return 401 Unauthorized)
- [ ] Verify credentials NOT transmitted over unencrypted HTTP outside local network
- [ ] **FAIL CRITERIA:** Web interface accessible without authentication

### 8.2 Concurrent Users

- [ ] Open web interface from 3 different devices simultaneously
- [ ] All devices should see live position updates
- [ ] Test motion commands from different devices (arbitration should prevent conflicts)

### 8.3 API Consistency

- [ ] Verify `/api/status` returns valid JSON
- [ ] Verify `/api/move` accepts JSON payload
- [ ] Test malformed JSON (should return 400 Bad Request)

---

## 9. Network & Communication Robustness

### 9.1 WiFi Reconnection

- [ ] Disconnect WiFi router power
- [ ] Verify controller attempts reconnection (check logs)
- [ ] Restore WiFi power
- [ ] **PASS CRITERIA:** Controller reconnects within 30 seconds

### 9.2 OTA Update Security

- [ ] Verify OTA password required for firmware update
- [ ] Test OTA with wrong password (should fail)
- [ ] Change OTA password and verify persistence

---

## 10. Long-Term Stability Testing

### 10.1 24-Hour Burn-In

- [ ] Run continuous motion for 24 hours (cycle all axes)
- [ ] Monitor for:
  - Watchdog resets (should be ZERO)
  - Memory leaks (`debug` heap should remain stable)
  - I2C errors (should be ZERO)
  - Encoder timeouts (should be ZERO)
  - Fault log growth (should be minimal)

### 10.2 Power Cycle Test

- [ ] Power cycle controller 100 times
- [ ] Verify clean boot every time (no corruption)
- [ ] Verify NVS data persists (calibration, faults, credentials)

---

## 11. Production Readiness Criteria

### Critical Requirements (Must Pass)

- [ ] **Physical E-Stop functional** (motor power physically cut)
- [ ] **Zero RS485 bus conflicts** (encoder + VFD coexist)
- [ ] **E-Stop latency < 50ms** (software E-Stop meets ISO 13849 PLd)
- [ ] **NVS flash wear protection active** (cooldown enforced)
- [ ] **Zero watchdog resets** (24-hour stability test)
- [ ] **Web authentication enabled** (no anonymous access)
- [ ] **Default passwords changed** (web + OTA)

### High Priority (Recommended)

- [ ] Position accuracy < ±0.05mm (100-cycle repeatability)
- [ ] Memory usage stable (no leaks after 24 hours)
- [ ] I2C errors < 1 per hour (bus recovery functional)
- [ ] Encoder deviation detection < 2 seconds
- [ ] WiFi reconnection < 30 seconds

### Optional Enhancements

- [ ] Modbus degraded mode (continue operation if VFD offline)
- [ ] HTTPS support (TLS encryption for web interface)
- [ ] Watchdog separation (Motion/PLC independent watchdogs)
- [ ] Telemetry export (Prometheus/InfluxDB metrics)

---

## 12. Regression Testing (After Code Changes)

### Quick Smoke Test (15 minutes)

1. Boot controller and check `info` output
2. Run `debug` and verify all subsystems healthy
3. Test single-axis move: `move X 10.0 20.0`
4. Trigger E-Stop and verify latency < 50ms
5. Check logs for errors

### Full Regression Test (2 hours)

1. Run all sections above (1-9)
2. Document any failures in GitHub issues
3. Re-test after fixes

---

## 13. Test Results Documentation

**Test Date:** _______________
**Firmware Version:** _______________
**Tester:** _______________

| Test Section | Status | Notes |
|-------------|--------|-------|
| 1. Pre-Deployment | ☐ Pass ☐ Fail | |
| 2. RS485 Bus Conflict | ☐ Pass ☐ Fail | |
| 3. NVS Flash Wear | ☐ Pass ☐ Fail | |
| 4. E-Stop Latency | ☐ Pass ☐ Fail | |
| 5. Memory Usage | ☐ Pass ☐ Fail | |
| 6. Motion Accuracy | ☐ Pass ☐ Fail | |
| 7. Safety Compliance | ☐ Pass ☐ Fail | |
| 8. Web Interface | ☐ Pass ☐ Fail | |
| 9. Network Robustness | ☐ Pass ☐ Fail | |
| 10. Long-Term Stability | ☐ Pass ☐ Fail | |

**Overall Production Readiness:** ☐ APPROVED ☐ REJECTED

**Signature:** _______________

---

## Related Documentation

- `docs/GEMINI_RS485_BUS_CONFLICT.md` - RS485 multiplexer architecture
- `docs/GEMINI_LOGGING_CRITICAL_SECTIONS.md` - Critical section safety analysis
- `docs/GEMINI_I2C_PRIORITY_ESTOP.md` - E-Stop latency analysis
- `docs/GEMINI_GHOST_TASK_ANALYSIS.md` - Memory optimization
- `ROADMAP.md` - Critical findings summary

---

## Test Automation (Future)

**Recommended Tools:**
- **Unity Test Framework** - Unit tests for motion control logic
- **Robot Framework** - Integration tests for web API
- **pytest** - Python scripts for hardware-in-the-loop testing
- **ESP-IDF Test Framework** - FreeRTOS task testing

**Continuous Integration:**
- Run unit tests on every commit (GitHub Actions)
- Run hardware tests nightly (dedicated test rig)
- Automated regression testing before release
