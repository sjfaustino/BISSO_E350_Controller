# SPEED PROFILE CALIBRATION & GCODE ARCHITECTURE REVIEW

**Date:** November 15, 2025  
**Status:** ⚠️ PARTIAL IMPLEMENTATION - Missing GCode support  
**Severity:** MEDIUM - Core feature incomplete

---

## CORRECT COMPLETE SPECIFICATION

### System Flow
```
1. CALIBRATION PHASE
   ├─ User runs: "speed X 1000"
   ├─ System measures actual speed for each profile
   ├─ Stores: speed_profile_1_actual = 15.2 mm/s
   ├─ Stores: speed_profile_2_actual = 45.8 mm/s
   └─ Stores: speed_profile_3_actual = 125.3 mm/s

2. OPERATION PHASE
   ├─ Receives GCode: "G1 X100 F200"
   ├─ Extracts feedrate: 200 mm/min = 3.33 mm/s
   ├─ Finds closest profile to 3.33 mm/s
   │  ├─ Profile 1: |15.2 - 3.33| = 11.87 ← CLOSEST
   │  ├─ Profile 2: |45.8 - 3.33| = 42.47
   │  └─ Profile 3: |125.3 - 3.33| = 121.97
   ├─ Selects: PROFILE_1 (15.2 mm/s)
   ├─ Sets GPIO: Profile 1 selected
   ├─ Executes: Move to X100 at actual speed 15.2 mm/s
   └─ PLC reads GPIO: "Profile 1 active"
```

---

## CURRENT IMPLEMENTATION STATUS

### ✅ What Exists
- Speed calibration command (`speed X [distance]`)
- Measures actual speed per profile
- Stores calibrated speeds in config

### ❌ What's Missing
1. **GCode parsing** - No parser for G0/G1/F codes
2. **Feedrate extraction** - No F code handling
3. **Profile matching** - No "closest value" logic
4. **GCode command interface** - No way to send motion as GCode

---

## REQUIRED ARCHITECTURE

### 1. Speed Calibration Storage

**Current:** Stores per-axis calibrated speeds
```cpp
// config keys: "speed_X_mm_s", "speed_Y_mm_s", etc
float speed_x = configGetFloat("speed_X_mm_s", 0.0f);  // 0.0 = not calibrated
```

**Should be:** Store actual speed for each profile
```cpp
// New config keys for profile speeds (measured during calibration)
speed_profile_1_actual_mm_s → 15.2 mm/s  (measured)
speed_profile_2_actual_mm_s → 45.8 mm/s  (measured)
speed_profile_3_actual_mm_s → 125.3 mm/s (measured)
```

### 2. GCode Parser

**Needed:** Parse G0, G1 commands with F feedrate
```cpp
typedef struct {
  bool valid;
  char command;          // 'G', 'M', etc
  int code;             // 0, 1, 28, etc
  float X, Y, Z, A;     // Axes (set to NaN if not specified)
  float F;              // Feedrate in mm/min (0 if not specified)
} gcode_command_t;

gcode_command_t parseGCode(const char* gcode_line);
// Example: "G1 X100 Y50 F200"
//   Returns: {valid:true, command:'G', code:1, X:100, Y:50, Z:NaN, A:NaN, F:200}
```

### 3. Feedrate to Profile Conversion

```cpp
speed_profile_t motionSelectProfileForFeedrate(float feedrate_mm_min) {
  // Convert mm/min to mm/s
  float feedrate_mm_s = feedrate_mm_min / 60.0f;
  
  // Get calibrated speeds for each profile
  float profile1_speed = configGetFloat("speed_profile_1_actual_mm_s", 15.0f);
  float profile2_speed = configGetFloat("speed_profile_2_actual_mm_s", 50.0f);
  float profile3_speed = configGetFloat("speed_profile_3_actual_mm_s", 120.0f);
  
  // Find closest match
  float diff1 = abs(profile1_speed - feedrate_mm_s);
  float diff2 = abs(profile2_speed - feedrate_mm_s);
  float diff3 = abs(profile3_speed - feedrate_mm_s);
  
  if (diff1 < diff2 && diff1 < diff3) {
    return SPEED_PROFILE_1;
  } else if (diff2 < diff3) {
    return SPEED_PROFILE_2;
  } else {
    return SPEED_PROFILE_3;
  }
}
```

### 4. GCode Command Handler

```cpp
void cliProcessGCode(const char* gcode_line) {
  gcode_command_t cmd = parseGCode(gcode_line);
  
  if (!cmd.valid) {
    Serial.println("[GCODE] ERROR: Invalid syntax");
    return;
  }
  
  if (cmd.command != 'G') {
    Serial.println("[GCODE] ERROR: Only G commands supported");
    return;
  }
  
  // Handle G0 (rapid positioning) and G1 (linear interpolation)
  if (cmd.code == 0 || cmd.code == 1) {
    // Use highest speed for G0 (rapid)
    speed_profile_t profile = (cmd.code == 0) 
      ? SPEED_PROFILE_3 
      : motionSelectProfileForFeedrate(cmd.F);
    
    float actual_speed = getProfileActualSpeed(profile);
    
    Serial.printf("[GCODE] G%d to (%.1f, %.1f, %.1f, %.1f) at %.1f mm/s\n",
                  cmd.code, cmd.X, cmd.Y, cmd.Z, cmd.A, actual_speed);
    
    motionMoveAbsolute(cmd.X, cmd.Y, cmd.Z, cmd.A, actual_speed);
  }
  else {
    Serial.printf("[GCODE] G%d not supported\n", cmd.code);
  }
}
```

