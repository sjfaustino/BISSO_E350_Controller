# Professional Stone Bridge Saw Controller Upgrade Plan

**Document Version:** 1.0
**Target Hardware:** KC868-A16/A32 (ESP32-S3)
**Current Firmware:** Gemini v3.5.x
**Analysis Date:** 2025-12-08

---

## Executive Summary

The current BISSO E350 controller provides **excellent foundation-level CNC motion control** with:
- ✅ Robust 4-axis closed-loop positioning
- ✅ Industrial-grade safety system with E-stop and fault logging
- ✅ Professional code architecture with FreeRTOS
- ✅ Comprehensive diagnostics and configuration management
- ✅ Web interface and CLI for operation/maintenance

However, to qualify as a **professional stone bridge saw controller**, several **critical stone-cutting-specific features** are missing. Stone cutting has unique requirements that differ from general CNC machining.

**Priority Rating:**
🔴 **CRITICAL** - Required for safe stone cutting operations
🟡 **HIGH** - Required for professional/production use
🟢 **MEDIUM** - Quality-of-life improvements
⚪ **LOW** - Nice-to-have features

---

## 1. CRITICAL HARDWARE GAPS (🔴 Must Address)

### 1.1 Water/Coolant System (🔴 CRITICAL)
**Current State:** ❌ No water system control or monitoring
**Problem:** Stone cutting **REQUIRES** continuous water flow. Without it:
- Blade overheats and fails within seconds
- Toxic silica dust is generated
- Material can crack from thermal stress

**Required Additions:**
| Component | Purpose | Hardware Interface | Notes |
|-----------|---------|-------------------|-------|
| **Water Pump Relay** | ON/OFF control | Y9 (spare relay output) | 10A relay capacity adequate |
| **Flow Sensor** | Verify water flowing | X9 (spare digital input) | Pulse-type flow meter (Hall effect) |
| **Low Water Level Switch** | Tank empty detection | X10 (spare digital input) | Float switch, NC contact |
| **Water Pressure Sensor** (optional) | Verify sufficient pressure | IN3 or IN4 (0-5V analog) | 0-5V pressure transducer |

**Safety Interlock Logic:**
```cpp
// Pseudo-code for water safety
if (!waterFlowDetected() || lowWaterLevel()) {
    // CRITICAL: Cannot run blade without water
    stopAllMotion();
    disableSpindle();
    triggerAlarm("WATER SYSTEM FAILURE");
    // Require manual reset after refilling
}
```

**Implementation Effort:** 🟡 MEDIUM (2-3 days)
- New module: `src/water_system.cpp` and `include/water_system.h`
- Add to safety task (5ms loop)
- Configuration keys: `water_flow_timeout_ms`, `water_flow_min_pulses`

---

### 1.2 Spindle/Blade Speed Control (🔴 CRITICAL)
**Current State:** ⚠️ Only 3 fixed speeds via digital relay outputs (SLOW/MED/FAST)
**Problem:** Professional stone cutting requires:
- Variable speed for different materials (granite 3500 RPM, marble 2800 RPM)
- Ramp-up/ramp-down to prevent blade damage
- Speed feedback for verification

**Required Additions:**
| Component | Purpose | Hardware Interface | Implementation |
|-----------|---------|-------------------|----------------|
| **0-10V Analog Output** | Variable spindle speed | **Requires external DAC module** | MCP4725 over I2C (0x60) |
| **Tachometer Input** | Actual blade RPM | IN1 or IN2 (0-20mA current loop) | Frequency-to-current converter |
| **Spindle Enable Relay** | Master ON/OFF | Y10 (spare relay) | Safety cutoff |

**Hardware Limitation:** ⚠️ KC868-A16/A32 **does not have** built-in analog outputs!

**Solution Options:**
1. **Add I2C DAC Module (RECOMMENDED):**
   - MCP4725 12-bit DAC ($3-5)
   - I2C address 0x60 (no conflict with existing 0x21/0x22/0x24)
   - 0-5V output → external op-amp to scale to 0-10V for VFD

2. **Use PWM with RC Filter:**
   - Use HT3 (GPIO32) for PWM output
   - Add external RC filter (R=10kΩ, C=10µF)
   - Lower resolution but no additional hardware

