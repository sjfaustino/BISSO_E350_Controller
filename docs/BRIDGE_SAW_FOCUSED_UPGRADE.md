# Professional Stone Bridge Saw Controller - Focused Upgrade Plan

**Document Version:** 2.0 (Revised for Manual Water/Spindle Operation)
**Target Hardware:** KC868-A16/A32 (ESP32-S3)
**Current Firmware:** Gemini v3.5.x
**Analysis Date:** 2025-12-08

---

## Executive Summary - Revised Scope

**User Clarification:** Water/coolant system and spindle speed are **controlled manually by the operator**.

This significantly simplifies the upgrade! The focus shifts from **"control systems"** to **"motion optimization and production tracking"**.

**What's Already Manual (Operator Controlled):**
- ❌ Water pump ON/OFF - operator turns valve
- ❌ Spindle speed (RPM) - operator sets VFD directly
- ❌ Spindle ON/OFF - operator uses VFD controls

**What Still Adds Value (Controller Features):**
- ✅ **Adaptive feed control** - Automatically slow down when cutting load is high
- ✅ **Material-specific profiles** - Pre-configured feed rates for granite, marble, etc.
- ✅ **Multi-pass cutting** - Automatically break thick cuts into multiple passes
- ✅ **Corner slowdown** - Reduce speed on sharp corners for quality
- ✅ **Blade life tracking** - Know when to replace blade
- ✅ **Guard interlocks** - Safety requirement (OSHA/CE)
- ✅ **Production statistics** - Track job times, efficiency

---

## 1. CRITICAL FEATURES (🔴 High Value)

### 1.1 Motor Load Sensing for Adaptive Feed (🔴 HIGHEST VALUE!)

**Problem:** Currently, feed rate is constant regardless of cutting load.
- Too fast → blade binds, poor cut quality, blade breakage
- Too slow → wasted time, reduced productivity

**Solution:** Monitor spindle motor current, automatically adjust feed rate.

#### Hardware Required

| Component | Purpose | Interface | Cost |
|-----------|---------|-----------|------|
| **AC Current Sensor** (SCT-013-030) | Monitor spindle motor current | IN3 (0-5V analog input) | $12 |
| **4-20mA Current Transmitter** | Convert AC to DC signal | External module | $15 |

**Total Cost:** $27

#### Wiring

```
Spindle Motor L1 Phase → SCT-013 Clamp Sensor → Current Transmitter → IN3 (GPIO39)
                                                                      → GND
```

#### Software Implementation

**New Files:**
- `include/load_monitor.h` - API for load sensing
- `src/load_monitor.cpp` - Implementation
- `include/adaptive_feed.h` - Feed control algorithm
- `src/adaptive_feed.cpp` - Implementation

**Code Example:**
```cpp
// load_monitor.cpp
float loadMonitorGetSpindleLoad() {
    int raw = analogRead(PIN_IN3);  // GPIO39, 12-bit ADC
    float voltage = raw * (5.0 / 4095.0);

    // Calibration: 0V = 0A, 5V = 30A (adjust for your sensor)
    float current_amps = (voltage / 5.0) * 30.0;

    // Convert to percentage of rated motor current
    float load_percent = (current_amps / MOTOR_RATED_CURRENT) * 100.0;

    return constrain(load_percent, 0.0, 150.0);
}

// adaptive_feed.cpp
void adaptiveFeedUpdate() {
    static float feed_override = 1.0;
    float load = loadMonitorGetSpindleLoad();

    // Aggressive cutting (high load) - slow down
    if (load > 90.0) {
        feed_override = 0.60;  // Reduce to 60%
        logWarning("[ADAPTIVE] High load %0.1f%% - reducing feed to 60%%", load);
    }
    else if (load > 80.0) {
        feed_override = 0.75;  // Reduce to 75%
    }
    else if (load > 70.0) {
        feed_override = 0.85;  // Reduce to 85%
    }
    // Light cutting (low load) - gradually increase back to 100%
    else if (load < 50.0 && feed_override < 1.0) {
        feed_override += 0.02;  // Slowly ramp back up
        if (feed_override > 1.0) feed_override = 1.0;
    }

    // Apply override to current motion
    motionSetFeedOverride(feed_override);
}
```

