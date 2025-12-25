# Audit Quick Reference - Action Items

## 🔴 HIGH Priority (Do First - ~20 hours)

### 1. Config API Input Validation (Finding 5.2)
**File:** `src/web_server.cpp:998`
**Risk:** Arbitrary NVS key writes
**Fix:**
```cpp
// Add before line 998
static const char* VALID_CONFIG_KEYS[] = {
    KEY_WIFI_SSID, KEY_WIFI_PASS, KEY_X_LIMIT_MIN,
    KEY_X_LIMIT_MAX, KEY_Y_LIMIT_MIN, KEY_Y_LIMIT_MAX,
    // ... add all config_keys.h constants
};

bool isValidConfigKey(const char* key) {
    for (int i = 0; i < sizeof(VALID_CONFIG_KEYS)/sizeof(char*); i++) {
        if (strcmp(key, VALID_CONFIG_KEYS[i]) == 0) return true;
    }
    return false;
}

// Then at line 1001:
if (!isValidConfigKey(key)) {
    request->send(400, "application/json",
                  "{\"error\":\"Invalid configuration key\"}");
    return;
}
```

### 2. Stack Watermark Monitoring (Finding 2.2)
**File:** `src/tasks_monitor.cpp:64`
**Risk:** Stack overflow crashes
**Enhancement:**
```cpp
// Existing code is good, but add persistent logging
if (watermark < 512) {  // <2KB free
    logError("[MONITOR] [CRIT] Stack near overflow: %s (%lu words free)",
             tasks[i].name, (unsigned long)watermark);

    // NEW: Log to NVS for post-crash analysis
    faultLogEntry(FAULT_CRITICAL, FAULT_TASK_HUNG, i, watermark,
                  "Stack overflow imminent: %s", tasks[i].name);
}
```

### 3. I2C Health Check Optimization (Finding 3.2)
**File:** `src/motion_control.cpp:269-295`
**Risk:** Motion loop delays
**Fix:**
```cpp
// MOVE this entire block to src/tasks_monitor.cpp
// Check at 1Hz instead of 1000Hz
void taskMonitorFunction(void* parameter) {
    while(1) {
        // ... existing code ...

        // NEW: I2C health check (once per second)
        if (elboIsShadowRegisterDirty()) {
            logError("[MONITOR] CRITICAL: PLC I2C failure");
            systemEventsSystemSet(EVENT_SYSTEM_I2C_FAILURE);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// In motion_control.cpp, replace lines 269-295 with:
EventBits_t events = systemEventsGetSystemStatus();
if (events & EVENT_SYSTEM_I2C_FAILURE) {
    motionEmergencyStop();
    return;
}
```

### 4. Watchdog Verification Test (Finding 4.3)
**File:** `src/cli_diag.cpp`
**Risk:** Watchdog might not bite
**Add:**
```cpp
void cmd_test_watchdog(int argc, char** argv) {
    Serial.println("[TEST] ⚠️  INTENTIONAL HANG - Testing watchdog...");
    Serial.println("[TEST] Expected: System resets in ~10 seconds");
    Serial.println("[TEST] If no reset, watchdog is NOT working!");

    watchdogFeed("Test");  // Feed once

    // Never feed again - should trigger reset
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Add to cliRegisterCommand():
cliRegisterCommand("test_watchdog", "Test watchdog reset", cmd_test_watchdog);
```

