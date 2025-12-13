# Phase 5.5: Altivar 31 VFD Diagnostics Integration

## Status: Headers Created, Implementation Strategy Defined

### What's Been Done

1. **Configuration Keys** ✅
   - Added 11 config keys for VFD calibration and temperature thresholds
   - File: `include/config_keys.h`

2. **Header Files** ✅
   - `include/altivar31_modbus.h` - VFD Modbus query interface
   - `include/vfd_current_calibration.h` - Current calibration system

### Implementation Strategy

#### Phase 5.5a: VFD Modbus Interface
**Files to Create:**
- `src/altivar31_modbus.cpp` - Modbus queries for Altivar 31
- `src/vfd_current_calibration.cpp` - Calibration measurement system

**Altivar 31 Modbus Registers** (Verify against your VFD manual):
```
Register 0x0200 (512 decimal): Output Frequency
  - Data format: INT16, units 0.01 Hz
  - Example: 0x0BB8 (3000 decimal) = 30.00 Hz

Register 0x0203 (515 decimal): Drive Current
  - Data format: INT16, units 0.1 A
  - Example: 0x00C8 (200 decimal) = 20.0 A

Register 0x0205 (517 decimal): Drive Status
  - 0 = Idle
  - 1 = Running
  - 2 = Fault
  - 3 = Overheat

Register 0x0204 (516 decimal): Fault Code
  - 0 = No fault
  - Other values = Specific faults (see VFD manual)

Register 0x0210 (528 decimal): Heatsink Temperature (if available)
  - Data format: INT16, units °C
  - May not be available on all models
```

**Implementation Approach:**
1. Extend existing Modbus infrastructure (reuse jxk10_modbus pattern)
2. Create generic Modbus read function for any register/slave address
3. Query registers at 1Hz (in background telemetry task)
4. Cache values in `altivar31_state_t` structure

**Code Pattern** (similar to spindle_current_monitor.cpp):
```cpp
// Query VFD frequency every 1 second
if (millis() - last_query_ms > 1000) {
    float freq = altivar31ReadFrequency();  // Blocking query
    float current = altivar31ReadCurrent(); // Blocking query
    int16_t temp = altivar31ReadTemperature(); // -999 if unavailable
    last_query_ms = millis();
}
```

#### Phase 5.5b: Current Calibration CLI Command
**Files to Modify:**
- `src/cli_diag.cpp` - Add `calibrate_current` command

**CLI Command Workflow:**
```
$ calibrate_current

[VFD Calibration] Starting current baseline measurement

Step 1/3: IDLE BASELINE
  - Spin blade without cutting for 10 seconds
  - Press ENTER to start...
  [Measuring idle current...]
  Idle RMS: 7.2A, Peak: 9.1A
  Press ENTER for next step...

Step 2/3: STANDARD CUT BASELINE
  - Cut stone at standard speed for 10 seconds
  - Press ENTER to start...
  [Measuring standard cut...]
  Standard Cut RMS: 22.5A, Peak: 24.8A
  Press ENTER for next step...

Step 3/3: HEAVY LOAD (OPTIONAL)
  - Increase speed/load for 10 seconds (or press ENTER to skip)
  [Measuring heavy load...]
  Heavy Cut RMS: 28.3A, Peak: 31.2A

[Calibration Complete]
Stall Threshold: 37.4A (31.2A + 20%)
Configuration saved. Stall detection now active.
```

**Implementation:**
1. Create state machine for guided calibration
2. Show countdown timer during measurement
3. Update config after each phase
4. Confirm settings with operator

#### Phase 5.5c: Stall Detection Integration
**Files to Modify:**
- `src/encoder_motion_integration.cpp` - Add VFD current check

**Current Stall Detection Algorithm:**
```cpp
// BEFORE: Position lag only
if (encoder_position_error > THRESHOLD && time_in_error > 2000) {
    stall_detected = true;
}

// AFTER: Current + Position lag
float vfd_current = altivar31ReadCurrent();
float stall_threshold = vfdCalibrationGetThreshold();

if (vfd_current > stall_threshold && encoder_velocity ≈ 0 && time_in_error > 2000) {
    stall_detected = true;  // Fast detection via current
}
```

**Benefit:** Current spike is detected instantly vs. position lag (50-200ms delay)

#### Phase 5.5d: Dashboard Updates
**Files to Modify:**
- `spiffs/dashboard.html` - Add VFD status panel
- `src/dashboard_metrics.cpp` - Include VFD data in WebSocket stream

**Dashboard Additions:**
- Real-time motor current (graph over time)
- VFD status (Running/Idle/Fault)
- Heatsink temperature with warning/critical levels
- Current calibration status (✓ Calibrated or ✗ Needs Calibration)
- Stall threshold display

---

## Missing Implementation Details

These must be filled in from YOUR Altivar 31 manual:

1. **Exact Register Addresses**
   - Confirm 0x0200, 0x0203, 0x0205, 0x0204, 0x0210
   - Some Altivar 31 variants may use different addresses
   - Firmware will need conditional compilation if variants differ

2. **Slave Address**
   - What is your VFD Modbus slave address? (default is usually 1)
   - Configured during VFD commissioning

3. **Baud Rate**
   - Must match RS485 shared bus baud rate
   - Currently 9600 bps (same as encoder)

4. **Temperature Register Availability**
   - Not all Altivar 31 models expose heatsink temp via Modbus
   - Register 0x0210 may not exist on your variant
   - Code will gracefully handle -999 (unavailable) response

---

## Next Steps

1. **Verify your VFD manual** for:
   - Exact Modbus register addresses for current, frequency, status, temperature
   - Confirm slave address and baud rate
   - Note any variant-specific differences

2. **Provide these details** and I will:
   - Implement `altivar31_modbus.cpp` with correct registers
   - Implement `vfd_current_calibration.cpp` with calibration workflow
   - Integrate into stall detection
   - Update dashboard

3. **Testing procedure**:
   - Upload firmware
   - Run `calibrate_current` command with blade spinning
   - Verify stall detection on encoder + VFD current correlation

---

## File Dependencies

```
include/
  ├─ altivar31_modbus.h          (Created ✅)
  ├─ vfd_current_calibration.h   (Created ✅)
  └─ config_keys.h               (Updated ✅)

src/
  ├─ altivar31_modbus.cpp        (Needs Implementation)
  ├─ vfd_current_calibration.cpp (Needs Implementation)
  ├─ cli_diag.cpp                (Needs calibrate_current command)
  ├─ encoder_motion_integration.cpp (Needs stall detection update)
  ├─ dashboard_metrics.cpp       (Needs VFD data collection)
  └─ tasks_telemetry.cpp         (Needs VFD query calls)

spiffs/
  └─ dashboard.html              (Needs VFD status display)
```

---

## Estimated Effort

- **Phase 5.5a (Modbus Interface)**: 2-3 hours (depends on manual verification)
- **Phase 5.5b (CLI Calibration)**: 1-2 hours
- **Phase 5.5c (Stall Integration)**: 30 minutes
- **Phase 5.5d (Dashboard)**: 1 hour

Total: ~5 hours of implementation after manual review