**Software Architecture:**
```cpp
// New spindle control module
class SpindleController {
    void setSpeed(uint16_t rpm);        // Target RPM
    uint16_t getActualSpeed();          // Read from tachometer
    bool isAtSpeed(uint16_t tolerance); // Verify reached target
    void rampToSpeed(uint16_t target, uint16_t accel_rpm_s);
    void emergencyStop();               // Immediate brake
};
```

**Implementation Effort:** 🔴 HIGH (5-7 days)
- Hardware: Add MCP4725 DAC, wire to VFD analog input
- Software: New `spindle_control.cpp` module
- Integration: Add to motion planner for coordinated moves
- Testing: RPM calibration and verification

---

### 1.3 Motor Load Sensing (🔴 CRITICAL for Material)
**Current State:** ❌ No current/power monitoring
**Problem:**
- Cannot detect blade binding (dangerous!)
- Cannot optimize feed rate for material hardness
- No overload protection
- Cannot estimate blade wear

**Required Additions:**
| Component | Purpose | Hardware Interface | Specs |
|-----------|---------|-------------------|-------|
| **AC Current Sensor** | Monitor spindle motor current | IN1 (0-20mA) or IN3 (0-5V) | SCT-013 or similar, 0-30A range |
| **Current Transmitter** | Convert AC to DC signal | External module | 4-20mA output proportional to load |

**Available Analog Inputs (Currently Unused!):**
- **IN1, IN2:** 0-20mA current loop (12-bit ADC)
- **IN3, IN4:** 0-5V analog (12-bit ADC)

**Usage:**
```cpp
// Read motor load percentage
float getSpindleLoad() {
    int raw = analogRead(PIN_IN3);  // GPIO39
    float voltage = raw * (5.0 / 4095.0);
    float current_amps = voltage * 6.0;  // Scale factor for sensor
    float load_percent = (current_amps / MOTOR_RATED_CURRENT) * 100.0;
    return load_percent;
}

// Adaptive feed control
if (getSpindleLoad() > 85.0) {
    reduceFeedRate(0.8);  // Slow down 20%
    logWarning("High cutting load - reducing feed");
}
```

**Implementation Effort:** 🟡 MEDIUM (3-4 days)
- Hardware: Install current sensor on spindle motor L1 phase
- Software: New `load_monitor.cpp` module
- Calibration: Determine load thresholds for materials
- Integration: Add adaptive feed to motion planner

---

### 1.4 Material Sensing (🟡 HIGH Priority)
**Current State:** ❌ No material detection
**Problem:** Operator can start cutting cycle with no material loaded

**Required Additions:**
| Sensor | Purpose | Interface | Type |
|--------|---------|-----------|------|
| **Material Presence Sensor** | Detect slab on table | X11 (digital input) | Inductive proximity sensor |
| **Material Thickness Sensor** (optional) | Auto-detect slab thickness | IN4 (0-5V analog) | Ultrasonic distance sensor |

**Implementation Effort:** 🟢 LOW (1 day)

---

## 2. CRITICAL SOFTWARE GAPS (🔴 Must Implement)

### 2.1 Water Safety Interlock (🔴 CRITICAL)
**Status:** ❌ Not implemented
**Required Logic:**
1. Water pump MUST start 5 seconds before spindle enable
2. Flow sensor MUST detect flow within 10 seconds or alarm
3. If flow stops during cutting → immediate spindle stop + motion halt
4. Low water level → prevent start, alarm if running

**Files to Modify:**
- `src/safety.cpp` - Add `SAFETY_WATER_FAILURE` fault type
- `include/safety.h` - Add water-related safety checks
- New: `src/water_system.cpp` - Water control logic

**Implementation:** 3 days

---

### 2.2 Adaptive Feed Control (🔴 CRITICAL for Quality)
**Status:** ❌ Not implemented
**Current Behavior:** Feed rate is constant regardless of load

