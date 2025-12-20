# Gemini AI Improvement Roadmap

## Overview

This document analyzes Gemini AI's comprehensive 3-step improvement roadmap for the BISSO E350 CNC Controller firmware. Each recommendation is evaluated for:
- Current status (already implemented, needs implementation, or not applicable)
- Technical merit and impact
- Implementation complexity
- Priority level

---

## Step 1: Stability (The "Anti-Freeze" Patch)

### 1.1 Modify logError: Add ISR Context Check

**Gemini Recommendation:**
```cpp
// Add to serial_logger.cpp
void logError(const char* format, ...) {
    if (xPortInIsrContext()) return;  // ← Prevent ISR crashes

    va_list args; va_start(args, format);
    vlogPrint(LOG_LEVEL_ERROR, "[ERROR] ", format, args);
    va_end(args);
}
```

**Current Status:** ⚠️ **Not Implemented, But Not Needed**

**Analysis:**

| Aspect | Assessment |
|--------|------------|
| **Current Codebase** | NO ISR handlers exist (verified via grep) |
| **ISR Usage** | Motion control is task-based (10ms vTaskDelay loop) |
| **Logging Calls** | All from FreeRTOS task context (mutex-safe) |
| **Risk** | Negligible (no ISRs to call logging functions) |

**Evidence:**
```bash
$ grep -r "IRAM_ATTR\|attachInterrupt\|hw_timer_t" src/
# No results - NO ISR handlers in codebase
```

**Technical Merit:** ✅ **Good Defensive Programming Practice**

Even though there are no ISRs currently, adding this check:
- **Pros:** Future-proofs code if timer ISR added later
- **Cons:** Adds 1-2μs overhead per log call (negligible)
- **Impact:** Prevents silent failures if ISR accidentally calls logging

**Recommendation:** ⚠️ **OPTIONAL - Low Priority**

**Implementation:**
```cpp
// serial_logger.cpp (lines 45-48)
void logError(const char* format, ...) {
    // Defensive: Prevent ISR crashes (even though no ISRs exist)
    if (xPortInIsrContext()) return;

    va_list args; va_start(args, format);
    vlogPrint(LOG_LEVEL_ERROR, "[ERROR] ", format, args);
    va_end(args);
}
```

**Apply to all logging functions:**
- `logError()` ← Most critical (errors in ISR context)
- `logWarning()` ← Medium priority
- `logInfo()`, `logDebug()`, `logVerbose()` ← Low priority

**Priority:** Low (defensive programming, not fixing actual bug)

---

### 1.2 Fix WiFi: Remove While Loop

**Gemini Recommendation:** "Rewrite cli_wifi.cpp to remove the while loop."

**Current Status:** ✅ **ALREADY FIXED** (Commit b50a8d7)

**Verification:**
```bash
$ grep -n "while.*WiFi\|delay.*WiFi" src/cli_wifi.cpp
# No matches - blocking loop already removed!
```

**Evidence from Previous Commits:**

Commit `b50a8d7` (Security & Hardcoded Credentials):
- **Fixed:** Blocking WiFi connection in `cli_wifi.cpp`
- **Before:** `while (WiFi.status() != WL_CONNECTED) { delay(500); }` (10 second blocking loop)
- **After:** Non-blocking WiFi.begin(), background connection

**Current Implementation (`cli_wifi.cpp`):**
```cpp
// Non-blocking WiFi connection
WiFi.begin(ssid, password);
Serial.println("[WIFI] Connection started (non-blocking)");
// Connection happens in background, no blocking delay loop
```

**Action:** ✅ **None Required** - Already implemented

---

### 1.3 NVS Safety: Add Cooldown to Fault Logging

**Gemini Recommendation:** "In tasks_fault_log.cpp, ensure the faultLogToNVS function has a 'cooldown' so it doesn't burn out your flash memory if a sensor flickers and generates 1000 faults/second."

**Current Status:** ❌ **NOT IMPLEMENTED - Critical Flash Wear Risk**

**Analysis:**

**Current Implementation (`fault_logging.cpp:239-291`):**
```cpp
void faultLogToNVS(const fault_entry_t* entry) {
    // Auto-rotation to prevent NVS filling (lines 242-267)
    if (last_fault_id >= MAX_FAULT_ENTRIES_NVS) {
        // Delete oldest entry...
    }

    // ❌ NO COOLDOWN - Writes to NVS immediately every time!
    last_fault_id++;
    fault_prefs.putUChar(key, (uint8_t)entry->severity);  // NVS write
    fault_prefs.putUChar(key, (uint8_t)entry->code);      // NVS write
    fault_prefs.putInt(key, entry->axis);                 // NVS write
    fault_prefs.putInt(key, entry->value);                // NVS write
    fault_prefs.putString(key, entry->message);           // NVS write
    fault_prefs.putULong(key, entry->timestamp);          // NVS write
    fault_prefs.putUInt("last_id", last_fault_id);        // NVS write
    // 7 NVS writes per fault!
}
```