**Integration Points:**
- Call `adaptiveFeedUpdate()` in Motion task (10ms loop) at `src/tasks_motion.cpp:XX`
- Add configuration keys:
  - `load_threshold_warning` (default: 80.0)
  - `load_threshold_critical` (default: 90.0)
  - `adaptive_feed_enabled` (default: true)

**Benefits:**
- 🎯 **Prevents blade binding** - automatically slows before damage
- 🎯 **Maximizes productivity** - finds optimal feed rate automatically
- 🎯 **Consistent quality** - same load = same finish across materials
- 🎯 **Extends blade life** - reduces stress on blade

**Implementation Effort:** 4-5 days

---

### 1.2 Material Database & Cutting Profiles (🔴 HIGH VALUE)

**Problem:** Operator must remember/guess feed rates for different materials.

**Solution:** Pre-configured profiles for common stone types.

#### Data Structure

```cpp
// New file: include/material_profiles.h
struct MaterialProfile {
    char name[32];              // Display name
    float feed_rate_mm_min;     // Recommended feed rate
    uint8_t max_depth_per_pass; // Maximum safe depth per pass (mm)
    float load_target_percent;  // Target cutting load (for adaptive feed)
    char notes[64];             // Usage notes
};

// Pre-defined materials
const MaterialProfile MATERIALS[] = {
    {"Granite - General",     800,  30, 80.0, "Hard - medium feed"},
    {"Granite - Black Galaxy", 700,  25, 85.0, "Very hard - slow feed"},
    {"Marble - Soft",         1200, 50, 70.0, "Soft - fast feed OK"},
    {"Marble - Hard (Carrara)", 900,  40, 75.0, "Medium - moderate feed"},
    {"Quartz - Engineered",    600,  25, 85.0, "Very hard - slow feed"},
    {"Limestone",             1500, 60, 65.0, "Soft - fast feed"},
    {"Slate",                 1000, 35, 75.0, "Moderate hardness"},
    {"Soapstone",             1800, 70, 60.0, "Very soft - fast OK"},
    {"Travertine",            1300, 55, 68.0, "Soft - watch for voids"},
    {"Onyx",                   500, 20, 80.0, "Fragile - very slow"},
};
```

#### User Interface

**CLI Commands:**
```bash
> material list              # Show all available materials
> material select granite    # Set active material (fuzzy match)
> material info              # Show current material parameters
> material custom 850 28 82  # Custom: feed_rate, max_depth, load_target
```

**Web UI Addition:**
```html
<!-- Add to data/index.html -->
<div class="material-selector">
  <h3>Material Selection</h3>
  <select id="materialSelect" onchange="applyMaterial()">
    <option value="0">Granite - General</option>
    <option value="1">Granite - Black Galaxy</option>
    <option value="2">Marble - Soft</option>
    <option value="3">Marble - Hard (Carrara)</option>
    <option value="4">Quartz - Engineered</option>
    <option value="5">Limestone</option>
    <option value="6">Slate</option>
    <option value="7">Soapstone</option>
    <option value="8">Travertine</option>
    <option value="9">Onyx</option>
  </select>

  <div class="material-params">
    <p>Feed Rate: <strong><span id="feedRate">800</span> mm/min</strong></p>
    <p>Max Depth/Pass: <strong><span id="maxDepth">30</span> mm</strong></p>
    <p>Target Load: <strong><span id="targetLoad">80</span>%</strong></p>
  </div>

  <button onclick="applyMaterial()">Apply Material</button>
</div>
```

**Storage:**
- Store last selected material in NVS: `material_active_index`
- Load on boot automatically

