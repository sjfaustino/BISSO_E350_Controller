# Professional Stone Bridge Saw Controller - Realistic Upgrade Plan

**Document Version:** 3.0 (Corrected for Actual System Architecture)
**Target Hardware:** KC868-A16/A32 (ESP32-S3)
**Current Firmware:** Gemini v3.5.x
**Analysis Date:** 2025-12-08

---

## System Architecture - Actual Understanding

### **Critical Architectural Constraints**

**Your system is fundamentally different from a modern CNC:**

1. **Single VFD for ALL axes** - One motor drives X, Y, Z, and A through mechanical switching
2. **Sequential motion ONLY** - Can only move ONE axis at a time, never simultaneously
3. **3 discrete speeds** - SLOW/MED/FAST are fixed by legacy PLC, no smooth adjustment
4. **No VFD access** - Controller only sends digital signals to PLC (axis select, direction, speed)
5. **PLC controls VFD** - Legacy PLC handles motor ramping, you just select which axis/speed
6. **Closed-loop via encoders** - WJ66 provides position feedback for stopping at target

**This is a "PLC Emulation Bridge" system, NOT a traditional CNC controller.**

### What the Controller Actually Does

```
ESP32 → I2C Expanders → Legacy PLC → VFD → Single Motor → Mechanical Switching → Active Axis

Controller Outputs:
- Axis Select (X/Y/Z/A) - which axis to engage
- Direction (+/-)
- Speed Profile (1/2/3) - SLOW/MED/FAST

Controller Reads:
- WJ66 Encoders - current position of all axes
- PLC "Consenso" signals - axis ready to move
- Safety inputs - E-stop, guards, etc.
```

---

## Critical Limitations (Cannot Change These)

### ❌ What You CANNOT Do

1. **Cannot smoothly adjust speed during motion**
   - Only 3 discrete speeds (SLOW/MED/FAST)
   - Switching speeds mid-move risks blade/stone damage
   - Must stop, change speed, restart

2. **Cannot move multiple axes simultaneously**
   - Single VFD drives one axis at a time via mechanical switching
   - No diagonal/circular motion possible
   - All motion is point-to-point

3. **Cannot control VFD acceleration/deceleration**
   - Legacy PLC controls ramping
   - No smooth trajectory planning possible