**Flash Wear Scenario:**

```
Flickering Sensor Example:
- Sensor reads NaN due to loose connection
- Safety system logs fault: "SENSOR_INVALID"
- Sensor recovers briefly
- Sensor reads NaN again (flicker)
- Fault logged again
- Repeat 1000 times/second...

Result: 7000 NVS writes/second × 60 seconds = 420,000 writes
ESP32 Flash Endurance: ~100,000 write cycles per sector
Flash Lifetime: Burned out in MINUTES!
```

**ESP32 Flash Characteristics:**
- Technology: SPI NOR flash
- Endurance: 10,000 - 100,000 erase/write cycles (typical)
- Wear Leveling: Enabled by NVS library (distributes writes)
- But: Excessive writes still cause premature wear

**Recommended Fix:**

```cpp
// Add to fault_logging.cpp
static uint32_t last_nvs_write_time[FAULT_CODE_MAX] = {0};
#define NVS_WRITE_COOLDOWN_MS 1000  // Max 1 write/second per fault type

void faultLogToNVS(const fault_entry_t* entry) {
    if (!entry) return;

    // ✅ COOLDOWN: Prevent flash wear from flickering sensors
    uint32_t now = millis();
    if ((now - last_nvs_write_time[entry->code]) < NVS_WRITE_COOLDOWN_MS) {
        // Skip NVS write, too soon since last write for this fault type
        logDebug("[FAULT] NVS write throttled: %s (cooldown)",
                 faultCodeToString(entry->code));
        return;
    }

    last_nvs_write_time[entry->code] = now;

    // ... existing NVS write logic ...
}
```

**Alternative: Rate Limiter per Severity**
```cpp
// Different cooldowns by severity
#define NVS_COOLDOWN_WARNING_MS  5000   // Warnings: 1 per 5 seconds
#define NVS_COOLDOWN_ERROR_MS    1000   // Errors: 1 per second
#define NVS_COOLDOWN_CRITICAL_MS 0      // Critical: Always write (no cooldown)
```

**Impact:**
- **Prevents:** Flash wear from flickering sensors
- **Trades off:** Some faults may not be logged to NVS if occurring too rapidly
- **But:** Serial log still captures all faults (no data loss for debugging)
- **Flash Lifetime:** Extended from months to years

**Recommendation:** ✅ **IMPLEMENT - High Priority**

**Files to Modify:**
1. `src/fault_logging.cpp` - Add cooldown mechanism
2. `include/fault_logging.h` - Add cooldown constant
3. Test with simulated flickering sensor

**Priority:** High (flash wear prevention)

---

## Step 2: Hardware Refactoring

### 2.1 Split the Buses: Move VFD to Separate I2C GPIO Pair

**Gemini Recommendation:** "Move VFD to a separate GPIO pair."

**Current Status:** ⚠️ **Requires Hardware Modification**

**Analysis:**

**Current I2C Architecture:**

| Device | Address | Bus | Priority |
|--------|---------|-----|----------|
| PLC (ELBO) | 0x20 | I2C0 (GPIO 21/22) | High (motion control) |
| Board Inputs | 0x21 | I2C0 (GPIO 21/22) | Highest (safety, E-Stop) |
| LCD | 0x27 | I2C0 (GPIO 21/22) | Low (display) |
| **VFD (Modbus)** | **Serial (not I2C!)** | **UART1** | High (spindle control) |

**Critical Discovery:** ✅ **VFD is NOT on I2C bus!**

VFD uses Modbus RTU over RS485 (Serial communication), not I2C:
- **Protocol:** Modbus RTU (serial, not I2C)
- **Physical:** RS485 differential pair
- **Interface:** ESP32 UART1 (GPIO 16/17 typical)
- **Separation:** Already independent of I2C bus!

**I2C Contention Analysis:**

Current separate I2C mutexes (`task_manager.cpp:48-50`):
```cpp
static SemaphoreHandle_t mutex_i2c_board = NULL;  // Board inputs (0x21)
static SemaphoreHandle_t mutex_i2c_plc = NULL;    // PLC interface (0x20)
static SemaphoreHandle_t mutex_lcd = NULL;        // LCD display (0x27)
```