**Benefits:**
- 🎯 **Eliminates guesswork** - proven parameters for each material
- 🎯 **Faster setup** - one click to configure
- 🎯 **Better results** - optimized for each stone type
- 🎯 **Training aid** - helps new operators

**Implementation Effort:** 3-4 days

---

### 1.3 Multi-Pass Cutting for Thick Materials (🔴 HIGH VALUE)

**Problem:** Thick slabs (>30mm) cannot be safely cut in one pass.
- Single deep pass causes blade binding
- Poor cut quality
- Excessive blade wear

**Solution:** Automatically break deep cuts into multiple passes.

#### Algorithm

```cpp
// New file: src/multi_pass_planner.cpp
void planMultiPassCut(float start_z, float target_z, float x, float y) {
    MaterialProfile mat = getCurrentMaterial();
    float total_depth = abs(target_z - start_z);

    if (total_depth <= mat.max_depth_per_pass) {
        // Single pass is OK
        planMove(x, y, target_z, getCurrentFeedRate());
        return;
    }

    // Multiple passes required
    int num_passes = ceil(total_depth / mat.max_depth_per_pass);
    float depth_per_pass = total_depth / num_passes;

    logInfo("[MULTI-PASS] Cutting %0.1fmm in %d passes of %0.1fmm each",
            total_depth, num_passes, depth_per_pass);

    for (int pass = 1; pass <= num_passes; pass++) {
        float current_z = start_z - (depth_per_pass * pass);

        // Execute cutting pass
        planMove(x, y, current_z, mat.feed_rate_mm_min);

        // Retract between passes (except last pass)
        if (pass < num_passes) {
            planMove(x, y, start_z + 5.0, RAPID_FEED_RATE);  // Retract 5mm above start
            logInfo("[MULTI-PASS] Pass %d/%d complete - retracting", pass, num_passes);
        }
    }

    logInfo("[MULTI-PASS] All %d passes complete", num_passes);
}
```

#### G-Code Integration

**Modify `gcode_parser.cpp`:**
```cpp
void GCodeParser::handleG0_G1(const char* line) {
    float x, y, z, a;
    bool has_z = parseCode(line, 'Z', z);

    if (has_z && multiPassEnabled) {
        float current_z = motionGetAxisPosition(AXIS_Z);
        float depth = abs(z - current_z);

        if (depth > getCurrentMaterial().max_depth_per_pass) {
            // Trigger multi-pass planner
            planMultiPassCut(current_z, z, x, y);
            return;  // Multi-pass planner handles it
        }
    }

    // Normal single-pass move
    pushMove(x, y, z, a);
}
```

#### Configuration

Add CLI command:
```bash
> multipass enable   # Enable automatic multi-pass
> multipass disable  # Disable (manual control)
> multipass status   # Show current setting
```

**Benefits:**
- 🎯 **Safer cutting** - prevents blade binding on thick materials
- 🎯 **Better quality** - each pass is within optimal depth
- 🎯 **Automatic** - operator doesn't need to manually program multiple passes
- 🎯 **Extends blade life** - reduces stress per pass

**Implementation Effort:** 5-6 days

---

### 1.4 Corner Slowdown Algorithm (🔴 HIGH VALUE for Quality)

**Problem:** Sharp corners at full speed cause:
- Blade deflection
- Poor cut quality (radius on inside corners)
- Chipping on outside corners
- Increased blade wear

**Solution:** Detect corner angles, automatically reduce feed rate.

#### Algorithm