**Required Features:**
```cpp
// Adaptive feed algorithm
void adaptiveFeedControl() {
    float load = getSpindleLoad();

    if (load > 90%) {
        feedOverride = 0.7;  // Reduce to 70%
        logWarning("Excessive load - reducing feed");
    }
    else if (load > 80%) {
        feedOverride = 0.85; // Reduce to 85%
    }
    else if (load < 40% && feedOverride < 1.0) {
        feedOverride = min(1.0, feedOverride + 0.05); // Gradually increase
    }

    // Apply override to current feed rate
    currentFeedRate = targetFeedRate * feedOverride;
}
```

**Benefits:**
- Prevents blade binding/breakage
- Maximizes productivity (automatically finds optimal feed)
- Extends blade life
- Consistent cut quality across materials

**Files to Create:**
- `src/adaptive_feed.cpp` - Feed optimization logic
- `include/adaptive_feed.h` - API

**Implementation:** 5 days

---

### 2.3 Material Database & Cut Parameters (🟡 HIGH)
**Status:** ❌ Not implemented
**Problem:** Operator must manually determine speeds/feeds for each material

**Required Database:**
```cpp
struct MaterialProfile {
    char name[32];              // "Granite - Black Galaxy"
    uint16_t spindle_rpm;       // 3500
    float feed_rate_mm_min;     // 800
    uint8_t water_flow_percent; // 100
    uint8_t max_depth_per_pass; // 30 mm
    float load_limit_percent;   // 85.0
};

// Pre-defined materials
const MaterialProfile materials[] = {
    {"Granite - General",     3500, 800,  100, 30, 85.0},
    {"Marble - Soft",         2800, 1200, 80,  50, 75.0},
    {"Marble - Hard",         3000, 900,  100, 40, 80.0},
    {"Quartz - Engineered",   3200, 600,  100, 25, 90.0},
    {"Limestone",             2500, 1500, 70,  60, 70.0},
    {"Slate",                 3000, 1000, 90,  35, 75.0},
};
```

**User Interface:**
- Add to web UI: Material selection dropdown
- Add CLI command: `material set granite`
- Store in NVS: Last used material per job

**Implementation:** 4 days

---

### 2.4 Multi-Pass Cutting for Thick Materials (🟡 HIGH)
**Status:** ❌ Not implemented
**Current Limitation:** Single-pass cutting only

**Problem:** Thick slabs (>30mm) cannot be cut safely in one pass

**Required G-Code Extensions:**
```gcode
; Example: Cut 80mm thick granite in 3 passes
G90           ; Absolute mode
M6 T1         ; Select "Granite" tool/material profile
G0 X0 Y0      ; Position to start
G1 Z-30 F800  ; Pass 1: 30mm depth
G1 X500 Y300  ; Cut profile
G0 Z0         ; Retract
G1 Z-60 F800  ; Pass 2: 60mm depth
G1 X500 Y300  ; Cut profile
G0 Z0         ; Retract
G1 Z-80 F800  ; Pass 3: 80mm depth (full through)
G1 X500 Y300  ; Cut profile
G0 Z0         ; Done
```

**Automatic Multi-Pass Planner:**
```cpp
// Break deep cuts into multiple passes
void planMultiPassCut(float target_depth, MaterialProfile mat) {
    int num_passes = ceil(abs(target_depth) / mat.max_depth_per_pass);
    float depth_per_pass = target_depth / num_passes;

    for (int pass = 1; pass <= num_passes; pass++) {
        float current_depth = depth_per_pass * pass;
        // Generate motion commands for this pass
        planCutPass(current_depth, mat.feed_rate_mm_min);
        // Add retract between passes
        if (pass < num_passes) {
            planRetract();
        }
    }
}
```

**Implementation:** 6 days

---

### 2.5 Corner Slowdown & Acceleration Limits (🟡 HIGH)
**Status:** ⚠️ Partial - has speed profiles but no corner detection
**Problem:** Sharp corners at full speed cause:
- Blade deflection → poor cut quality
- Increased blade wear
- Chipping on inside corners

**Required Algorithm:**
```cpp
// Detect sharp corners and reduce feed
void cornerSlowdown(GCodeMove next_move) {
    float angle = calculateCornerAngle(current_direction, next_move.direction);

    if (angle > 45.0) {  // Sharp corner
        float slowdown_factor = map(angle, 45, 90, 1.0, 0.4);
        next_move.feed_rate *= slowdown_factor;
        logInfo("Corner %0.1f° - reducing to %0.1f%%", angle, slowdown_factor * 100);
    }
}
```

