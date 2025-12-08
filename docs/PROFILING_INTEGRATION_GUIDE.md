# Performance Profiling Integration Guide

This guide shows you how to integrate the performance profiler into your CNC controller code.

## Quick Start

### Step 1: Add to platformio.ini

```ini
build_flags =
    -DPROFILING_ENABLED=1  # Set to 0 to disable profiling in production
```

### Step 2: Include in your code

```cpp
#include "performance_profiler.h"
```

### Step 3: Initialize in main.cpp

```cpp
void setup() {
    // ... existing initialization ...

    profilerInit();  // Add this line

    // ... rest of setup ...
}
```

---

## Example 1: Instrument Motion Update Loop

**File: `src/motion_control.cpp`**

```cpp
#include "performance_profiler.h"

// Define the profiling section once (at file scope)
PROFILE_DEFINE(motion_update, 500);  // Warn if exceeds 500µs (5% of 10ms budget)

void motionUpdate() {
    PROFILE_START(motion_update);  // Start timing

    if (!m_state.global_enabled) {
        PROFILE_STOP(motion_update);  // Don't forget to stop!
        return;
    }

    // ... existing motion code ...

    PROFILE_STOP(motion_update);  // Stop timing
}
```

**Better approach using RAII (automatic cleanup):**

```cpp
void motionUpdate() {
    PROFILE_FUNCTION(500);  // Automatically starts/stops timing

    if (!m_state.global_enabled) return;  // No need to manually stop

    // ... existing motion code ...
}
```

---

## Example 2: Profile Critical Sections

**File: `src/motion_control.cpp`**

```cpp
bool motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
    PROFILE_FUNCTION(200);  // Warn if exceeds 200µs

    // Profile individual sections for detailed analysis
    {
        PROFILE_SCOPE("Input Validation", 50);
        if (!m_state.global_enabled) return false;
        // ... validation code ...
    }

    {
        PROFILE_SCOPE("Coordinate Transform", 100);
        // ... coordinate calculations ...
    }

    {
        PROFILE_SCOPE("PLC Command", 150);
        motionSetPLCSpeedProfile(prof);
        motionSetPLCAxisDirection(target_axis, true, is_fwd);
    }

    return true;
}
```

---

## Example 3: Profile Safety Task

**File: `src/tasks_safety.cpp`**

```cpp
#include "performance_profiler.h"

PROFILE_DEFINE(safety_task, 250);  // Warn if exceeds 250µs (5% of 5ms budget)
PROFILE_DEFINE(safety_input_read, 50);

void taskSafetyFunction(void* parameter) {
    // ... initialization ...

    while (1) {
        PROFILE_START(safety_task);

        // 1. Internal safety checks
        safetyUpdate();

        // 2. Profile I2C input reading
        if (taskLockMutex(taskGetI2cMutex(), 2)) {
            PROFILE_START(safety_input_read);
            button_state_t btns = boardInputsUpdate();
            PROFILE_STOP(safety_input_read);

            taskUnlockMutex(taskGetI2cMutex());

            // ... handle buttons ...
        }

        PROFILE_STOP(safety_task);

        watchdogFeed("Safety");
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_SAFETY));
    }
}
```

---

## Example 4: Profile I2C Transactions

**File: `src/plc_iface.cpp`**

```cpp
#include "performance_profiler.h"

PROFILE_DEFINE(i2c_write, 100);  // Warn if I2C write takes > 100µs
PROFILE_DEFINE(i2c_read, 100);

static bool plcWriteI2C(uint8_t address, uint8_t data, const char* context) {
    PROFILE_START(i2c_write);

    uint8_t error = 0;
    for (int i = 0; i < I2C_RETRIES; i++) {
        Wire.beginTransmission(address);
        Wire.write(data);
        error = Wire.endTransmission();

        if (error == 0) {
            PROFILE_STOP(i2c_write);
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    logError("[PLC] I2C Write Failed (Addr 0x%02X, Err %d): %s", address, error, context);
    PROFILE_STOP(i2c_write);
    return false;
}

bool elboI73GetInput(uint8_t bit) {
    PROFILE_START(i2c_read);

    uint8_t count = Wire.requestFrom((uint8_t)ADDR_I73_INPUT, (uint8_t)1);

    if (count == 1) {
        i73_input_shadow = Wire.read();
    } else {
        // ... error handling ...
    }

    PROFILE_STOP(i2c_read);
    return (i73_input_shadow & (1 << bit));
}
```