```cpp
// New file: src/corner_slowdown.cpp
float calculateCornerSlowdown(float prev_x, float prev_y, float curr_x, float curr_y,
                               float next_x, float next_y) {
    // Calculate vectors
    float v1_x = curr_x - prev_x;
    float v1_y = curr_y - prev_y;
    float v2_x = next_x - curr_x;
    float v2_y = next_y - curr_y;

    // Calculate angle between vectors (dot product)
    float dot = (v1_x * v2_x) + (v1_y * v2_y);
    float mag1 = sqrt(v1_x*v1_x + v1_y*v1_y);
    float mag2 = sqrt(v2_x*v2_x + v2_y*v2_y);

    if (mag1 < 0.1 || mag2 < 0.1) return 1.0;  // Too short to matter

    float cos_angle = dot / (mag1 * mag2);
    float angle_deg = acos(constrain(cos_angle, -1.0, 1.0)) * (180.0 / PI);

    // Calculate slowdown factor based on angle
    if (angle_deg < 15.0) {
        return 1.0;  // Nearly straight - no slowdown
    }
    else if (angle_deg < 45.0) {
        return 0.9;  // Gentle corner - 10% reduction
    }
    else if (angle_deg < 90.0) {
        return 0.7;  // Moderate corner - 30% reduction
    }
    else {
        return 0.5;  // Sharp corner (90°+) - 50% reduction
    }
}
```

#### Integration

**Modify motion planner to lookahead:**
```cpp
// In motion_planner.cpp
void motionPlannerAddMove(float x, float y, float z, float feed) {
    if (move_buffer.count >= 2) {
        // We have enough history to calculate corner angle
        Move prev = move_buffer[move_buffer.count - 2];
        Move curr = move_buffer[move_buffer.count - 1];

        float slowdown = calculateCornerSlowdown(
            prev.x, prev.y,
            curr.x, curr.y,
            x, y
        );

        if (slowdown < 1.0) {
            float adjusted_feed = feed * slowdown;
            logInfo("[CORNER] Angle detected - reducing feed to %0.1f%%",
                    slowdown * 100.0);
            feed = adjusted_feed;
        }
    }

    // Add move with adjusted feed rate
    bufferAddMove(x, y, z, feed);
}
```

**Benefits:**
- 🎯 **Better cut quality** - clean inside corners
- 🎯 **Reduces chipping** - especially on outside corners
- 🎯 **Extends blade life** - reduces deflection stress
- 🎯 **Automatic** - no manual speed adjustments needed

**Implementation Effort:** 3-4 days

---

## 2. SAFETY FEATURES (🟡 Required for Compliance)

### 2.1 Guard Interlock Switches (🟡 OSHA/CE Requirement)

**Requirement:** Blade guard must have interlock switches.

#### Hardware

| Component | Purpose | Interface | Cost |
|-----------|---------|-----------|------|
| **Magnetic Proximity Sensors** (M12, NC) | Detect guard open | X12, X13 | $30 (qty 2) |

**Wiring:**
```
Front Guard Sensor → X12 (GPIO12) → GND
Rear Access Door   → X13 (GPIO13) → GND
```

#### Software

**Modify `src/safety.cpp`:**
```cpp
bool safetyCheckGuardsInterlocked() {
    bool front_guard_closed = !digitalRead(PIN_X12);  // NC contact - LOW when closed
    bool rear_guard_closed = !digitalRead(PIN_X13);

    if (!front_guard_closed || !rear_guard_closed) {
        return false;  // Guard open
    }
    return true;
}

void safetyUpdate() {
    // ... existing checks ...

    // NEW: Guard interlock check
    if (!safetyCheckGuardsInterlocked()) {
        if (motionIsMoving()) {
            motionEmergencyStop();
            safetyTriggerAlarm("GUARD INTERLOCK - Motion stopped");
        }
        // Inhibit motion start
        motion_inhibit = true;
    }
}
```

**Configuration:**
- Add `guard_interlock_enabled` NVS key (default: true)
- Add CLI command: `safety guards [enable|disable|status]`

**Implementation Effort:** 2 days

---

### 2.2 Material Presence Sensor (🟢 Recommended)

**Purpose:** Prevent starting cut cycle with no material on table.

#### Hardware

| Component | Interface | Cost |
|-----------|-----------|------|
| **Inductive Proximity Sensor** (M18, NPN-NO) | X11 | $15 |

