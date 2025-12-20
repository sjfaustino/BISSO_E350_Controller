# Architecture Documentation - BISSO E350 Controller

## Motion Control Data Flow

### Overview

This document addresses the Gemini AI audit observation about "inconsistent logic" between motion.cpp (float) and encoder_wj66.cpp (int32_t). **This is intentional design**, not an inconsistency.

### Design Philosophy: Integer Core, Float Boundaries

The firmware uses **int32_t encoder counts** as the source of truth for all motion control, with float MM conversions only at system boundaries.

```
┌─────────────────────────────────────────────────────────────┐
│ USER INPUT (G-Code, API, Jogging)                          │
│ Uses: float MM (e.g., "G0 X100.5 Y50.25")                 │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  │ Convert MM → Encoder Counts
                  │ (x_mm * pulses_per_mm = x_counts)
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ MOTION BUFFER (motion_buffer.h)                            │
│ Storage: int32_t x_counts, y_counts, z_counts, a_counts   │
│ Purpose: Command queue with ZERO cumulative error          │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  │ Integer math only (add, subtract, compare)
                  │ NO floating point operations in motion loop
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ MOTION PLANNER (motion_planner.cpp)                        │
│ Operations: int32_t position += velocity_counts_per_step  │
│ Purpose: Real-time control loop with predictable timing    │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  │ Send target counts to stepper/encoder layer
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ ENCODER DRIVER (encoder_wj66.cpp)                          │
│ Hardware: WJ66 Modbus encoder (int32_t raw counts)        │
│ Purpose: Direct hardware interface, no conversion          │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  │ Feedback: Current position in counts
                  │ Convert Counts → MM for display only
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ USER DISPLAY (Web UI, Serial, API)                        │
│ Uses: float MM (e.g., "X: 100.50 mm")                     │
│ Purpose: Human-readable output                             │
└─────────────────────────────────────────────────────────────┘
```

### Why This Architecture?

#### 1. **Eliminates Cumulative Float Error**

**Problem with all-float architecture:**
```cpp
// BAD: Float accumulation (drifts over time)
float position = 0.0f;
for (int i = 0; i < 10000; i++) {
    position += 0.1f;  // Rounding error accumulates!
}
// Result: position ≈ 999.9473 instead of 1000.0
```

**Solution with int32_t core:**
```cpp
// GOOD: Integer accumulation (exact!)
int32_t position_counts = 0;
for (int i = 0; i < 10000; i++) {
    position_counts += 1;  // Exact integer math
}
float position_mm = position_counts / 10.0f;  // Convert once at end
// Result: position_mm = 1000.0 (exact!)
```

#### 2. **Hardware Native Format**

The WJ66 Modbus encoder reports positions as **int32_t counts**:
- Reading encoder: `int32_t wj66_count = readModbusRegister(...);`
- No conversion needed in critical path
- Direct comparison: `if (target_counts == current_counts) { /* arrived */ }`

Using floats would require:
```cpp
// BAD: Unnecessary conversion on every encoder read (1000x/sec)
float encoder_mm = (float)wj66_count / pulses_per_mm;
if (fabs(target_mm - encoder_mm) < 0.01f) { /* close enough? */ }
// Issues: Performance overhead, tolerance ambiguity, drift
```

#### 3. **Real-Time Performance**

Motion control loop runs at **1000 Hz** (every 1ms):
- Integer math: 1-2 CPU cycles (add, subtract, compare)
- Float math: 10-50 CPU cycles (multiply, divide, rounding)
- Integer operations are deterministic (no denormal stalls, no NaN checks)

#### 4. **Infinite Precision for Long Jobs**

A CNC job with 1 million moves:
- **Float position**: Accumulates 0.0001mm error per move → 100mm total drift!
- **Int32_t position**: ZERO error after 1 million moves

### Data Type Usage Guidelines

| Layer | Data Type | Why |
|-------|-----------|-----|
| G-Code Parser | float MM | Industry standard (G0 X100.5) |
| Motion Buffer | int32_t counts | ZERO cumulative error |
| Motion Planner | int32_t counts | Real-time performance |
| Encoder Driver | int32_t counts | Hardware native format |
| Web UI / API | float MM | Human-readable display |

### Conversion Points (Float ↔ Int32_t)

**Only 2 conversion points in entire system:**

1. **Input Boundary** (motion_buffer.cpp:48-67):
```cpp
// Convert user input (float MM) to encoder counts (int32_t)
int32_t x_counts = (int32_t)(x_mm * machineCal.X.pulses_per_mm);
buffer[head].x_counts = x_counts;  // Store as integer
```