**Implementation:** 3 days

---

### 2.6 Blade/Tool Life Tracking (🟡 HIGH)
**Status:** ❌ Not implemented
**Problem:** No way to track blade usage or predict replacement

**Required Tracking:**
```cpp
struct BladeLifeData {
    uint32_t total_cut_time_sec;       // Actual cutting time
    float total_cut_length_meters;     // Linear distance cut
    uint32_t total_starts;             // Start/stop cycles (wear)
    float total_load_integral;         // Cumulative load (indicates wear)
    uint32_t installation_timestamp;   // When blade was installed
    char blade_type[32];               // "14in Diamond - Granite"
    uint16_t replacement_threshold_hrs; // 40 hours typical
};

// Track during operation
void updateBladeLife() {
    if (isSpindleRunning() && isAxisMoving()) {
        blade.total_cut_time_sec++;
        blade.total_cut_length_meters += getInstantaneousSpeed() / 60.0;
        blade.total_load_integral += getSpindleLoad();
    }
}

// Maintenance alert
void checkBladeLife() {
    float hours = blade.total_cut_time_sec / 3600.0;
    if (hours > blade.replacement_threshold_hrs * 0.9) {
        logWarning("Blade at %0.1f hours - approaching replacement", hours);
    }
}
```

**User Interface:**
- CLI: `blade info` - show usage statistics
- CLI: `blade reset` - reset after blade change
- Web UI: Maintenance page with blade life gauge

**Implementation:** 3 days

---

## 3. SAFETY ENHANCEMENTS (🔴 CRITICAL)

### 3.1 Guard Interlock Switches (🔴 CRITICAL)
**Status:** ❌ Not implemented
**Requirement:** OSHA/CE requires blade guard interlock

| Switch | Function | Interface | Logic |
|--------|----------|-----------|-------|
| **Front Guard** | Detect guard open | X12 (digital input) | NC contact, motion inhibit |
| **Rear Access** | Detect access door | X13 (digital input) | NC contact, spindle inhibit |

**Safety Logic:**
```cpp
if (guardOpen() && (isSpindleRunning() || isMotionActive())) {
    emergencyStop();
    triggerAlarm("GUARD INTERLOCK VIOLATION");
    // Require manual reset
}
```

**Implementation:** 2 days

---

### 3.2 Two-Hand Start Control (🟡 HIGH - OSHA Recommended)
**Status:** ❌ Not implemented
**Purpose:** Prevent operator hands near blade during start

**Interface:**
- X14: Left start button
- X15: Right start button

**Logic:**
```cpp
// Both buttons must be pressed within 0.5 seconds
bool twoHandStart() {
    if (leftButtonPressed() && !rightButtonPressed()) {
        uint32_t timeout = millis() + 500;
        while (millis() < timeout) {
            if (rightButtonPressed()) return true;
        }
        return false;  // Timeout
    }
    return false;
}
```

**Implementation:** 2 days

---

### 3.3 Blade Overload Detection (🔴 CRITICAL)
**Status:** ❌ Not implemented (requires load sensing from 1.3)

**Logic:**
```cpp
#define OVERLOAD_THRESHOLD 95.0  // % of rated motor current
#define OVERLOAD_TIME_MS   2000  // Sustained for 2 seconds

void checkOverload() {
    static uint32_t overload_start = 0;

    if (getSpindleLoad() > OVERLOAD_THRESHOLD) {
        if (overload_start == 0) {
            overload_start = millis();
        }
        else if (millis() - overload_start > OVERLOAD_TIME_MS) {
            // CRITICAL: Blade is binding or stalled
            emergencyStop();
            triggerAlarm("SPINDLE OVERLOAD - CHECK BLADE");
            overload_start = 0;
        }
    } else {
        overload_start = 0;  // Reset
    }
}
```

**Implementation:** 1 day (depends on 1.3)

---

## 4. PRODUCTION FEATURES (🟢 MEDIUM Priority)

### 4.1 Job Time Estimation (🟢 MEDIUM)
**Status:** ❌ Not implemented