**Benefit of Separate Mutexes:**
- Safety task (board inputs 0x21) does NOT block on Motion task (PLC 0x20)
- LCD task (0x27) does NOT block Safety or Motion tasks
- Already provides most benefits of separate buses

**Gemini's Recommendation Analysis:**

**If Gemini meant "separate physical I2C buses for PLC vs Board Inputs":**

Pros:
- Complete isolation (no shared bus contention)
- Safety task (board inputs) never delayed by PLC I/O

Cons:
- Requires hardware rewiring (ESP32 has 2 I2C peripherals, can use both)
- ESP32 I2C1 pins: GPIO 25/26 (example, configurable)
- Board layout change
- No benefit over current mutex architecture (contention already minimal)

**Recommendation:** ⚠️ **Not Recommended - Marginal Benefit**

**Current architecture is adequate:**
- Separate mutexes provide logical isolation
- Safety task uses 10ms I2C timeout (never blocks long)
- Priority inheritance prevents unbounded delays
- Physical bus split adds complexity without significant performance gain

**If future I2C contention becomes an issue:**
1. Measure actual I2C bus utilization (likely <10%)
2. Consider software optimizations first (reduce I2C transaction frequency)
3. Only then consider hardware split

**Priority:** Low (optimization, not critical)

---

### 2.2 Interrupt-Based Safety: Stop Polling Board Inputs

**Gemini Recommendation:** "In tasks_safety.cpp, stop polling boardInputs. Use an interrupt line from the I/O expander to wake the task only when a button is actually pressed."

**Current Status:** ⚠️ **Requires Hardware Support**

**Analysis:**

**Current Implementation (Polling-Based):**

```cpp
// tasks_safety.cpp
void taskSafetyFunction(void* parameter) {
    while (1) {
        // Poll board inputs every 5ms (200 Hz)
        button_state_t buttons = boardInputsUpdate();  // I2C read

        if (!buttons.connection_ok) {
            // Handle I2C failure...
        }

        if (buttons.e_stop_pressed) {
            safetyTriggerAlarm("E-STOP");
        }

        vTaskDelay(pdMS_TO_TICKS(5));  // 5ms polling interval
    }
}
```

**Polling Overhead:**
- Frequency: 200 Hz (every 5ms)
- I2C transaction: ~1-2ms per read
- CPU overhead: ~400 μs per poll (total ~80ms/sec = 8% CPU @ 200 Hz)

**Interrupt-Based Alternative:**

**Requirements:**
1. **Hardware:** I2C I/O expander must have interrupt output pin (INT or IRQ)
   - Common chips: PCF8574, MCP23017, TCA9534 (all have INT pin)
2. **Wiring:** INT pin connected to ESP32 GPIO (e.g., GPIO 34)
3. **Configuration:** I/O expander configured to assert INT on input change

**Implementation:**
```cpp
// Setup interrupt
pinMode(GPIO_BOARD_INPUT_INT, INPUT_PULLUP);
attachInterrupt(digitalPinToInterrupt(GPIO_BOARD_INPUT_INT),
                boardInputISR, FALLING);

// ISR - minimal work, signal task
void IRAM_ATTR boardInputISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(task_safety, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Task - wait for notification instead of polling
void taskSafetyFunction(void* parameter) {
    while (1) {
        // Block until interrupt wakes us up (0 CPU when idle)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));  // 100ms timeout

        // Read inputs (I2C) - only when INT triggered or timeout
        button_state_t buttons = boardInputsUpdate();

        if (buttons.e_stop_pressed) {
            safetyTriggerAlarm("E-STOP");
        }
    }
}
```

**Benefits:**
- **CPU Savings:** ~8% CPU reduction (no polling when idle)
- **Lower Latency:** Interrupt triggers immediately (~10μs) vs polling delay (up to 5ms)
- **Power Savings:** Task sleeps when no input changes (beneficial for battery operation)

**Drawbacks:**
- **Hardware Dependency:** Requires INT pin wiring (may not be available on current PCB)
- **Debouncing:** Need software debounce (buttons can bounce, trigger multiple interrupts)
- **Complexity:** More code (ISR + task notification + timeout fallback)
- **Edge Case:** If INT pin fails (wiring issue), no button detection (polling has no such failure mode)

**Hybrid Approach (Best Practice):**
```cpp
// Interrupt-driven with periodic fallback
while (1) {
    // Wait for interrupt OR 100ms timeout (fallback polling)
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

    // Read inputs (triggered by INT or timeout)
    button_state_t buttons = boardInputsUpdate();

    // ... handle buttons ...
}
```