### 5. I2C Bus Recovery (Finding 8.2)
**File:** `src/plc_iface.cpp:266`
**Risk:** Permanent E-STOP on I2C glitch
**Add:**
```cpp
#include <Wire.h>

bool i2cBusRecover() {
    logWarning("[I2C] Attempting bus recovery...");

    // Toggle SCL 9 times to clear stuck slave
    pinMode(I2C_SCL_PIN, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, HIGH);
        delayMicroseconds(5);
    }

    // Reinitialize I2C
    Wire.end();
    vTaskDelay(pdMS_TO_TICKS(10));
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);

    logInfo("[I2C] Bus recovery complete");
    return true;
}

// Call before emergency stop at line 275:
if (elboIsShadowRegisterDirty()) {
    // NEW: Try recovery first
    if (i2cBusRecover()) {
        // Verify recovery worked
        if (!elboIsShadowRegisterDirty()) {
            logInfo("[MOTION] I2C recovered successfully");
            last_i2c_check_ms = millis();
            return;  // Continue normal operation
        }
    }

    // Recovery failed - emergency stop
    logError("[MOTION] CRITICAL: PLC I2C communication failure");
    // ... existing emergency stop code ...
}
```

---

## 🟡 MEDIUM Priority (Do Next - ~20 hours)

### 6. Spinlock → Mutex Migration (Finding 1.3)
**Files:** `src/motion_control.cpp:48, 180, 349`, etc.
**Risk:** Real-time performance degradation
**Strategy:**
```cpp
// STEP 1: Audit spinlock duration
// Add timing to all critical sections:
uint32_t start = micros();
portENTER_CRITICAL(&motionSpinlock);
// ... protected code ...
portEXIT_CRITICAL(&motionSpinlock);
uint32_t duration = micros() - start;
if (duration > 10) {
    logWarning("[MOTION] Long critical section: %lu us", duration);
}

// STEP 2: Replace if >10us
// Before:
portENTER_CRITICAL(&motionSpinlock);
int32_t pos = axes[axis].position;
portEXIT_CRITICAL(&motionSpinlock);

// After:
xSemaphoreTake(motion_mutex, portMAX_DELAY);
int32_t pos = axes[axis].position;
xSemaphoreGive(motion_mutex);
```

### 7. G-code Command Validation (Finding 5.3)
**File:** `src/web_server.cpp:1232`
**Risk:** Command injection
**Fix:**
```cpp
bool isValidGcode(const char* cmd) {
    // Whitelist approach
    if (strncmp(cmd, "G0 ", 3) == 0) return true;   // Rapid move
    if (strncmp(cmd, "G1 ", 3) == 0) return true;   // Linear move
    if (strncmp(cmd, "G28", 3) == 0) return true;   // Home
    if (strncmp(cmd, "G92", 3) == 0) return true;   // Set position
    if (strncmp(cmd, "M3", 2) == 0) return true;    // Spindle on
    if (strncmp(cmd, "M5", 2) == 0) return true;    // Spindle off
    return false;
}

// Add at line 1232:
if (!isValidGcode(command)) {
    request->send(400, "application/json",
                  "{\"error\":\"Invalid G-code command\"}");
    return;
}
```

### 8. NVS Flash Wear Protection (Finding 2.3)
**File:** `src/fault_logging.cpp:228`
**Risk:** Premature flash wear
**Enhancement:**
```cpp
// Add fault rate tracking
static uint32_t fault_timestamps[10] = {0};
static uint8_t fault_timestamp_idx = 0;

uint32_t getFaultRate() {
    uint32_t now = millis();
    fault_timestamps[fault_timestamp_idx] = now;
    fault_timestamp_idx = (fault_timestamp_idx + 1) % 10;

    // Calculate faults per second (last 10 faults)
    uint32_t oldest = fault_timestamps[fault_timestamp_idx];
    if (oldest == 0) return 0;

    uint32_t time_span = now - oldest;
    return (10 * 1000) / (time_span + 1);  // faults/sec
}

// Modify cooldown at line 234:
uint32_t faults_per_sec = getFaultRate();
uint32_t cooldown = (faults_per_sec > 5)
    ? 10000  // 10 seconds during fault storm
    : 1000;  // 1 second normal

if (time_since_last_write < cooldown) return;
```

---

## 🟢 LOW Priority (Long-term - ~20 hours)