**Algorithm:**
```cpp
float estimateJobTime(GCodeFile* job, MaterialProfile mat) {
    float total_time = 0;
    total_time += 5.0;  // Water pump startup delay
    total_time += mat.spindle_rpm / 100.0;  // Spindle ramp-up (rough estimate)

    for (GCodeMove move : job->moves) {
        if (move.type == G0) {  // Rapid
            total_time += move.distance / RAPID_SPEED_MM_MIN * 60.0;
        } else {  // G1 cutting
            total_time += move.distance / mat.feed_rate_mm_min * 60.0;
        }
    }

    total_time += 10.0;  // Spindle shutdown, cleanup
    return total_time;
}
```

**Display:** Show estimate before job start on web UI

**Implementation:** 2 days

---

### 4.2 Production Statistics & Job History (🟢 MEDIUM)
**Status:** ❌ Not implemented

**Track Per Job:**
- Job name, start/end timestamp
- Material used
- Actual vs. estimated time
- Blade usage during job
- Alarm/fault events
- Cut length, cut area

**Storage:** NVS or SD card (if added)

**Implementation:** 4 days

---

### 4.3 Automatic Tool/Blade Offset Calibration (🟢 MEDIUM)
**Status:** ⚠️ Manual calibration only

**Feature:** Touch-off probe for automatic Z-zero

**Implementation:** 3 days (requires touch probe sensor on X16)

---

## 5. USER INTERFACE ENHANCEMENTS

### 5.1 Web UI - Material Selection Page (🟡 HIGH)
**Add to existing Web UI** (`data/index.html`):

```html
<!-- New section -->
<div class="material-selector">
  <h3>Material & Cut Parameters</h3>
  <select id="materialSelect">
    <option value="granite">Granite - General</option>
    <option value="marble_soft">Marble - Soft</option>
    <option value="marble_hard">Marble - Hard</option>
    <option value="quartz">Quartz - Engineered</option>
  </select>

  <div class="parameters">
    <label>Spindle RPM: <span id="rpm">3500</span></label>
    <label>Feed Rate: <span id="feed">800</span> mm/min</label>
    <label>Max Depth/Pass: <span id="depth">30</span> mm</label>
  </div>

  <button onclick="applyMaterial()">Apply Settings</button>
</div>
```

**Implementation:** 2 days

---

### 5.2 Web UI - Blade Life Dashboard (🟢 MEDIUM)
**Visual Indicators:**
- Circular gauge showing % blade life remaining
- Cut time counter
- Cut length meter
- "Replace Blade" button to reset

**Implementation:** 1 day

---

### 5.3 CLI - Material Management Commands (🟡 HIGH)
**New Commands:**
```bash
> material list                    # Show all materials
> material select granite          # Set active material
> material info                    # Show current parameters
> material custom 3200 750 100 28  # Custom: RPM, Feed, Water%, Depth
```

**Implementation:** 1 day

---

## 6. G-CODE EXTENSIONS FOR STONE CUTTING

### 6.1 M-Code Extensions (🟡 HIGH)
**Implement Stone-Specific M-Codes:**

| Code | Function | Example | Description |
|------|----------|---------|-------------|
| **M3** | Spindle ON (CW) | `M3 S3500` | Start spindle at 3500 RPM |
| **M5** | Spindle OFF | `M5` | Stop spindle |
| **M6** | Tool/Material Change | `M6 T1` | Load material profile #1 |
| **M7** | Water ON | `M7` | Enable coolant |
| **M9** | Water OFF | `M9` | Disable coolant |
| **M0** | Program Pause | `M0` | Wait for user (blade inspection) |
| **M30** | Program End | `M30` | Stop spindle, water, return home |

**Current Status:** G-code parser supports G0/G1/G90/G91/G92 only

**Implementation:** 3 days

---

## 7. IMPLEMENTATION ROADMAP

### Phase 1: CRITICAL SAFETY (2-3 weeks)
**Goal:** Make system safe for stone cutting operations

| Task | Priority | Effort | Dependencies |
|------|----------|--------|--------------|
| 1. Add water flow sensor + relay | 🔴 | 3 days | Hardware install |
| 2. Implement water safety interlock | 🔴 | 2 days | Task 1 |
| 3. Add guard interlock switches | 🔴 | 2 days | Hardware install |
| 4. Add motor load sensing (current sensor) | 🔴 | 4 days | Hardware install, ADC config |
| 5. Implement overload detection | 🔴 | 1 day | Task 4 |
| 6. Add spindle control (DAC + relay) | 🔴 | 5 days | MCP4725 module, VFD wiring |