**Benefits of Hybrid:**
- Fast response (interrupt-driven)
- Fault-tolerant (polling fallback if INT fails)
- Periodic health check (timeout ensures button state refreshed every 100ms)

**Recommendation:** ⚠️ **Optional - Medium Priority**

**Prerequisites:**
1. Verify I/O expander has INT pin (check hardware datasheet)
2. Verify INT pin is wired to ESP32 GPIO (check schematic)
3. If not wired, requires PCB rework (low priority)

**Priority:** Medium (optimization, requires hardware support)

---

## Step 3: Performance Tuning

### 3.1 Static JSON: Stream Directly to HTTP Buffer

**Gemini Recommendation:** "In api_config.cpp, stop constructing String objects. Stream directly to the HTTP buffer."

**Current Status:** ✅ **Partially Implemented** (StaticJsonDocument used, but not streaming)

**Analysis:**

**Current Implementation (`api_config.cpp`):**

We already converted to `StaticJsonDocument` in commit `e5f843e`:
```cpp
// api_config.cpp (current - after StaticJsonDocument fix)
void handleConfigGet(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> doc;  // ✅ Stack-allocated (no heap fragmentation)

    doc["category"] = category;
    doc["value"] = value;
    // ... populate JSON ...

    // ❌ Serialize to intermediate buffer, then send
    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));
    request->send(200, "application/json", buffer);
}
```

**Gemini's Optimization: Direct Streaming**

```cpp
// Gemini's approach: Stream directly to HTTP buffer (zero-copy)
void handleConfigGet(AsyncWebServerRequest* request) {
    AsyncResponseStream* response = request->beginResponseStream("application/json");

    StaticJsonDocument<512> doc;
    doc["category"] = category;
    doc["value"] = value;
    // ... populate JSON ...

    // ✅ Stream directly to HTTP buffer (no intermediate copy)
    serializeJson(doc, *response);
    request->send(response);
}
```

**Performance Comparison:**

| Approach | Heap Usage | RAM Usage | Speed | Code Clarity |
|----------|------------|-----------|-------|--------------|
| **DynamicJsonDocument** (old) | ❌ Heap alloc | High (fragmentation) | Medium | Good |
| **StaticJsonDocument + buffer** (current) | ✅ Stack | Medium (512 byte buffer) | Medium | Good |
| **Direct streaming** (Gemini) | ✅ Stack | Low (no intermediate buffer) | ✅ Fast | Good |

**Savings:**
- RAM: ~512 bytes per HTTP request (no intermediate buffer)
- Speed: ~200-500 μs per request (eliminate memcpy from buffer → HTTP)
- Heap: No change (already using StaticJsonDocument)

**Trade-offs:**
- Code changes required across multiple API handlers
- `AsyncWebServer` supports streaming (confirmed in library)
- Benefit: Marginal (512 bytes RAM per request, 200-500 μs speed)

**Recommendation:** ⚠️ **Optional - Low Priority**

**Current implementation is adequate:**
- No heap fragmentation (StaticJsonDocument)
- 512 byte stack buffer is negligible (ESP32 has 320KB RAM)
- Speed difference negligible for HTTP API (network latency >> 500 μs)

**If Implementing:**
- Apply to all API handlers consistently
- Estimate: 10-20 file modifications
- Testing: Verify AsyncWebServer streaming works correctly

**Priority:** Low (micro-optimization)

---

### 3.2 Task Priorities: Revised Priority Scheme

**Gemini Recommendation:**

```
Motion:    Priority 20 (Core 1)
Encoder:   Priority 19 (Core 1)
Safety:    Priority 18 (Core 1)
WiFi/Web:  Priority 5  (Core 0)
LCD:       Priority 3  (Core 0)
```

**Current Priorities (`task_manager.h:13-24`):**

```
Safety:       24 (Highest - Core 1)
Motion:       22 (Core 1)
Encoder:      20 (Core 1)
PLC_Comm:     18 (Core 1) ← Ghost task
I2C_Manager:  17 (Core 1)
CLI:          15 (Core 0 or 1)
Fault_Log:    14 (Core 0)
Monitor:      12 (Core 0)
Telemetry:    11 (Core 0)
LCD_Format:   10 (Core 0)
LCD:           9 (Core 0)
```

**Comparison:**