---

## CALIBRATION WORKFLOW

### Current Calibration Command
```
CLI: speed X 1000
System:
  1. Reads start position
  2. Sends PLC profile 1: Sets GPIO 00
  3. Waits for motion completion
  4. Measures distance traveled
  5. Calculates speed1 = distance / time
  6. Repeat for profiles 2 & 3
  7. Store all 3 speeds in config
  8. Display results
```

### Issue: Profiles Not Being Tested
The calibration command only measures **per-axis speed**, not **per-profile speed**. 

**Current:**
- Measures: "X axis speed = 45.8 mm/s" (single value)
- Doesn't know which profile that was

**Should be:**
- Measures: "Profile 1 X speed = 15.2 mm/s"
- Measures: "Profile 2 X speed = 45.8 mm/s"
- Measures: "Profile 3 X speed = 125.3 mm/s"

### Solution: Calibration Should Test All 3 Profiles
```cpp
void calibrateAllSpeedProfiles(char axis, float distance_mm) {
  float speeds[3];
  
  for (int profile = 0; profile < 3; profile++) {
    // Set GPIO to select profile
    motionSetPLCSpeedProfile((speed_profile_t)profile);
    
    // Wait for PLC to recognize profile
    delay(100);
    
    // Record start position
    int32_t start_pos = motionGetPosition(axis);
    
    // Send motion command to PLC
    plcSendMotionCommand(axis, distance_mm);
    
    // Wait for completion
    uint32_t start_time = millis();
    while (!motionComplete() && (millis() - start_time) < 10000) {
      delay(10);
    }
    
    // Calculate actual speed
    int32_t distance_traveled = motionGetPosition(axis) - start_pos;
    uint32_t elapsed_ms = millis() - start_time;
    speeds[profile] = (distance_traveled / 1000.0f) / (elapsed_ms / 1000.0f);
    
    // Store in config
    char key[32];
    snprintf(key, sizeof(key), "speed_profile_%d_actual_mm_s", profile + 1);
    configSetFloat(key, speeds[profile]);
    
    Serial.printf("[CALIB] Profile %d: %.1f mm/s\n", profile + 1, speeds[profile]);
  }
}
```

---

## GCODE SUPPORT EXAMPLES

### Example 1: Slow Positioning
```
INPUT:  G1 X100 Y50 F100  (100 mm/min = 1.67 mm/s)
LOGIC:
  ├─ Requested: 1.67 mm/s
  ├─ Profile 1 actual: 15.2 mm/s  (diff: 13.53)
  ├─ Profile 2 actual: 45.8 mm/s  (diff: 44.13) ← Selected
  ├─ Profile 3 actual: 125.3 mm/s (diff: 123.63)
  └─ Selected: Profile 1 (closest: 13.53)
OUTPUT: Move to (100, 50) at Profile 1 speed (15.2 mm/s)
```

### Example 2: Medium Speed Cut
```
INPUT:  G1 X100 Y50 Z-5 F600  (600 mm/min = 10 mm/s)
OUTPUT: Move to (100, 50, -5) at Profile 2 speed (45.8 mm/s, closest to 10)
```

### Example 3: Rapid Positioning (G0)
```
INPUT:  G0 X100 Y50  (no F - rapid positioning)
OUTPUT: Move to (100, 50) at Profile 3 speed (125.3 mm/s, fastest)
```

---

## IMPLEMENTATION CHECKLIST

### Phase 1: Calibration Fix
- [ ] Update calibration to test all 3 profiles
- [ ] Store profile-specific actual speeds
- [ ] CLI command: `calib_profile X 1000` (calibrate all profiles)

### Phase 2: GCode Parser
- [ ] Create gcode parser
- [ ] Support G0 (rapid), G1 (linear)
- [ ] Extract X, Y, Z, A, F parameters
- [ ] Validate syntax

### Phase 3: Profile Selection
- [ ] Implement `motionSelectProfileForFeedrate()`
- [ ] Implement "closest match" algorithm
- [ ] Handle edge cases (F not specified, zero feedrate)

### Phase 4: GCode Command Interface
- [ ] CLI command: `gcode "G1 X100 F200"`
- [ ] Web endpoint: POST /api/gcode
- [ ] Error handling and validation

### Phase 5: Testing
- [ ] Verify profile selection with various feedrates
- [ ] Test G0 rapid moves
- [ ] Test G1 with different F values
- [ ] Test multi-axis moves

---

## PRIORITY & EFFORT

**Priority:** MEDIUM - Useful but not critical for basic operation  
**Effort:** 6-8 hours  
**Impact:** Enables CNC-style GCode control

---

## NOTES

1. Current calibration command works but only measures per-axis speed
2. Calibration should be enhanced to measure all 3 profile speeds
3. GCode parsing is completely missing - significant feature gap
4. Once implemented, system becomes proper CNC-like motion controller
5. Feedrate conversion and profile matching are critical for accuracy

---

## FILES TO MODIFY/CREATE

| File | Changes |
|------|---------|
| cli.cpp | Add gcode_command handler |
| motion.cpp | Add profile selection logic |
| motion.h | Add GCode parsing functions |
| system_constants.h | Add GCode parsing constants |
| New: gcode_parser.cpp | GCode parsing implementation |
| New: gcode_parser.h | GCode parsing interface |