---

## Example 5: Profile Encoder Reading

**File: `src/encoder_wj66.cpp`**

```cpp
#include "performance_profiler.h"

PROFILE_DEFINE(encoder_update, 500);  // Warn if exceeds 500µs (2.5% of 20ms)
PROFILE_DEFINE(encoder_parse, 200);

void wj66Update() {
    PROFILE_START(encoder_update);

    static uint32_t last_read = 0;

    if (millis() - last_read < WJ66_READ_INTERVAL_MS) {
        PROFILE_STOP(encoder_update);
        return;
    }
    last_read = millis();

    // Send command
    if (millis() - wj66_state.last_command_time > 200) {
        Serial1.print("#00\r");
        wj66_state.last_command_time = millis();
    }

    // Parse response
    {
        PROFILE_START(encoder_parse);
        while (Serial1.available() > 0) {
            // ... parsing logic ...
        }
        PROFILE_STOP(encoder_parse);
    }

    PROFILE_STOP(encoder_update);
}
```

---

## CLI Commands

Add profiling commands to your CLI:

**File: `src/cli_diag.cpp` (or create new `src/cli_profiler.cpp`)**

```cpp
#include "performance_profiler.h"

void cmd_profile(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("Usage: profile [report | reset | health]");
        return;
    }

    if (strcmp(argv[1], "report") == 0) {
        bool detailed = (argc > 2 && strcmp(argv[2], "all") == 0);
        profilerPrintReport(detailed);
    }
    else if (strcmp(argv[1], "reset") == 0) {
        profilerReset();
        Serial.println("[PROFILER] Statistics cleared");
    }
    else if (strcmp(argv[1], "health") == 0) {
        health_status_t health;
        profilerGetHealthStatus(&health);

        Serial.println("\n=== SYSTEM HEALTH ===");
        Serial.printf("Motion Loop:  %s\n", health.motion_loop_healthy ? "OK" : "SLOW");
        Serial.printf("Safety Loop:  %s\n", health.safety_loop_healthy ? "OK" : "SLOW");
        Serial.printf("Memory:       %s\n", health.memory_healthy ? "OK" : "LOW");
        Serial.printf("I2C Bus:      %s\n", health.i2c_healthy ? "OK" : "ERRORS");
        Serial.printf("Warnings/min: %lu\n", (unsigned long)health.warnings_last_minute);
    }
}

// Register in cliInit():
void cliInit() {
    // ... existing commands ...
    cliRegisterCommand("profile", cmd_profile, "Performance profiling tools");
}
```

---

## Expected Output

```
╔══════════════════════════════════════════════════════════════════╗
║            PERFORMANCE PROFILING REPORT                          ║
╚══════════════════════════════════════════════════════════════════╝

Section                  Calls      Avg(µs)   Min(µs)   Max(µs)   Warn
─────────────────────────────────────────────────────────────────────
motion_update             10234       124        87       847       3
safety_task               20468        63        42       223       0
encoder_update             5117       287       189       612       1
i2c_write                  8193        45        23       198       0
i2c_read                   8193        38        21       187       0
config_get                  523         8         5        34       0
PLC Command                5234        82        45       245       0

─────────────────────────────────────────────────────────────────────
Heap Free: 147824 bytes | Min: 145632 bytes | Max Block: 113920 bytes

╔══════════════════════════════════════════════════════════════════╗
║                        HEALTH STATUS                             ║
╚══════════════════════════════════════════════════════════════════╝
  Motion Loop:  ✓ OK
  Safety Loop:  ✓ OK
  Memory:       ✓ OK
  I2C Bus:      ✓ OK
  Warnings/min: 4
╚══════════════════════════════════════════════════════════════════╝
```