4. **Cannot implement traditional CNC features**
   - No interpolated G0/G1 moves (X+Y simultaneously)
   - No feed rate override (only 3 discrete speeds)
   - No corner slowdown (no corners - sequential motion only)
   - No adaptive feed control (can't smoothly adjust)

### ✅ What You CAN Do

1. **Select which axis moves** - Control axis selection logic
2. **Monitor position accurately** - WJ66 encoders provide precise feedback
3. **Implement safety interlocks** - Stop motion based on guards, limits, etc.
4. **Sequence complex multi-step moves** - Program series of sequential moves
5. **Track usage and statistics** - Monitor cutting time, distances, cycles
6. **Provide operator guidance** - Recommend speeds for materials
7. **Verify motion accuracy** - Compare target vs actual position
8. **Pre-move validation** - Check conditions before starting motion

---

## Realistic Features for Professional Bridge Saw

Given the architectural constraints, here's what actually adds value:

---

## 1. SAFETY FEATURES (🔴 CRITICAL)

### 1.1 Guard Interlock System (🔴 HIGHEST PRIORITY)

**Requirement:** OSHA/CE requires blade guard interlocks

#### Hardware

| Component | Purpose | Interface | Cost |
|-----------|---------|-----------|------|
| **Magnetic Proximity Sensors** (M12, NC) | Detect guard position | X12, X13 | $30 |

**Wiring:**
```
Front Blade Guard → Magnetic Sensor → X12 (GPIO12)
Rear Access Panel → Magnetic Sensor → X13 (GPIO13)
```

#### Software Logic

```cpp
// In safety.cpp
bool safetyCheckGuardsInterlocked() {
    bool front_guard_closed = !digitalRead(PIN_X12);  // NC - LOW when closed
    bool rear_guard_closed = !digitalRead(PIN_X13);

    return (front_guard_closed && rear_guard_closed);
}

void safetyUpdate() {
    // Check guards every 5ms
    if (!safetyCheckGuardsInterlocked()) {
        if (motionIsMoving()) {
            // CRITICAL: Stop motion immediately
            motionEmergencyStop();
            safetyTriggerAlarm("GUARD OPEN - Motion stopped");
        }
        // Inhibit new motion commands
        motion_inhibit_flags |= INHIBIT_GUARD_OPEN;
    } else {
        motion_inhibit_flags &= ~INHIBIT_GUARD_OPEN;
    }
}
```

**Benefits:**
- 🎯 OSHA/CE compliance
- 🎯 Prevents operator injury
- 🎯 Insurance requirement
- 🎯 Immediate motion stop if guard opened during cut

**Implementation:** 2 days (hardware + software)

---

### 1.2 Material Presence Detection (🟡 HIGH)

**Purpose:** Prevent starting cutting cycle with no material on table

#### Hardware

| Component | Interface | Cost |
|-----------|-----------|------|
| **Inductive Proximity Sensor** (M18, PNP-NO) | X11 | $15 |

**Logic:**
```cpp
bool materialPresent() {
    return digitalRead(PIN_X11);  // HIGH when metal table detected
}

// Before starting job
if (!materialPresent()) {
    Serial.println("[ERROR] No material detected - load material first");
    return false;
}
```

**Implementation:** 1 day

---

### 1.3 Two-Hand Start Control (🟡 OSHA Recommended)

**Purpose:** Operator's hands must be clear of blade area during start

#### Hardware

| Component | Interface | Cost |
|-----------|-----------|------|
| **Start Buttons** (2x momentary, NO) | X14, X15 | $20 |

**Logic:**
```cpp
bool twoHandStartCheck() {
    if (!digitalRead(PIN_X14) || !digitalRead(PIN_X15)) {
        return false;  // One or both not pressed
    }

    // Both pressed - verify simultaneous within 500ms
    uint32_t start_time = millis();
    while (millis() - start_time < 500) {
        if (!digitalRead(PIN_X14) || !digitalRead(PIN_X15)) {
            return false;  // One released
        }
    }
    return true;  // Both held for 500ms
}
```

**Implementation:** 2 days

---

## 2. PRODUCTION TRACKING (🟢 HIGH VALUE)

### 2.1 Blade Life Tracking (🟢 VERY USEFUL)

**This is actually PERFECT for your system** - doesn't require any control changes, just monitoring.

#### What to Track

```cpp
struct BladeLifeData {
    // Time-based
    uint32_t total_cutting_time_sec;     // Time with axis moving
    uint32_t total_spindle_on_time_sec;  // Total time blade spinning (if monitored)

    // Distance-based
    float total_cut_distance_X_meters;   // Linear distance X axis traveled
    float total_cut_distance_Y_meters;
    float total_cut_distance_Z_meters;
    float total_cut_distance_A_degrees;  // Rotational axis

    // Cycle-based
    uint32_t start_stop_cycles;          // Number of motion starts (wear indicator)
    uint32_t direction_reversals;        // Direction changes (stress indicator)

    // Metadata
    uint32_t installation_timestamp;     // When blade installed
    char blade_type[32];                 // "14in Diamond Continuous Rim"
    char material_cut[64];               // "Primarily Granite"
    uint16_t replacement_hours;          // Alert threshold (default: 40 hrs)

    // Quality indicators
    uint32_t last_quality_check;         // Timestamp of last inspection
    uint8_t quality_rating;              // 1-5, operator rated
};
```

#### Tracking Logic

```cpp
// Call from motion task (10ms loop)
void bladeTrackerUpdate() {
    static uint32_t last_update = 0;
    uint32_t now = millis();

    // Only track when axis is actually moving (cutting)
    if (motionIsMoving()) {
        uint8_t active_axis = motionGetActiveAxis();
        float speed_mm_s = motionGetCurrentSpeed() / 60.0;

        // Accumulate cutting time
        if (now - last_update >= 1000) {  // Every second
            blade_data.total_cutting_time_sec++;

            // Accumulate distance per axis
            float distance_this_sec = speed_mm_s;  // mm/s * 1s
            switch (active_axis) {
                case AXIS_X:
                    blade_data.total_cut_distance_X_meters += distance_this_sec / 1000.0;
                    break;
                case AXIS_Y:
                    blade_data.total_cut_distance_Y_meters += distance_this_sec / 1000.0;
                    break;
                case AXIS_Z:
                    blade_data.total_cut_distance_Z_meters += distance_this_sec / 1000.0;
                    break;
                case AXIS_A:
                    // For rotational axis, track in degrees
                    blade_data.total_cut_distance_A_degrees += (distance_this_sec / PPM_A) * 360.0;
                    break;
            }

            last_update = now;
        }
    }
}

// Track start/stop cycles
void bladeTrackerOnMotionStart() {
    blade_data.start_stop_cycles++;
}

void bladeTrackerOnDirectionChange() {
    blade_data.direction_reversals++;
}

// Warning system
void bladeTrackerCheckWarnings() {
    float hours = blade_data.total_cutting_time_sec / 3600.0;

    if (hours > blade_data.replacement_hours) {
        logError("[BLADE] PAST REPLACEMENT: %.1f hrs (limit: %d)",
                 hours, blade_data.replacement_hours);
        // Optional: Prevent start until blade reset
    }
    else if (hours > blade_data.replacement_hours * 0.9) {
        // Warning at 90% of limit
        static uint32_t last_warning = 0;
        if (now - last_warning > 600000) {  // Every 10 minutes
            logWarning("[BLADE] Approaching replacement: %.1f / %d hrs",
                       hours, blade_data.replacement_hours);
            last_warning = now;
        }
    }
}
```

#### User Interface

**CLI Commands:**
```bash
> blade info
=== BLADE LIFE MONITOR ===
Blade Type: 14in Diamond Continuous Rim
Installed: 2025-11-01 08:30
Material: Granite/Marble Mix

Cutting Time: 37.2 hours
Replacement Limit: 40.0 hours
Remaining: 2.8 hours (93% used)

Cut Distances:
  X-Axis: 1,247 meters
  Y-Axis: 892 meters
  Z-Axis: 234 meters
  A-Axis: 1,580 degrees

Cycles:
  Start/Stop: 3,124 cycles
  Direction Changes: 8,456

Status: [WARNING] Approaching replacement

> blade reset
[BLADE] Enter blade type (or Enter for default): 14in Diamond
[BLADE] Enter expected life in hours (40): 45
[BLADE] Blade tracker reset. New blade installed.

> blade set_limit 50
[BLADE] Replacement limit set to 50 hours

> blade quality 4
[BLADE] Quality rated 4/5 - recorded
```

**Web UI Dashboard:**
```html
<div class="blade-monitor">
  <h3>Blade Life Monitor</h3>

  <!-- Circular gauge 0-100% -->
  <canvas id="bladeGauge" width="200" height="200"></canvas>
  <p class="gauge-label">37.2 / 40.0 hours (93%)</p>

  <div class="blade-stats">
    <table>
      <tr><td>Blade Type:</td><td id="bladeType">14in Diamond</td></tr>
      <tr><td>Installed:</td><td id="bladeInstalled">2025-11-01</td></tr>
      <tr><td>Cutting Time:</td><td id="cuttingTime">37.2 hrs</td></tr>
      <tr><td>Cut Distance:</td><td id="cutDist">2,373 m</td></tr>
      <tr><td>Cycles:</td><td id="cycles">3,124</td></tr>
      <tr><td>Status:</td><td id="status" class="warning">⚠ Replace Soon</td></tr>
    </table>
  </div>

  <button onclick="replaceBlade()" class="btn-warning">Blade Replaced</button>
</div>
```

**Benefits:**
- 🎯 **Predictive maintenance** - Know exactly when to replace
- 🎯 **Cost tracking** - Calculate blade cost per meter/job
- 🎯 **Quality control** - Replace before cuts degrade
- 🎯 **Prevent downtime** - Order replacement in advance
- 🎯 **Historical records** - Track blade performance over time

**Implementation:** 3-4 days

---

### 2.2 Job Sequence Programming (🟢 VERY USEFUL)

**Purpose:** Program multi-step cutting sequences with operator verification between steps

#### Feature: Step-by-Step Job Templates

```cpp
// Job definition
struct CuttingStep {
    uint8_t axis;                  // Which axis to move
    float target_position_mm;      // Target position
    speed_profile_t speed;         // SLOW/MED/FAST
    bool wait_for_operator;        // Pause for verification
    char description[64];          // "Position blade above cut line"
};

struct CuttingJob {
    char name[32];                 // "Standard Slab Cut 36x72"
    CuttingStep steps[32];         // Up to 32 steps
    uint8_t step_count;
    bool require_material_check;   // Check material sensor before start
    bool require_guard_check;      // Verify guards before start
};

// Example: Standard rectangular cut
CuttingJob job_rectangle = {
    .name = "Rectangle Cut",
    .steps = {
        // Step 1: Position X to start
        {AXIS_X, 0.0, SPEED_FAST, true, "Move to X start position"},

        // Step 2: Lower Z to cutting depth
        {AXIS_Z, -30.0, SPEED_SLOW, true, "Lower blade to cutting depth"},

        // Step 3: Cut along X axis
        {AXIS_X, 500.0, SPEED_MED, false, "Cut X axis to 500mm"},

        // Step 4: Raise Z
        {AXIS_Z, 0.0, SPEED_FAST, false, "Raise blade"},

        // Step 5: Move Y to next cut line
        {AXIS_Y, 300.0, SPEED_FAST, true, "Position Y for return cut"},

        // Step 6: Lower Z
        {AXIS_Z, -30.0, SPEED_SLOW, false, "Lower blade"},

        // Step 7: Return cut along X
        {AXIS_X, 0.0, SPEED_MED, false, "Return cut to X start"},

        // Step 8: Raise Z and finish
        {AXIS_Z, 0.0, SPEED_FAST, false, "Raise blade - job complete"}
    },
    .step_count = 8,
    .require_material_check = true,
    .require_guard_check = true
};
```

#### Execution Logic

```cpp
void executeJob(CuttingJob* job) {
    Serial.printf("\n[JOB] Starting: %s\n", job->name);
    Serial.printf("[JOB] Steps: %d\n\n", job->step_count);

    // Pre-job checks
    if (job->require_material_check && !materialPresent()) {
        Serial.println("[JOB] ERROR: No material detected");
        return;
    }

    if (job->require_guard_check && !safetyCheckGuardsInterlocked()) {
        Serial.println("[JOB] ERROR: Guards not closed");
        return;
    }

    // Execute each step
    for (uint8_t i = 0; i < job->step_count; i++) {
        CuttingStep* step = &job->steps[i];

        Serial.printf("[JOB] Step %d/%d: %s\n", i+1, job->step_count, step->description);

        if (step->wait_for_operator) {
            Serial.println("[JOB] Press RESUME to continue...");
            while (!resumeButtonPressed()) {
                delay(100);
                // Check for E-stop during wait
                if (safetyIsAlarmed()) {
                    Serial.println("[JOB] ABORTED - E-stop triggered");
                    return;
                }
            }
        }

        // Execute move
        Serial.printf("[JOB] Moving %s to %.1fmm at speed %d\n",
                      axisName(step->axis), step->target_position_mm, step->speed);

        bool success = motionMoveAbsolute(step->axis, step->target_position_mm, step->speed);

        if (!success) {
            Serial.printf("[JOB] ERROR at step %d - job aborted\n", i+1);
            return;
        }

        // Wait for move to complete
        while (motionIsMoving()) {
            delay(50);
            // Monitor for safety issues
            if (safetyIsAlarmed()) {
                Serial.println("[JOB] ABORTED - Safety alarm");
                return;
            }
        }

        Serial.printf("[JOB] Step %d complete\n\n", i+1);
        bladeTrackerOnMotionStart();  // Track usage
    }

    Serial.printf("[JOB] %s COMPLETE\n", job->name);
    jobHistoryAdd(job->name, millis() - job_start_time);
}
```

#### CLI Interface

```bash
> job list
Available Jobs:
  1. Rectangle Cut (8 steps)
  2. L-Shape Cut (12 steps)
  3. Corner Miter (6 steps)
  4. Manual Multi-Pass (4 steps)

> job run 1
[JOB] Starting: Rectangle Cut
[JOB] Steps: 8

[JOB] Step 1/8: Move to X start position
[JOB] Press RESUME to continue...
[User presses button]
[JOB] Moving X to 0.0mm at speed FAST
[JOB] Step 1 complete

[JOB] Step 2/8: Lower blade to cutting depth
[JOB] Press RESUME to continue...
...

> job create my_custom
[JOB] Creating new job: my_custom
Step 1 - Enter axis (X/Y/Z/A): X
Step 1 - Target position (mm): 100
Step 1 - Speed (SLOW/MED/FAST): MED
Step 1 - Wait for operator? (y/n): n
Step 1 - Description: First cut
Add another step? (y/n): y
...
```

**Benefits:**
- 🎯 **Repeatability** - Same cut pattern every time
- 🎯 **Safety** - Operator verification at critical points
- 🎯 **Training** - New operators follow programmed sequences
- 🎯 **Efficiency** - No manual positioning between cuts
- 🎯 **Error reduction** - Eliminate manual measurement errors

**Implementation:** 5-6 days

---

### 2.3 Material & Speed Recommendations (🟢 USEFUL)

**Purpose:** Guide operator on which speed to use for each material

**This is realistic given your constraints** - just provide guidance, operator still selects speed manually.

#### Material Database

```cpp
struct MaterialGuide {
    char name[32];
    speed_profile_t recommended_speed_roughing;  // Rapid positioning
    speed_profile_t recommended_speed_cutting;   // Actual cutting
    uint8_t max_depth_per_pass_mm;              // Safety limit
    char notes[128];
};

const MaterialGuide MATERIAL_DATABASE[] = {
    {"Granite - General",       SPEED_FAST, SPEED_MED, 30,
     "Hard material - use MEDIUM for cutting, SLOW for corners"},

    {"Granite - Black Galaxy",  SPEED_FAST, SPEED_SLOW, 25,
     "Very hard - use SLOW speed to prevent blade damage"},

    {"Marble - Soft",           SPEED_FAST, SPEED_FAST, 50,
     "Soft material - FAST speed OK, watch for chipping on edges"},

    {"Marble - Hard (Carrara)", SPEED_FAST, SPEED_MED, 40,
     "Moderate hardness - MEDIUM speed recommended"},

    {"Quartz - Engineered",     SPEED_MED, SPEED_SLOW, 25,
     "Very hard/abrasive - SLOW speed, check blade wear frequently"},

    {"Limestone",               SPEED_FAST, SPEED_FAST, 60,
     "Soft material - FAST speed OK, watch for surface finish"},

    {"Slate",                   SPEED_FAST, SPEED_MED, 35,
     "Moderate - MEDIUM speed, watch for natural fracture planes"},

    {"Soapstone",               SPEED_FAST, SPEED_FAST, 70,
     "Very soft - FAST speed OK, minimal blade wear"},

    {"Travertine",              SPEED_FAST, SPEED_FAST, 55,
     "Soft - FAST OK, watch for voids/holes in material"},

    {"Onyx",                    SPEED_SLOW, SPEED_SLOW, 20,
     "Fragile/translucent - SLOW speed, high risk of cracking"},
};
```

#### User Interface

**CLI:**
```bash
> material list
=== MATERIAL CUTTING GUIDE ===
1.  Granite - General
2.  Granite - Black Galaxy
3.  Marble - Soft
4.  Marble - Hard (Carrara)
5.  Quartz - Engineered
6.  Limestone
7.  Slate
8.  Soapstone
9.  Travertine
10. Onyx

> material info granite
=== Granite - General ===
Positioning Speed: FAST
Cutting Speed: MEDIUM
Max Depth/Pass: 30mm
Notes: Hard material - use MEDIUM for cutting, SLOW for corners

Typical blade life: 35-45 hours
Recommended water flow: High

> material select 1
[MATERIAL] Active: Granite - General
[MATERIAL] Recommended cutting speed: MEDIUM
[MATERIAL] Max depth/pass: 30mm
[MATERIAL] Use SLOW speed for corners and final passes
```

**Web UI:**
```html
<div class="material-guide">
  <h3>Material Selection</h3>
  <select id="materialSelect" onchange="showMaterialInfo()">
    <option value="0">Granite - General</option>
    <option value="1">Granite - Black Galaxy</option>
    ...
  </select>

  <div class="material-info">
    <h4 id="materialName">Granite - General</h4>
    <table>
      <tr>
        <td>Positioning Speed:</td>
        <td class="speed-fast">FAST</td>
      </tr>
      <tr>
        <td>Cutting Speed:</td>
        <td class="speed-med">MEDIUM</td>
      </tr>
      <tr>
        <td>Max Depth/Pass:</td>
        <td>30 mm</td>
      </tr>
    </table>
    <p class="material-notes">
      Hard material - use MEDIUM for cutting, SLOW for corners
    </p>
  </div>
</div>
```

**Implementation:** 2 days

---

### 2.4 Production Statistics (🟢 BUSINESS VALUE)

#### Track Per Session/Day/Month

```cpp
struct ProductionStats {
    // Time tracking
    uint32_t total_cutting_time_sec;
    uint32_t total_idle_time_sec;
    uint32_t total_alarm_time_sec;

    // Job tracking
    uint16_t jobs_completed;
    uint16_t jobs_aborted;

    // Efficiency
    float uptime_percent;           // cutting_time / (cutting + idle)
    uint16_t avg_job_time_sec;

    // Axis usage
    float total_X_travel_meters;
    float total_Y_travel_meters;
    float total_Z_travel_meters;

    // Safety events
    uint16_t estop_activations;
    uint16_t guard_openings;
    uint16_t limit_violations;
};
```

**CLI:**
```bash
> stats today
=== PRODUCTION STATISTICS (Today) ===
Uptime: 8h 23m
Cutting Time: 6h 47m (81% efficiency)
Idle Time: 1h 36m

Jobs: 27 completed, 2 aborted
Avg Job Time: 15m 4s

Axis Travel:
  X: 234.5 meters
  Y: 187.3 meters
  Z: 45.8 meters

Safety Events:
  E-Stop: 3
  Guard Opens: 12
  Limit Violations: 0

> stats week
...

> stats reset
[STATS] Daily statistics reset
```

**Implementation:** 3 days

---

## 3. OPERATOR ASSISTANCE FEATURES (🟢 HELPFUL)

### 3.1 Position Verification & Accuracy Check

**Purpose:** Verify motion system accuracy, detect mechanical issues

```cpp
void verifyPositionAccuracy(uint8_t axis) {
    float target = 100.0;  // Move 100mm

    Serial.printf("[VERIFY] Testing %s axis accuracy...\n", axisName(axis));

    // Move to target
    motionMoveAbsolute(axis, target, SPEED_MED);
    while (motionIsMoving()) delay(50);

    // Check actual position
    float actual = motionGetAxisPosition(axis);
    float error = abs(actual - target);

    Serial.printf("[VERIFY] Target: %.3f mm\n", target);
    Serial.printf("[VERIFY] Actual: %.3f mm\n", actual);
    Serial.printf("[VERIFY] Error: %.3f mm\n", error);

    if (error < 0.1) {
        Serial.println("[VERIFY] PASS - Excellent accuracy");
    } else if (error < 0.5) {
        Serial.println("[VERIFY] PASS - Acceptable accuracy");
    } else {
        Serial.println("[VERIFY] FAIL - Check mechanical system");
        logWarning("[VERIFY] Axis %d positioning error: %.3f mm", axis, error);
    }
}
```

**CLI:**
```bash
> verify X
[VERIFY] Testing X axis accuracy...
[VERIFY] Target: 100.000 mm
[VERIFY] Actual: 100.023 mm
[VERIFY] Error: 0.023 mm
[VERIFY] PASS - Excellent accuracy
```

**Implementation:** 1 day

---

### 3.2 Speed Profile Measurement

**Purpose:** Measure actual speeds achieved by PLC/VFD at each profile

```cpp
void measureSpeedProfile(uint8_t axis, speed_profile_t profile) {
    float start_pos = motionGetAxisPosition(axis);
    float test_distance = 100.0;  // mm

    Serial.printf("[SPEED] Measuring %s speed on %s axis...\n",
                  speedName(profile), axisName(axis));

    uint32_t start_time = millis();
    motionMoveRelative(axis, test_distance, profile);

    while (motionIsMoving()) delay(10);

    uint32_t elapsed_ms = millis() - start_time;
    float actual_pos = motionGetAxisPosition(axis);
    float distance = abs(actual_pos - start_pos);

    float speed_mm_min = (distance / elapsed_ms) * 60000.0;

    Serial.printf("[SPEED] Distance: %.1f mm\n", distance);
    Serial.printf("[SPEED] Time: %lu ms\n", (unsigned long)elapsed_ms);
    Serial.printf("[SPEED] Speed: %.1f mm/min\n", speed_mm_min);

    // Save to config
    configSetSpeedProfile(axis, profile, speed_mm_min);
}
```

**CLI:**
```bash
> speed_measure X SLOW
[SPEED] Measuring SLOW speed on X axis...
[SPEED] Distance: 100.0 mm
[SPEED] Time: 12,430 ms
[SPEED] Speed: 483.1 mm/min
[SPEED] Saved to config: X_SPEED_SLOW = 483

> speed_measure X MED
...

> speed_measure X FAST
...
```

**Implementation:** 2 days

---

## 4. REALISTIC IMPLEMENTATION ROADMAP

### **Phase 1: Safety Compliance** (1 week)
**Priority:** 🔴 CRITICAL
**Cost:** $65 hardware

| Task | Days | Hardware |
|------|------|----------|
| Install guard interlock sensors | 0.5 | $30 |
| Implement guard safety logic | 1 | - |
| Add material presence sensor | 0.5 | $15 |
| Implement two-hand start | 1 | $20 |
| Testing & validation | 2 | - |

**Deliverable:** OSHA/CE compliant safety system

---

### **Phase 2: Production Tracking** (1-2 weeks)
**Priority:** 🟢 HIGH VALUE
**Cost:** $0 hardware

| Task | Days |
|------|------|
| Blade life tracking system | 4 |
| Production statistics | 3 |
| Web UI dashboards | 2 |
| CLI commands | 1 |

**Deliverable:** Complete usage tracking and maintenance scheduling

---

### **Phase 3: Operator Assistance** (1-2 weeks)
**Priority:** 🟢 HELPFUL
**Cost:** $0 hardware

| Task | Days |
|------|------|
| Material recommendation database | 2 |
| Job sequence programming | 6 |
| Position verification tools | 1 |
| Speed profile measurement | 2 |

**Deliverable:** Guided operation and repeatable job programs

---

## 5. TOTAL INVESTMENT

### Hardware Costs

| Item | Cost |
|------|------|
| Guard interlock sensors (2x) | $30 |
| Material presence sensor | $15 |
| Two-hand start buttons | $20 |
| **TOTAL** | **$65** |

### Engineering Time

| Phase | Duration | Value |
|-------|----------|-------|
| Phase 1 (Safety) | 1 week | Compliance required |
| Phase 2 (Tracking) | 1-2 weeks | High business value |
| Phase 3 (Assistance) | 1-2 weeks | Quality of life |
| **TOTAL** | **3-5 weeks** | - |

---

## 6. WHAT'S NOT REALISTIC (Given Architecture)

### ❌ Features That Don't Make Sense

1. **Adaptive Feed Control**
   - Requires smooth speed adjustment
   - You only have 3 discrete speeds
   - Cannot change speed mid-move safely

2. **Corner Slowdown**
   - Requires simultaneous multi-axis motion
   - Your system moves one axis at a time
   - No "corners" in sequential motion

3. **Trajectory Planning / Interpolation**
   - Requires coordinated multi-axis motion
   - Single VFD cannot drive multiple axes simultaneously
   - Not possible with legacy PLC architecture

4. **Motor Load Sensing for Adaptive Control**
   - Could monitor load, but cannot adjust speed smoothly
   - Only value would be alerting operator "load is high"
   - Low ROI for $27 hardware investment

5. **Traditional CNC G-Code (G0/G1 with XY)**
   - Cannot execute simultaneous X+Y moves
   - G-code assumes interpolated motion
   - Your system is point-to-point only

---

## 7. HONEST ASSESSMENT

### **Current System Grade for Stone Cutting: C+ (Functional but Limited)**

**Strengths:**
- ✅ Accurate position control (WJ66 encoders)
- ✅ Closed-loop feedback
- ✅ Basic safety (E-stop, soft limits)
- ✅ Reliable sequential motion

**Limitations (Cannot Change):**
- ⚠️ Single VFD / sequential motion only
- ⚠️ 3 discrete speeds (cannot smooth adjust)
- ⚠️ No multi-axis interpolation
- ⚠️ Legacy PLC architecture constraints

**With Proposed Upgrades: B (Professional Sequential System)**

After implementing all 3 phases:
- ✅ OSHA/CE safety compliance
- ✅ Production tracking and maintenance scheduling
- ✅ Operator guidance for materials/speeds
- ✅ Repeatable job programming
- ✅ Professional user interface

**But Still Limited By:**
- ⚠️ Sequential motion (inherent to hardware)
- ⚠️ Discrete speeds (legacy PLC limitation)
- ⚠️ Cannot match modern CNC capabilities

---

## 8. RECOMMENDATION

### **Focus on What's Realistic and Valuable**

**START WITH: Phase 1 (Safety) - 1 week, $65**

This is:
1. **Required** for regulatory compliance
2. **Low cost** and quick to implement
3. **Immediate safety value**
4. **Necessary** before considering other features

**THEN ADD: Phase 2 (Tracking) - 1-2 weeks, $0**

This provides:
1. **Business value** (maintenance scheduling, cost tracking)
2. **No hardware cost**
3. **Works perfectly** with your sequential architecture
4. **Tangible ROI** (blade life optimization, statistics)

**OPTIONALLY ADD: Phase 3 (Assistance) - 1-2 weeks, $0**

This gives:
1. **Operator convenience** (material guides, job templates)
2. **Repeatability** (programmed sequences)
3. **Training aid** for new operators
4. **Quality of life** improvements

---

## 9. THE HONEST TRUTH

**Your system is fundamentally limited by the legacy PLC architecture with single VFD and sequential motion.**

To make this a truly "professional" CNC stone cutter comparable to modern systems, you would need:

1. **Multi-axis servo system** - Simultaneous X+Y+Z motion
2. **Modern motion controller** - Trajectory planning, smooth acceleration
3. **Direct VFD control** - Variable speed with feedback
4. **Spindle VFD** - Independent blade speed control

**This would be a complete hardware replacement, not a firmware upgrade.**

**What you CAN achieve with firmware upgrades:**
- ✅ Safety compliance (guards, interlocks)
- ✅ Production tracking (blade life, statistics)
- ✅ Operator assistance (guides, job templates)
- ✅ Better monitoring and diagnostics

**What you CANNOT achieve without hardware changes:**
- ❌ Smooth interpolated motion
- ❌ Adaptive feed control
- ❌ Corner slowdown
- ❌ Modern CNC features

---

## NEXT STEPS

Would you like me to:

1. **Implement Phase 1 (Safety)** - Guard interlocks, material sensor, two-hand start?
2. **Implement Phase 2 (Tracking)** - Blade life tracking and production statistics?
3. **Focus on specific feature** - e.g., just blade tracking or job programming?
4. **Provide hardware wiring diagrams** - For the safety sensors?

Let me know which direction makes most sense for your operation!