**Implementation Effort:** 1 day

---

## 3. PRODUCTION TRACKING FEATURES (🟢 High Value for Business)

### 3.1 Blade Life Tracking (🟢 HIGH VALUE)

**Problem:** No way to know when blade needs replacement.

#### Data Structure

```cpp
// New file: include/blade_tracker.h
struct BladeLifeData {
    uint32_t total_cut_time_sec;       // Actual cutting time (spindle on + motion)
    float total_cut_length_meters;     // Linear distance cut
    uint32_t total_starts;             // Start/stop cycles
    float total_load_integral;         // Cumulative load (wear indicator)
    uint32_t installation_timestamp;   // Unix timestamp when installed
    char blade_type[32];               // "14in Diamond - Granite"
    uint16_t warning_threshold_hrs;    // Alert threshold (default: 35 hrs)
    uint16_t replacement_threshold_hrs; // Replace at (default: 40 hrs)
};
```

#### Tracking Logic

```cpp
// In motion task or dedicated tracking task
void bladeTrackerUpdate() {
    // Only count when actually cutting
    if (isSpindleRunning() && motionIsMoving()) {
        blade_data.total_cut_time_sec++;

        float speed_mm_s = motionGetCurrentSpeed() / 60.0;
        blade_data.total_cut_length_meters += speed_mm_s / 1000.0;

        // Include load as wear factor
        blade_data.total_load_integral += loadMonitorGetSpindleLoad() / 100.0;
    }
}

void bladeTrackerCheckWarnings() {
    float hours = blade_data.total_cut_time_sec / 3600.0;

    if (hours > blade_data.replacement_threshold_hrs) {
        logError("[BLADE] PAST REPLACEMENT THRESHOLD: %0.1f hrs", hours);
        // Optional: inhibit start until reset
    }
    else if (hours > blade_data.warning_threshold_hrs) {
        static uint32_t last_warning = 0;
        if (millis() - last_warning > 300000) {  // Every 5 minutes
            logWarning("[BLADE] Approaching replacement: %0.1f / %d hrs",
                       hours, blade_data.replacement_threshold_hrs);
            last_warning = millis();
        }
    }
}
```

#### User Interface

**CLI Commands:**
```bash
> blade info       # Show usage statistics
> blade reset      # Reset after blade change (prompts for blade type)
> blade set_limit 40  # Set replacement threshold hours
```

**Web UI Dashboard:**
```html
<div class="blade-life">
  <h3>Blade Life Monitor</h3>
  <div class="gauge">
    <canvas id="bladeGauge"></canvas>  <!-- Circular gauge 0-100% -->
  </div>
  <table>
    <tr><td>Cutting Time:</td><td><span id="cutTime">32.5</span> hours</td></tr>
    <tr><td>Cut Distance:</td><td><span id="cutDist">1,247</span> meters</td></tr>
    <tr><td>Start Cycles:</td><td><span id="cycles">156</span></td></tr>
    <tr><td>Installed:</td><td><span id="installed">2025-11-15</span></td></tr>
    <tr><td>Type:</td><td><span id="bladeType">14in Diamond - Granite</span></td></tr>
  </table>
  <button onclick="resetBlade()">Replace Blade</button>
</div>
```

**Benefits:**
- 🎯 **Predictive maintenance** - know when to order replacement
- 🎯 **Cost tracking** - blade cost per meter cut
- 🎯 **Quality assurance** - replace before quality degrades
- 🎯 **Prevent downtime** - plan replacement during scheduled maintenance

**Implementation Effort:** 3-4 days

---

### 3.2 Job Time Estimation (🟢 MEDIUM VALUE)

**Purpose:** Show estimated time before starting job.

#### Algorithm