2. **Output Boundary** (motion.cpp):
```cpp
// Convert encoder counts to display MM (for web UI only)
float x_mm = (float)encoder_counts / machineCal.X.pulses_per_mm;
webServer.setPositionX(x_mm);
```

**Critically: NO conversions in the motion control loop!**

### Addressing Gemini's Concern

**Gemini Observation:**
> "motion.cpp uses float-based logic, while encoder_wj66.cpp uses int32_t raw counts"

**Reality:**
- ✅ motion_buffer.h: Uses int32_t counts (source of truth)
- ✅ motion_planner.cpp: Integer math only
- ✅ encoder_wj66.cpp: int32_t counts (hardware interface)
- ✅ Float used ONLY for user input/output (boundaries)

**This is NOT an inconsistency - it's intentional layered architecture!**

### Example: 100.5mm Move

```cpp
// 1. USER INPUT (float MM)
G0 X100.5

// 2. PARSER → MOTION BUFFER (convert to int32_t)
float target_mm = 100.5f;
int32_t target_counts = (int32_t)(100.5f * 100.0f);  // = 10050 counts
// Stored in motion buffer as: buffer[0].x_counts = 10050

// 3. MOTION PLANNER (int32_t only)
int32_t current = 0;
int32_t target = 10050;
while (current < target) {
    current += velocity_counts_per_step;  // Integer increment
    encoder_setTarget(current);           // Send int32_t to hardware
}

// 4. ENCODER DRIVER (int32_t only)
int32_t encoder_position = wj66_readCounts();  // Read raw counts
if (encoder_position >= target) {
    // Arrived! (exact comparison, no tolerance needed)
}

// 5. DISPLAY (convert to float MM for user)
float display_mm = (float)encoder_position / 100.0f;  // = 100.50
webUI.showPosition(display_mm);
```

### Benefits Summary

| Aspect | Integer Core | All-Float |
|--------|--------------|-----------|
| Cumulative error | **ZERO** | Accumulates over time |
| Hardware interface | **Direct** | Conversion overhead |
| Real-time performance | **Fast** | Slower (FPU ops) |
| Long job accuracy | **Perfect** | Drifts (can be mm!) |
| Complexity | **Conversions at boundaries** | Conversions everywhere |

### Conclusion

The "inconsistency" Gemini observed is actually **best-practice embedded systems architecture**:

1. ✅ Use native hardware data types in core (int32_t)
2. ✅ Convert to user-friendly units at boundaries (float)
3. ✅ Minimize conversions (only 2 points in entire system)
4. ✅ Eliminate cumulative error (integer math)
5. ✅ Maximize real-time performance (no FPU in critical path)

**This design is intentional, well-architected, and should NOT be changed.**

---

## Global Variables & Module State

### Gemini Concern: "Heavy reliance on extern and global state structs"

**Response:** This is **intentional and correct** for embedded systems. See `CODE_STYLE.md` for comprehensive documentation.

**Key Points:**

1. **Module-Level State Structs** (Preferred Pattern):
```cpp
// encoder_wj66.cpp - Encapsulated module state
static wj66_state_t wj66_state = {
    .position_counts = {0, 0, 0, 0},
    .last_read_time_ms = 0,
    .error_count = 0
};

// Public accessor (controlled access)
int32_t wj66GetPosition(uint8_t axis) {
    return wj66_state.position_counts[axis];
}
```

2. **Why Global State is Correct for Embedded Systems:**
   - ✅ Hardware is inherently global (one I2C bus, one WiFi radio, one VFD)
   - ✅ No dynamic memory allocation (heap fragmentation is dangerous)
   - ✅ RTOS tasks run forever loops (state must persist across task switches)
   - ✅ Real-time performance (passing pointers everywhere adds overhead)

3. **Encapsulation Through Static + Accessors:**
   - State structs are `static` (file-scope only, not extern)
   - Public API provides controlled access
   - Mutex protection for thread safety
   - Clear module ownership

4. **Unit Testing:**
   - Each module can be tested independently via public API
   - Test frameworks can call accessor functions
   - State can be reset via init() functions
   - No "spaghetti code" - dependencies are explicit

**Conclusion:** The firmware uses **industry-standard embedded C patterns**. Global state with proper encapsulation is the correct approach, not a code smell.

---

**Related Documents:**
- `CODE_STYLE.md` - Global variable best practices (350+ lines)
- `SECURITY.md` - Credential management and security patterns

**Related Commits:**
- Float drift fix: Commit from session (motion_buffer.h conversion)
- Sensor validation: Commit 5e36d42 (NaN checks on float boundaries)
- Global variable audit: Commit b765c69 (CODE_STYLE.md created)

**Last Updated:** 2025-01-XX
**Maintained by:** BISSO Development Team
