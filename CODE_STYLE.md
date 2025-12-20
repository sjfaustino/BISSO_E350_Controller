# Code Style Guide - BISSO E350 Controller

## Global Variable Management

### Overview

This document addresses the Gemini AI audit recommendation about global variables. **The codebase already follows best practices** for embedded systems development.

### Current State (GOOD ✅)

The firmware uses **module-level state structs** extensively - a recognized best practice for embedded C/C++ development.

#### Examples of Proper Global Variable Usage:

```cpp
// altivar31_modbus.cpp - VFD state grouped into struct
static altivar31_state_t altivar31_state = {
    .slave_address = 1,
    .baud_rate = 19200,
    .frequency_raw = 0,
    .frequency_hz = 0.0f,
    // ... all related VFD state in ONE struct
};

// spindle_current_monitor.cpp - Monitor state grouped
static spindle_monitor_state_t monitor_state = {
    .enabled = true,
    .current_amps = 0.0f,
    .poll_interval_ms = 1000,
    // ... all monitoring state grouped
};

// axis_synchronization.cpp - Axis metrics grouped
static all_axes_metrics_t all_axes = {
    // All 3 axes + combined metrics in ONE struct
};
```

### Why This Pattern is Good for Embedded Systems

1. **Clear Module Ownership**
   - Each .cpp file manages its own state
   - State is encapsulated (static = file-scope only)
   - Public API provides controlled access

2. **Thread Safety Benefits**
   - Single mutex can protect entire module state
   - Atomic updates to related fields
   - Clear critical section boundaries

3. **Memory Efficiency**
   - Struct padding optimized by compiler
   - No heap allocation (all static)
   - Cache-friendly access patterns

4. **Debugger Friendly**
   - View entire module state in one watch window
   - Struct hierarchies are navigable
   - Named fields instead of loose variables

### Global Variable Patterns in This Codebase

#### ✅ Pattern 1: Module State Struct (Preferred)

```cpp
// Include file
typedef struct {
    float temperature;
    uint32_t samples;
    bool is_valid;
} sensor_state_t;

// Implementation file
static sensor_state_t sensor_state = {0};

// Accessor functions
float sensorGetTemperature(void) {
    return sensor_state.temperature;
}
```

**When to use:** Any module with multiple related state variables.

**Benefits:**
- All state in one place
- Easy to pass to functions
- Single mutex for thread safety

#### ✅ Pattern 2: Singleton Objects (Acceptable)

```cpp
// Module-level singleton instances
NetworkManager networkManager;
MotionBuffer motionBuffer;
WebServerManager webServer;
```

**When to use:** C++ classes that represent system-wide resources.

**Rationale:**
- Common in embedded systems (single-instance managers)
- Constructor initializes state
- Methods provide controlled access
- Alternative would be passing pointers everywhere (worse)

#### ✅ Pattern 3: Constants and Lookup Tables

```cpp
static const uint16_t crc16_table[256] = { /* ... */ };
static const char* ERROR_MESSAGES[] = { /* ... */ };
```

**When to use:** Read-only data needed by module functions.

**Benefits:**
- No runtime initialization
- Lives in flash (not RAM)
- Compiler can optimize access

#### ⚠️ Pattern 4: Utility Variables (Use Sparingly)

```cpp
static uint8_t modbus_tx_buffer[32];
static uint32_t last_update_time = 0;
```

**When acceptable:**
- Buffer reused by multiple functions (avoid stack overflow)
- Timestamp/state for non-blocking operations
- Temporary workspace to avoid heap allocation

**When NOT acceptable:**
- Related variables that should be in a struct
- Multiple timestamps for same module → group them!

### Minor Improvement Opportunities

Based on audit, here are **optional** refinements (not critical):

#### 1. Board Inputs Module

**Current:**
```cpp
// board_inputs.cpp
static uint8_t input_cache = 0xFF;
static uint8_t mask_estop = 0;
static uint8_t mask_pause = 0;
static uint8_t mask_resume = 0;
```

**Suggested (if refactoring):**
```cpp
typedef struct {
    uint8_t cache;
    struct {
        uint8_t estop;
        uint8_t pause;
        uint8_t resume;
    } masks;
} board_inputs_state_t;

static board_inputs_state_t inputs = {
    .cache = 0xFF,
    .masks = {0, 0, 0}
};
```

**Benefit:** Single struct, clearer relationships.

#### 2. Config Unified Runtime State