```cpp
float estimateJobTime(GCodeFile* job) {
    MaterialProfile mat = getCurrentMaterial();
    float total_time = 0;

    for (GCodeMove move : job->moves) {
        if (move.type == G0) {  // Rapid positioning
            total_time += move.distance / RAPID_FEED_RATE * 60.0;
        } else {  // G1 cutting move
            total_time += move.distance / mat.feed_rate_mm_min * 60.0;
        }

        // Add corner slowdown time
        if (move.corner_angle > 45.0) {
            total_time *= 1.2;  // Rough 20% overhead for corners
        }
    }

    // Add overhead
    total_time += 10.0;  // Setup/teardown

    return total_time;
}
```

**Display:**
- Show in web UI before starting job
- CLI: `job estimate filename.gcode`

**Implementation Effort:** 2 days

---

### 3.3 Production Statistics (🟢 MEDIUM VALUE)

**Track:**
- Jobs completed per day/week/month
- Total cutting time
- Total material processed (area or length)
- Average job time
- Fault/alarm frequency

**Storage:** NVS or SD card (if available)

**Implementation Effort:** 4 days

---

## 4. G-CODE ENHANCEMENTS

### 4.1 Useful M-Codes (Even with Manual Spindle/Water)

| M-Code | Function | Use Case |
|--------|----------|----------|
| **M0** | Program Pause | Operator inspection, blade change |
| **M1** | Optional Stop | Stop if "optional stop" switch enabled |
| **M30** | Program End & Reset | Return to home, turn off outputs |
| **M117** | Display Message | Show text on web UI / LCD |

**Implementation Effort:** 2 days

---

## 5. REVISED IMPLEMENTATION ROADMAP

### **Phase 1: Motion Optimization** (3-4 weeks) 🔴 HIGHEST ROI

| Task | Priority | Effort | Hardware Cost |
|------|----------|--------|---------------|
| 1. Add motor load sensing (current sensor) | 🔴 | 4 days | $27 |
| 2. Implement adaptive feed control | 🔴 | 3 days | - |
| 3. Material database implementation | 🔴 | 4 days | - |
| 4. Multi-pass cutting planner | 🔴 | 6 days | - |
| 5. Corner slowdown algorithm | 🔴 | 4 days | - |
| 6. Web UI - Material selector | 🔴 | 2 days | - |
| 7. CLI - Material commands | 🔴 | 1 day | - |

**Total Effort:** ~24 days (3-4 weeks)
**Total Cost:** $27

**Deliverable:** Optimized cutting with automatic feed control, multi-pass, and corner slowdown

---

### **Phase 2: Safety & Compliance** (1 week) 🟡

| Task | Priority | Effort | Hardware Cost |
|------|----------|--------|---------------|
| 8. Add guard interlock switches | 🟡 | 2 days | $30 |
| 9. Material presence sensor | 🟢 | 1 day | $15 |
| 10. Safety logic updates | 🟡 | 1 day | - |

**Total Effort:** ~4 days (1 week)
**Total Cost:** $45

**Deliverable:** OSHA/CE compliant safety interlocks

---

### **Phase 3: Production Tracking** (1-2 weeks) 🟢

| Task | Priority | Effort | Hardware Cost |
|------|----------|--------|---------------|
| 11. Blade life tracking | 🟢 | 3 days | - |
| 12. Job time estimation | 🟢 | 2 days | - |
| 13. Production statistics | 🟢 | 4 days | - |
| 14. Web UI - Blade dashboard | 🟢 | 2 days | - |
| 15. M-code extensions (M0/M1/M30/M117) | 🟢 | 2 days | - |

**Total Effort:** ~13 days (2 weeks)
**Total Cost:** $0

**Deliverable:** Full production tracking and maintenance management

---

## 6. TOTAL INVESTMENT SUMMARY

### Hardware Costs (Significantly Reduced!)

| Phase | Components | Cost |
|-------|------------|------|
| **Phase 1** | Current sensor + transmitter | $27 |
| **Phase 2** | Guard sensors + material sensor | $45 |
| **Phase 3** | None | $0 |
| **TOTAL** | | **$72** |

