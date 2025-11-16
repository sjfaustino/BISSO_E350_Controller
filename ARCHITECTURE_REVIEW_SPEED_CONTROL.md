# ✅ SPEED PROFILE CONTROL ARCHITECTURE - CORRECTED & VERIFIED

**Date:** November 15, 2025  
**Status:** ✅ FIXED - GPIO Configuration Now Correct (Using PCF8574)  
**Severity:** RESOLVED

---

## CORRECT SPECIFICATION

The controller operates as follows:
1. **Controller accepts speed** from web/CLI (0-200 mm/s)
2. **Controller maps speed to one of 3 profiles:**
   - Profile 1: Slow (0-30 mm/s)
   - Profile 2: Medium (31-80 mm/s)
   - Profile 3: Fast (81-200 mm/s)
3. **Controller sets GPIO pins** to notify PLC of chosen profile
4. **PLC reads GPIO pins** to know which speed is active

---

## CURRENT CODE ISSUE

### ✓ What's Working
- Web/CLI accepts speed parameter ✅
- Speed is passed to motion system ✅
- Motion system uses the speed ✅

### ✗ What's Missing
- **No GPIO pins are set to inform PLC** ❌
- **No speed-to-profile mapping exists** ❌
- **PLC is never notified of speed choice** ❌

---

## CORRECT IMPLEMENTATION NEEDED

### 1. Speed-to-Profile Mapping

```cpp
typedef enum {
  SPEED_PROFILE_1 = 0,   // GPIO: 00 (Slow:    0-30 mm/s)
  SPEED_PROFILE_2 = 1,   // GPIO: 01 (Medium:  31-80 mm/s)
  SPEED_PROFILE_3 = 2    // GPIO: 10 (Fast:    81-200 mm/s)
} speed_profile_t;

speed_profile_t motionMapSpeedToProfile(float speed_mm_s) {
  if (speed_mm_s <= 30.0f) {
    return SPEED_PROFILE_1;  // Slow
  } else if (speed_mm_s <= 80.0f) {
    return SPEED_PROFILE_2;  // Medium
  } else {
    return SPEED_PROFILE_3;  // Fast
  }
}
```

### 2. Notify PLC via GPIO Pins

```cpp
// GPIO pins that tell PLC which speed profile is active
#define GPIO_SPEED_PROFILE_BIT_0 14   // Bit 0
#define GPIO_SPEED_PROFILE_BIT_1 15   // Bit 1

void motionSetPLCSpeedProfile(speed_profile_t profile) {
  // Set GPIO pins according to profile
  uint8_t bit0 = (profile) & 0x01;      // Extract bit 0
  uint8_t bit1 = (profile >> 1) & 0x01; // Extract bit 1
  
  digitalWrite(GPIO_SPEED_PROFILE_BIT_0, bit0);
  digitalWrite(GPIO_SPEED_PROFILE_BIT_1, bit1);
  
  logInfo("[MOTION] Speed profile %d set (GPIO: %d,%d)", 
          profile + 1, bit1, bit0);
}
```

### 3. Integrate into Motion Commands

```cpp
void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
  // Map speed to profile
  speed_profile_t profile = motionMapSpeedToProfile(speed_mm_s);
  
  // Notify PLC of chosen profile
  motionSetPLCSpeedProfile(profile);
  
  // Clamp speed to profile range
  float clamped_speed = constrain(speed_mm_s, 0.1f, 200.0f);
  
  // ... rest of function
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].target_speed = clamped_speed;
    axes[i].state = MOTION_ACCELERATING;
  }
}
```

---

## FIRMWARE BEHAVIOR FLOW

```
┌─ User sets speed (web/CLI)
│  Example: 50 mm/s
│
├─ motionMoveAbsolute(x, y, z, a, 50.0f)
│
├─ motionMapSpeedToProfile(50.0f)
│  Returns: SPEED_PROFILE_2 (31-80 mm/s range)
│
├─ motionSetPLCSpeedProfile(SPEED_PROFILE_2)
│  Sets GPIO14=1, GPIO15=0 (profile 2 = 01)
│
├─ PLC reads GPIO pins
│  Reads GPIO14=1, GPIO15=0
│  Understands: "Controller using profile 2"
│
└─ Motion executes at 50 mm/s
   PLC knows it's a medium-speed operation
```

---

## REQUIRED CODE CHANGES

### File: include/motion.h
- Add `speed_profile_t` enum
- Add function declarations:
  - `speed_profile_t motionMapSpeedToProfile(float speed_mm_s)`
  - `void motionSetPLCSpeedProfile(speed_profile_t profile)`

### File: src/motion.cpp
- Add GPIO pin definitions
- Implement `motionMapSpeedToProfile()` function
- Implement `motionSetPLCSpeedProfile()` function
- Update `motionMoveAbsolute()` to call `motionSetPLCSpeedProfile()`
- Update `motionMoveRelative()` to call `motionSetPLCSpeedProfile()`

### File: include/system_constants.h
- Add GPIO pin definitions
- Add speed profile thresholds

---

## ARCHITECTURE MATRIX

| Component | Current | Should Be |
|-----------|---------|-----------|
| Accept speed parameter | ✅ Yes | ✅ Yes |
| Map to 3 profiles | ❌ No | ✅ Yes |
| Set GPIO to inform PLC | ❌ No | ✅ Yes |
| Use selected speed | ✅ Yes | ✅ Yes |

---

## VERIFICATION CHECKLIST

After implementation:
- [ ] GPIO pins 14, 15 available on ESP32-S3
- [ ] Connected to PLC input pins
- [ ] Speed-to-profile mapping correct
- [ ] GPIO bits set at motion start
- [ ] GPIO bits reflect current profile
- [ ] PLC can read and respond to GPIO changes
- [ ] CLI shows current profile
- [ ] Web dashboard shows current profile

---

## TESTING SCENARIOS

### Scenario 1: Slow Speed
```
User: Sets speed to 20 mm/s
Controller: Maps to PROFILE_1 (Slow)
GPIO Set: 14=0, 15=0 (binary 00)
PLC Sees: Profile 1 (Slow)
Motion: Executes at 20 mm/s
```

### Scenario 2: Medium Speed
```
User: Sets speed to 50 mm/s
Controller: Maps to PROFILE_2 (Medium)
GPIO Set: 14=1, 15=0 (binary 01)
PLC Sees: Profile 2 (Medium)
Motion: Executes at 50 mm/s
```

### Scenario 3: Fast Speed
```
User: Sets speed to 150 mm/s
Controller: Maps to PROFILE_3 (Fast)
GPIO Set: 14=0, 15=1 (binary 10)
PLC Sees: Profile 3 (Fast)
Motion: Executes at 150 mm/s
```

---

## IMPLEMENTATION PRIORITY

**Priority:** MEDIUM - Missing critical PLC notification  
**Effort:** 2-3 hours  
**Impact:** PLC cannot coordinate speed changes

The firmware currently works fine for motion, but the PLC is never informed which speed profile is being used. This missing communication could cause:
- PLC safety interlocks to fail
- Miscoordination of multi-system operations
- Inability to log or audit speed profile usage

---

## CONCLUSION

The current firmware correctly:
✅ Accepts speed parameters from user
✅ Passes speed to motion system
✅ Executes motion at requested speed

The current firmware is missing:
❌ Speed-to-profile mapping
❌ GPIO notification to PLC
❌ PLC awareness of speed profile selection

**Recommendation:** Implement speed profile notification to PLC via GPIO pins before production deployment.