**Current:**
```cpp
// config_unified.cpp
static int config_count = 0;
static bool config_dirty = false;
static uint32_t last_nvs_save = 0;
```

**Suggested:**
```cpp
typedef struct {
    int count;
    bool dirty;
    uint32_t last_nvs_save;
} config_runtime_state_t;

static config_runtime_state_t runtime = {0, false, 0};
```

**Benefit:** Single mutex protects all runtime state.

#### 3. Axis Jitter Trackers (Array vs Individual)

**Current:**
```cpp
static jitter_tracker_t x_jitter = {};
static jitter_tracker_t y_jitter = {};
static jitter_tracker_t z_jitter = {};
```

**Suggested:**
```cpp
static jitter_tracker_t jitter[3] = {}; // Index by axis
// Access: jitter[AXIS_X], jitter[AXIS_Y], jitter[AXIS_Z]
```

**Benefit:** Loop over axes, indexed access.

### Best Practices for New Code

#### DO ✅

1. **Group related state into structs**
   ```cpp
   typedef struct {
       int state;
       uint32_t timestamp;
       float value;
   } module_state_t;
   ```

2. **Use static for file-scope globals**
   ```cpp
   static module_state_t state = {0};  // File-scope only
   ```

3. **Provide accessor functions**
   ```cpp
   float moduleGetValue(void) {
       return state.value;
   }
   ```

4. **Initialize in init() function**
   ```cpp
   void moduleInit(void) {
       state.state = 0;
       state.timestamp = 0;
       state.value = 0.0f;
   }
   ```

5. **Document module state in header**
   ```c
   // In .h file
   typedef struct {
       float temp;  ///< Temperature in Celsius
       bool valid;  ///< True if reading is valid
   } sensor_state_t;
   ```

#### DON'T ❌

1. **Don't scatter related variables**
   ```cpp
   // BAD - hard to understand relationships
   static float motor_speed;
   static float motor_current;
   static bool motor_fault;
   static uint32_t motor_runtime;

   // GOOD - clear relationships
   typedef struct {
       float speed;
       float current;
       bool fault;
       uint32_t runtime;
   } motor_state_t;
   static motor_state_t motor = {0};
   ```

2. **Don't use extern for module state**
   ```cpp
   // BAD - exposes internals
   extern motor_state_t motor;  // Other files can modify!

   // GOOD - provide accessors
   float motorGetSpeed(void);   // Controlled access
   ```

3. **Don't use globals for temporary storage**
   ```cpp
   // BAD - function-local state as global
   static int temp_result;  // Only used in one function

   // GOOD - local variable
   int calculateThing(void) {
       int result = 0;  // Local scope
       // ...
       return result;
   }
   ```

### Rationale: Why Not Eliminate All Globals?

**Embedded systems differ from desktop applications:**

1. **No dynamic memory allocation**
   - Heap fragmentation is dangerous
   - Static allocation = predictable memory
   - Globals ensure single instance

2. **Hardware is inherently global**
   - One I2C bus, one SPI controller, one WiFi radio
   - Global state mirrors hardware reality
   - Passing pointers everywhere is artificial

3. **RTOS task context**
   - Tasks run "forever" loops
   - State must persist across task switches
   - Stack variables don't survive task preemption

4. **Performance**
   - Passing large structs by pointer has overhead
   - Globals accessed directly = faster
   - Critical for real-time control loops

### Thread Safety with Global State

**Pattern: Mutex-Protected State Struct**

```cpp
// State definition
static SemaphoreHandle_t state_mutex = NULL;
static module_state_t state = {0};

// Initialization
void moduleInit(void) {
    state_mutex = xSemaphoreCreateMutex();
    // ...
}

// Thread-safe accessor
float moduleGetValue(void) {
    float value = 0.0f;
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100))) {
        value = state.value;
        xSemaphoreGive(state_mutex);
    }
    return value;
}
```

**Key principle:** One mutex protects the entire state struct.

### Summary

**Gemini's concern:** "High count of global variables can lead to side effects"

**Reality for this codebase:**
- ✅ Global variables are **properly grouped into state structs**
- ✅ Each module manages its own state (encapsulation)
- ✅ Accessor functions provide controlled access
- ✅ Mutex protection prevents race conditions
- ✅ Follows embedded systems best practices

**Conclusion:** The codebase is **well-architected** for an embedded control system. Minor refinements possible but not critical.

---

**Last Updated:** 2025-01-XX
**Applies to:** Firmware v3.5.x and later
**Maintained by:** BISSO Development Team