---

## Performance Analysis Tips

### 1. Identify Bottlenecks

Look for sections with high `Max(µs)` values:
- If `Max > 10× Avg`, investigate why (worst-case scenario)
- If `Max > warning threshold`, tune the threshold or optimize code

### 2. Monitor Budget Usage

**10ms Motion Loop:**
- Budget: 10,000 µs
- Avg: 124 µs = **1.2% usage** ✅
- Max: 847 µs = **8.5% usage** ✅
- Headroom: **91.5%** ✅

**5ms Safety Loop:**
- Budget: 5,000 µs
- Avg: 63 µs = **1.3% usage** ✅
- Max: 223 µs = **4.5% usage** ✅
- Headroom: **95.5%** ✅

### 3. Memory Health

- **Good:** Free heap > 100KB
- **Warning:** Free heap < 64KB
- **Critical:** Free heap < 32KB

### 4. Warning Trends

- 0-5 warnings/min = Normal operation
- 5-20 warnings/min = System under load
- 20+ warnings/min = Performance degradation

---

## Disable Profiling in Production

Set in `platformio.ini`:

```ini
build_flags =
    -DPROFILING_ENABLED=0  # Zero overhead when disabled
```

All profiling macros become no-ops (compile out completely).

---

## Advanced Usage

### Custom Sections

```cpp
// Register a custom section
static int my_section = profilerRegisterSection("Custom Work", 1000);

void myFunction() {
    profilerStart(my_section);
    // ... work ...
    profilerStop(my_section);
}
```

### Conditional Profiling

```cpp
#if PROFILING_ENABLED
    uint32_t start = micros();
#endif

    // ... critical code ...

#if PROFILING_ENABLED
    uint32_t elapsed = micros() - start;
    if (elapsed > 100) {
        Serial.printf("[PERF] Operation took %lu µs\n", elapsed);
    }
#endif
```

---

## Integration Checklist

- [ ] Add `performance_profiler.h` and `performance_profiler.cpp` to project
- [ ] Call `profilerInit()` in `setup()`
- [ ] Add `PROFILE_FUNCTION()` to `motionUpdate()`
- [ ] Add `PROFILE_FUNCTION()` to `taskSafetyFunction()`
- [ ] Add `PROFILE_DEFINE/START/STOP` to I2C operations
- [ ] Add `PROFILE_DEFINE/START/STOP` to encoder reading
- [ ] Add CLI command `profile report`
- [ ] Test: Run system for 60 seconds, check `profile report`
- [ ] Tune warning thresholds based on actual measurements
- [ ] Document expected performance in your README

---

## Troubleshooting

**Q: I see "Nested call detected" warnings**
- You called `PROFILE_START` twice without `PROFILE_STOP`
- Use `PROFILE_FUNCTION` for simpler automatic cleanup

**Q: Profiler shows 0 calls for a section**
- Section wasn't registered (check `profilerRegisterSection` return value)
- Code path never executed
- Profiling disabled at compile time

**Q: High warning counts but system seems fine**
- Thresholds too aggressive - increase them
- Occasional spikes are normal (GC, WiFi events)
- Check `Max` vs `Avg` - if Max is 10× Avg, it's rare spikes

**Q: Memory usage increasing over time**
- Not a profiler issue - you have a memory leak
- Check `Heap Min` in report - if steadily decreasing, investigate

---

## Next Steps

1. **Baseline Measurement:** Run system for 24 hours, save `profile report`
2. **Optimization:** Focus on highest `Avg` times first
3. **Stress Testing:** Monitor under maximum load
4. **Production:** Disable profiling or keep lightweight version

Happy profiling! 🚀