| Task | Current | Gemini | Change | Rationale |
|------|---------|--------|--------|-----------|
| **Safety** | 24 (highest) | 18 | ⬇️ -6 | Gemini: Motion more critical |
| **Motion** | 22 | 20 | ⬇️ -2 | Gemini: Highest priority |
| **Encoder** | 20 | 19 | ⬇️ -1 | Slight decrease |
| **LCD** | 9 | 3 | ⬇️ -6 | Much lower priority |
| **WiFi/Web** | N/A (AsyncTCP) | 5 | N/A | Web runs on Core 0 |

**Analysis: Motion Priority 20 (Gemini) vs Safety Priority 24 (Current)**

**Current Architecture Rationale (Safety Highest):**

```
Priority 24: Safety
- E-Stop button monitoring (5ms cycle)
- Stall detection (encoder error detection)
- VFD thermal monitoring
- Axis quality validation

Priority 22: Motion
- Motion execution (10ms cycle)
- PLC I/O (axis direction, speed profile)
- Motion buffer pop/execute
```

**Why Safety is Highest Priority (Current):**

1. **E-Stop Responsiveness:** E-Stop button must be detected within 5ms
   - User presses E-Stop → Safety task reads button (I2C) → Triggers alarm
   - If Motion preempts Safety during E-Stop press, response delayed by 10ms

2. **Stall Detection:** Safety monitors encoder errors every 5ms
   - If Motion is highest priority and runs long, stall detection delayed

3. **Defense-in-Depth:** Safety is supervisor over Motion
   - Safety can halt Motion (motionEmergencyStop)
   - Motion should not be able to preempt Safety checks

**Gemini's Architecture Rationale (Motion Highest):**

```
Priority 20: Motion (highest)
- Real-time motion execution must not be delayed
- Motion timing is critical for smooth movement
- Safety can tolerate slight delays (E-Stop has physical lag anyway)
```

**Counter-argument:**
- Motion timing: 10ms cycle is NOT hard real-time (no sub-ms jitter requirements)
- E-Stop latency: Additional 10ms delay (if Motion preempts Safety) is significant
- Safety supervision: Should have higher priority than controlled system

**Recommendation:** ✅ **Keep Current Priorities - No Change**

**Current priority scheme is correct for safety-critical system:**

| Principle | Current (Correct) | Gemini (Questionable) |
|-----------|-------------------|----------------------|
| Safety First | ✅ Safety highest (24) | ❌ Motion highest (20) |
| Supervisor Pattern | ✅ Safety supervises Motion | ❌ Motion can preempt Safety |
| E-Stop Latency | ✅ 5ms (Safety priority) | ❌ Up to 15ms (Motion preempts) |
| Industry Standard | ✅ Safety > Motion | ❌ Uncommon |

**Exception: Hard Real-Time Motion Control**

If motion control requires sub-millisecond jitter (e.g., laser cutting, high-speed machining):
- Motion priority could be higher
- But: Current system uses 10ms cycle (not hard real-time)
- Safety 5ms + Motion 10ms is adequate for CNC positioning

**Priority:** ❌ **No Change Recommended** - Current priorities are correct

---

## Summary: Gemini Roadmap Assessment

| Recommendation | Status | Priority | Action |
|----------------|--------|----------|--------|
| **1.1 ISR Context Check in logError** | ⚠️ Optional | Low | Add defensive check |
| **1.2 Fix WiFi While Loop** | ✅ Already Done | N/A | None |
| **1.3 NVS Cooldown** | ❌ Not Impl | ✅ **High** | **Implement** |
| **2.1 Split I2C Buses** | ⚠️ Hardware | Low | Not recommended |
| **2.2 Interrupt-Based Safety** | ⚠️ Hardware | Medium | If INT pin available |
| **3.1 Stream JSON to HTTP** | ⚠️ Optional | Low | Micro-optimization |
| **3.2 Revise Task Priorities** | ❌ Incorrect | N/A | **Keep current** |

**High Priority Actions:**
1. ✅ **Implement NVS Cooldown** - Prevents flash wear (critical)

**Medium Priority Actions:**
2. ⚠️ **Interrupt-Based Safety** - If hardware supports (check PCB)

**Low Priority Actions:**
3. ⚠️ **ISR Context Check** - Defensive programming (future-proofing)
4. ⚠️ **Stream JSON** - Micro-optimization (marginal benefit)

**Not Recommended:**
- ❌ **Split I2C Buses** - Current mutex architecture adequate
- ❌ **Revise Task Priorities** - Current priorities correct for safety-critical system

---

**Last Updated:** 2025-01-XX
**Architecture:** ESP32 FreeRTOS-based CNC Controller
**Maintained by:** BISSO Development Team