**Previous estimate with water/spindle control:** $269
**Savings by using manual operation:** $197 (73% reduction!)

### Engineering Time

| Phase | Duration | Hours |
|-------|----------|-------|
| Phase 1 | 3-4 weeks | ~160-180 hrs |
| Phase 2 | 1 week | ~40 hrs |
| Phase 3 | 1-2 weeks | ~80-100 hrs |
| **TOTAL** | **6-8 weeks** | **~280-320 hrs** |

---

## 7. WHAT YOU GET

### Phase 1 Benefits (Motion Optimization)
✅ **Adaptive feed control** - Prevents blade binding, maximizes productivity
✅ **Material profiles** - One-click setup for granite, marble, quartz, etc.
✅ **Multi-pass cutting** - Automatically handles thick slabs safely
✅ **Corner slowdown** - Better cut quality, less chipping
✅ **Professional UI** - Material selector on web interface

**Value:** Significantly improves cut quality and blade life, reduces operator skill required

### Phase 2 Benefits (Safety)
✅ **Guard interlocks** - OSHA/CE compliant
✅ **Material presence** - Prevents no-load cutting
✅ **Enhanced safety** - Meets regulatory requirements

**Value:** Regulatory compliance, insurance requirements, operator safety

### Phase 3 Benefits (Production Tracking)
✅ **Blade life tracking** - Know when to replace before quality degrades
✅ **Job estimates** - Accurate time predictions
✅ **Production stats** - Track efficiency, job counts, cutting time
✅ **Maintenance scheduling** - Predictive maintenance

**Value:** Cost control, scheduling, quality assurance

---

## 8. WHAT'S NOT NEEDED (Given Manual Operation)

❌ Water pump relay control - operator controls valve
❌ Water flow sensor - operator responsibility
❌ MCP4725 DAC for spindle - operator sets VFD
❌ Spindle ON/OFF relay - operator uses VFD
❌ RPM tachometer - operator watches VFD display
❌ Pressure sensors - not needed
❌ M3/M5/M7/M9 codes - manual operation

**This eliminates:**
- ~$200 in hardware
- ~100 hours of engineering
- Significant wiring complexity
- Calibration effort

---

## 9. RECOMMENDATION

### Start with Phase 1 (Motion Optimization)

**Why:**
1. **Highest ROI** - $27 hardware investment, massive quality/productivity improvement
2. **Leverages existing strengths** - Your motion control is already excellent
3. **Tangible results** - Immediately see better cuts and longer blade life
4. **Low risk** - Software-focused, minimal hardware changes

### Key Features to Implement First:
1. **Motor load sensing** (1 week) - Hardware + basic software
2. **Adaptive feed control** (3-4 days) - Algorithm implementation
3. **Material database** (3-4 days) - Pre-configured profiles
4. **Multi-pass cutting** (5-6 days) - Critical for thick materials

**Timeline:** ~4 weeks for Phase 1
**Cost:** $27

Then evaluate whether Phase 2 (safety) and Phase 3 (tracking) add enough value for your specific operation.

---

## 10. CURRENT SYSTEM STRENGTHS (Unchanged)

Your current controller is **excellent** for stone cutting motion:
✅ Closed-loop positioning (WJ66 encoders)
✅ Professional FreeRTOS architecture
✅ Robust safety system (E-stop, soft limits, stall detection)
✅ Comprehensive fault logging
✅ Web interface + CLI
✅ Performance profiling (just added!)

**The foundation is solid.** These upgrades are about **optimization and intelligence**, not fixing problems.

---

## NEXT STEPS

Would you like me to:
1. **Implement Phase 1** (motion optimization with adaptive feed)?
2. **Start with just motor load sensing** (quickest win)?
3. **Create a specific feature first** (e.g., material database)?
4. **Generate wiring diagram** for the current sensor?

Let me know your priority and I'll start implementation!