### 9. Magic Number Elimination (Finding 6.1)
**Files:** Multiple
**Risk:** Maintainability
**Create:** `include/system_tuning.h`
```cpp
#ifndef SYSTEM_TUNING_H
#define SYSTEM_TUNING_H

// Motion Control Timeouts
#define MOTION_MUTEX_TIMEOUT_BASE_MS 100
#define MOTION_MUTEX_TIMEOUT_ESCALATION 20
#define MOTION_POSITION_TOLERANCE_COUNTS 1

// Safety Thresholds
#define SAFETY_QUALITY_THRESHOLD_CRITICAL 25
#define SAFETY_QUALITY_THRESHOLD_WARNING 50
#define SAFETY_STALL_CHECK_INTERVAL_MS 500

// Fault Logging
#define NVS_WRITE_COOLDOWN_NORMAL_MS 1000
#define NVS_WRITE_COOLDOWN_STORM_MS 10000
#define FAULT_STORM_THRESHOLD_PER_SEC 5

#endif
```

### 10. Edge Case Testing (Finding 7.2)
**File:** `test/test_stress.cpp` (new file)
**Risk:** Hidden bugs
```cpp
#include "unity.h"
#include "motion.h"
#include "safety.h"

void test_concurrent_motion_commands() {
    for (int i = 0; i < 1000; i++) {
        float x = (float)(rand() % 100);
        float y = (float)(rand() % 100);
        motionMoveAbsolute(x, y, 0, 0, 100);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Verify no crashes, no stuck states
    TEST_ASSERT_TRUE(!motionIsEmergencyStopped());
    TEST_ASSERT_TRUE(!safetyIsAlarmed());
}

void test_queue_overflow() {
    // Fill fault queue
    for (int i = 0; i < 200; i++) {
        faultLogEntry(FAULT_WARNING, FAULT_MOTION_STALL, 0, i, "Test");
    }

    // Verify ring buffer fallback worked
    TEST_ASSERT_GREATER_THAN(0, faultGetRingBufferEntryCount());
}

void test_mutex_timeout_recovery() {
    // Hold motion mutex from another task
    xSemaphoreTake(taskGetMotionMutex(), portMAX_DELAY);

    // Try motion command - should timeout gracefully
    bool success = motionMoveAbsolute(10, 10, 10, 10, 100);
    TEST_ASSERT_FALSE(success);

    xSemaphoreGive(taskGetMotionMutex());
}
```

---

## 📊 Risk Reduction Timeline

```
Week 1: HIGH Priority Items (5 issues, 20 hours)
├─ Day 1-2: Config API validation (5.2)
├─ Day 2-3: Stack monitoring (2.2)
├─ Day 3-4: I2C health check (3.2)
├─ Day 4: Watchdog test (4.3)
└─ Day 5: I2C recovery (8.2)

Week 2: MEDIUM Priority Items (3 issues, 20 hours)
├─ Day 1-3: Spinlock audit & migration (1.3)
├─ Day 4: G-code validation (5.3)
└─ Day 5: Flash wear protection (2.3)

Week 3+: LOW Priority Items (2 issues, 20 hours)
├─ Magic number refactoring (6.1)
└─ Stress testing suite (7.2)
```

**Total Effort:** 60 hours (7.5 days)
**Risk Reduction:** MEDIUM → LOW

---

## ✅ What's Already Good (Keep Doing)

1. ✅ Zero dynamic allocation in critical paths
2. ✅ SHA-256 password hashing with rate limiting
3. ✅ Integer-based motion control (no float drift)
4. ✅ Event-driven architecture
5. ✅ Comprehensive fault logging with ring buffer
6. ✅ Task-based (no ISRs) - inherently safer
7. ✅ Excellent error handling patterns
8. ✅ Good documentation (ARCHITECTURE.md, etc.)

---

## 📞 Questions?

See full audit report: `COMPREHENSIVE_AUDIT_REPORT.md`

**Contact:**
- Full findings in main report
- Code examples in this document
- Implementation guidance included