**Deliverable:** System can safely cut stone with water interlock and overload protection

---

### Phase 2: PROFESSIONAL FEATURES (3-4 weeks)
**Goal:** Add production-ready features

| Task | Priority | Effort | Dependencies |
|------|----------|--------|--------------|
| 7. Material database implementation | 🟡 | 4 days | - |
| 8. Adaptive feed control | 🟡 | 5 days | Task 4 (load sensing) |
| 9. Multi-pass cutting planner | 🟡 | 6 days | - |
| 10. Corner slowdown algorithm | 🟡 | 3 days | - |
| 11. M-code extensions (M3/M5/M6/M7/M9) | 🟡 | 3 days | Task 6 (spindle), Task 2 (water) |
| 12. Web UI - Material selector | 🟡 | 2 days | Task 7 |
| 13. CLI - Material commands | 🟡 | 1 day | Task 7 |

**Deliverable:** Professional stone cutting with optimized parameters

---

### Phase 3: PRODUCTION TRACKING (1-2 weeks)
**Goal:** Track usage and maintenance

| Task | Priority | Effort | Dependencies |
|------|----------|--------|--------------|
| 14. Blade life tracking | 🟡 | 3 days | - |
| 15. Job time estimation | 🟢 | 2 days | - |
| 16. Production statistics | 🟢 | 4 days | - |
| 17. Web UI - Blade dashboard | 🟢 | 1 day | Task 14 |

**Deliverable:** Full production tracking and maintenance scheduling

---

### Phase 4: ADVANCED FEATURES (2-3 weeks)
**Goal:** Premium features for high-end installations

| Task | Priority | Effort | Dependencies |
|------|----------|--------|--------------|
| 18. Two-hand start control | 🟡 | 2 days | Hardware install |
| 19. Material thickness auto-detect | 🟢 | 2 days | Ultrasonic sensor |
| 20. Automatic Z-zero touch probe | 🟢 | 3 days | Touch probe sensor |
| 21. Vibration monitoring (optional) | ⚪ | 5 days | Accelerometer module |

---

## 8. HARDWARE SHOPPING LIST

### Essential Components (Phase 1)
| Component | Qty | Est. Cost | Purpose |
|-----------|-----|-----------|---------|
| **Hall Effect Flow Sensor** (YF-S201) | 1 | $8 | Water flow detection |
| **Float Switch** (PP liquid level) | 1 | $5 | Low water level |
| **AC Current Sensor** (SCT-013-030) | 1 | $12 | Motor load sensing |
| **4-20mA Transmitter Module** | 1 | $15 | Convert AC current to DC signal |
| **MCP4725 DAC Module** (I2C) | 1 | $5 | Spindle speed control (0-5V) |
| **Op-Amp Circuit** (0-10V scaler) | 1 | $8 | Scale 0-5V to 0-10V for VFD |
| **Magnetic Proximity Sensor** (M12) | 2 | $30 | Guard interlocks |
| **Inductive Proximity Sensor** (M18) | 1 | $15 | Material presence |
| **Relay Module** (5V trigger) | 1 | $6 | Water pump control |
| **Wire, connectors, terminals** | - | $30 | Installation |

**Phase 1 Total:** ~$134

### Optional Components (Phases 2-4)
| Component | Qty | Est. Cost | Purpose |
|-----------|-----|-----------|---------|
| **Ultrasonic Distance Sensor** (A02YYUW) | 1 | $12 | Material thickness |
| **Touch Probe** (NC contact) | 1 | $25 | Auto Z-zero |
| **Pressure Transducer** (0-5V) | 1 | $35 | Water pressure |
| **Tachometer Sensor** (NPN) | 1 | $18 | Spindle RPM feedback |
| **Two-Hand Buttons** (OSHA compliant) | 1 set | $45 | Safety start |

**Optional Total:** ~$135

**Grand Total Hardware:** ~$269

---

## 9. CURRENT SYSTEM STRENGTHS (Keep As-Is)

✅ **Excellent Foundation:**
1. **Robust motion control** - Closed-loop with WJ66 encoders is production-grade
2. **FreeRTOS architecture** - Professional task management
3. **Safety system** - E-stop, soft limits, stall detection well-implemented
4. **Fault logging** - Comprehensive diagnostics in place
5. **Configuration system** - NVS persistence, validation, calibration
6. **Web interface** - Already has real-time position display
7. **Performance profiling** - Just added, will help optimize new features
8. **Code quality** - Clean, well-documented, follows best practices

**DO NOT REINVENT:** The core motion, safety, and configuration systems are excellent. Build on this foundation.

---

## 10. RISK ASSESSMENT

### High-Risk Items (Require Testing/Validation)
1. **Water interlock reliability** - False positives could stop production
   - **Mitigation:** Adjustable timeout, redundant sensors

2. **Load sensing accuracy** - Incorrect thresholds could damage blade or material
   - **Mitigation:** Extensive testing with different materials, calibration procedure

3. **Spindle speed control** - Incorrect RPM could cause blade failure
   - **Mitigation:** RPM feedback verification, safety limits in firmware

4. **I2C bus congestion** - Adding MCP4725 DAC to existing PCF8574 devices
   - **Mitigation:** I2C bus is currently only ~20% utilized (50ms cycle), plenty of headroom

---

## 11. TESTING & VALIDATION PLAN

### Phase 1 Testing (Safety Features)
- [ ] Water flow sensor triggers alarm when flow stops
- [ ] Water flow sensor does NOT false-trigger with normal flow
- [ ] Low water level prevents spindle start
- [ ] Guard interlock stops motion within 100ms
- [ ] Overload detection stops blade before damage
- [ ] All safety interlocks survive 1000 cycle test

### Phase 2 Testing (Production Features)
- [ ] Adaptive feed maintains 75-85% load across 10m cut
- [ ] Material profiles produce acceptable cut quality
- [ ] Multi-pass cuts complete without errors
- [ ] Corner slowdown prevents chipping on 90° corners
- [ ] M-codes execute correctly in sequence

### Phase 3 Testing (Tracking)
- [ ] Blade life tracking accumulates correctly over 8-hour shift
- [ ] Job time estimates within ±10% of actual
- [ ] Statistics persist across power cycles

---

## 12. REGULATORY COMPLIANCE

### Required for CE/OSHA Certification
- ✅ E-stop (implemented)
- ✅ Soft limits (implemented)
- 🔴 Guard interlocks (MUST ADD)
- 🔴 Water safety interlock (MUST ADD for stone)
- 🟡 Two-hand start (recommended)
- 🟡 Overload protection (recommended)

**Note:** Consult with safety engineer before production deployment.

---

## 13. CONCLUSION & NEXT STEPS

### Current System Grade: **B+ (Very Good Foundation)**
The BISSO E350 controller has **excellent motion control, safety architecture, and code quality**. It's 100% production-ready for general CNC applications.

### Stone Bridge Saw Grade: **D (Incomplete)**
**Missing critical stone-cutting-specific features:**
- No water system (absolute requirement)
- No load sensing (quality/safety issue)
- Fixed speed only (limits material compatibility)

### Recommended Action Plan:
1. **Immediate:** Implement Phase 1 (water + safety interlocks) - 2-3 weeks
2. **Short-term:** Implement Phase 2 (professional features) - 3-4 weeks
3. **Medium-term:** Implement Phase 3 (production tracking) - 1-2 weeks
4. **Long-term:** Phase 4 as needed based on customer requirements

**Total Implementation Time:** 8-12 weeks for full professional system

### Investment Required:
- **Hardware:** ~$269
- **Engineering Time:** ~350-450 hours (2-3 months full-time)
- **Testing/Validation:** ~80 hours
- **Documentation:** ~40 hours

**ROI:** Transforms general CNC controller into **professional stone cutting system** with adaptive control, safety interlocks, and production tracking.

---

**Document Prepared By:** Claude (AI Code Assistant)
**Review Recommended:** Senior Controls Engineer, Safety Engineer
**Next Review Date:** After Phase 1 completion
