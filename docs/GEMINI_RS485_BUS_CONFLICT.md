# RS485 Bus Conflict Analysis - CRITICAL FINDING

**Date:** 2025-12-21
**Severity:** HIGH
**Status:** CONFIRMED - Bug Identified
**Impact:** Encoder read failures, motion stutter, data corruption

---

## Executive Summary

Gemini AI audit identified a **critical hardware conflict** where the encoder and VFD share a single RS485 bus (Serial1) without proper arbitration. Investigation confirms:

1. ✅ **Encoder and Altivar31 VFD DO share Serial1** (GPIO 14/33)
2. ✅ **RS485 multiplexer EXISTS** but is **NOT used by Altivar31**
3. ✅ **JXK-10 current sensor CORRECTLY uses multiplexer** (no conflict)
4. ❌ **Altivar31 VFD bypasses multiplexer** → **BUS CONFLICT CONFIRMED**

---

## Architecture Overview

### Hardware Configuration

**Serial1 (GPIO 14/33) - Shared RS485 Bus:**
- **Primary:** WJ66 Encoder (4 axes, high-speed position feedback)
- **Secondary:** Altivar31 VFD (motor current/frequency telemetry)
- **Secondary:** JXK-10 Current Sensor (spindle current monitoring)

**Abstraction Layer:**
```
Application Layer:
  ├─ encoder_wj66.cpp       → Uses Serial1 directly
  ├─ altivar31_modbus.cpp   → Uses encoderHalSend/Receive (via HAL)
  └─ jxk10_modbus.cpp       → Uses encoderHalSend/Receive (via HAL)

HAL Layer:
  └─ encoder_hal.cpp        → Manages Serial1 (GPIO 14/33)
       └─ hal_state.serial_port = &Serial1

Multiplexer Layer:
  └─ spindle_current_rs485.cpp → Device arbitration (10ms inter-frame delay)
       ├─ RS485_DEVICE_ENCODER  (default)
       └─ RS485_DEVICE_SPINDLE
```

---

## Timing Analysis

### 1. Encoder Task (High Priority - Correct)

**File:** `src/tasks_encoder.cpp`, `src/encoder_wj66.cpp`

| Parameter | Value | Source |
|-----------|-------|--------|
| Task Period | 20ms (50Hz) | TASK_PERIOD_ENCODER |
| Read Interval | 50ms | WJ66_READ_INTERVAL_MS |
| Actual Read Frequency | **20 Hz** (every 50ms) | Rate-limited by interval |
| UART | Serial1 | Direct access |
| Protocol | ASCII ("#00\r" → "!X,Y,Z,A\r") | WJ66 proprietary |
| Transaction Time | ~5-10ms @ 9600 baud | 4 byte TX + ~20 byte RX |
| Multiplexer | ❌ Not used | Direct Serial1 access |

**Code Evidence:**
```cpp
// tasks_encoder.cpp:42
vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_ENCODER));  // 20ms

// encoder_wj66.cpp:119
bool time_to_poll = (now - wj66_state.last_command_time > WJ66_READ_INTERVAL_MS);  // 50ms

// encoder_wj66.cpp:128
Serial1.print("#00\r");  // Direct Serial1 access - NO MULTIPLEXER CHECK!
```

---

### 2. Telemetry Task - Altivar31 VFD (BUG - Bypasses Multiplexer)

**File:** `src/tasks_telemetry.cpp`, `src/altivar31_modbus.cpp`

| Parameter | Value | Source |
|-----------|-------|--------|
| Task Period | 1000ms (1Hz) | TASK_PERIOD_TELEMETRY |
| VFD Read Frequency | **1 Hz** (every 1s) | Rotates through 3 registers |
| UART | Serial1 (via encoderHal) | encoderHalSend/Receive |
| Protocol | Modbus RTU | Standard Modbus |
| Baud Rate | 19200 | altivar31_state.baud_rate |
| Transaction Time | ~10-15ms @ 19200 baud | 8 byte TX + ~10 byte RX |
| Multiplexer | ❌ **NOT USED** | **BUG - Direct encoderHalSend()** |

**Code Evidence (BUG):**
```cpp
// tasks_telemetry.cpp:54-65 (TELEMETRY TASK)
switch (vfd_telemetry_cycle % 3) {
    case 0:  altivar31ModbusReadCurrent();       // ❌ NO MULTIPLEXER CHECK!
    case 1:  altivar31ModbusReadFrequency();     // ❌ NO MULTIPLEXER CHECK!
    case 2:  altivar31ModbusReadThermalState();  // ❌ NO MULTIPLEXER CHECK!
}

// altivar31_modbus.cpp:170 (VFD MODBUS DRIVER)
bool altivar31ModbusReadCurrent(void) {
    // Build Modbus RTU request
    uint16_t tx_len = modbusReadRegistersRequest(...);

    // ❌ BUG: Sends directly via encoderHal WITHOUT checking multiplexer state!
    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {  // ← CONFLICT HERE!
        return false;
    }
    // ...
}

// altivar31_modbus.cpp:257 (VFD MODBUS DRIVER)
bool altivar31ModbusReceiveResponse(void) {
    // ❌ BUG: Receives directly via encoderHal WITHOUT checking multiplexer state!
    if (!encoderHalReceive(rx_data, &rx_len)) {  // ← CONFLICT HERE!
        return false;
    }
    // ...
}
```

**Why This is Wrong:**
- Altivar31 calls `encoderHalSend()` without checking `rs485MuxGetCurrentDevice()`
- If multiplexer is set to `RS485_DEVICE_ENCODER` (default), Altivar31 Modbus traffic collides with encoder data
- Encoder receives corrupted response → timeout → `ENCODER_TIMEOUT` error

---

### 3. Spindle Monitor - JXK-10 (CORRECT - Uses Multiplexer)

**File:** `src/spindle_current_monitor.cpp`, `src/jxk10_modbus.cpp`

| Parameter | Value | Source |
|-----------|-------|--------|
| Poll Interval | 1000ms (1Hz) | monitor_state.poll_interval_ms |
| UART | Serial1 (via encoderHal) | encoderHalSend/Receive |
| Protocol | Modbus RTU | JXK-10 current sensor |
| Baud Rate | 9600 | monitor_state.jxk10_baud_rate |
| Transaction Time | ~15-20ms @ 9600 baud | 8 byte TX + ~10 byte RX |
| Multiplexer | ✅ **CORRECT** | Switches to RS485_DEVICE_SPINDLE |

**Code Evidence (CORRECT):**
```cpp
// spindle_current_monitor.cpp:134-139 (STATE MACHINE)
case POLL_STATE_SWITCH_DEVICE: {
    // ✅ CORRECT: Check and switch multiplexer state
    if (rs485MuxGetCurrentDevice() != RS485_DEVICE_SPINDLE) {
        rs485MuxSwitchDevice(RS485_DEVICE_SPINDLE);
    }

    // ✅ CORRECT: Wait for multiplexer to complete switch (10ms delay)
    if (rs485MuxUpdate()) {
        poll_state = POLL_STATE_SEND_REQUEST;
    }
    // ...
}

// spindle_current_monitor.cpp:203-204 (CLEANUP)
// ✅ CORRECT: Switch back to encoder device after read
rs485MuxSwitchDevice(RS485_DEVICE_ENCODER);
rs485MuxUpdate();
```

**Why This is Correct:**
- JXK-10 checks multiplexer state before sending
- Waits for 10ms inter-frame delay (prevents bus collision)
- Switches back to `RS485_DEVICE_ENCODER` after read
- No conflict with encoder reads

---

## The Bus Conflict

### Conflict Scenario

**Timeline:**
1. **T = 0ms:** Encoder task wakes up, sends `"#00\r"` on Serial1
2. **T = 5ms:** Encoder expects response within 50ms timeout
3. **T = 10ms:** **Telemetry task sends Altivar31 Modbus request (8 bytes)**
4. **T = 10-15ms:** Encoder receives **CORRUPTED DATA** (mix of encoder response + Modbus traffic)
5. **T = 55ms:** Encoder timeout → `wj66_state.status = ENCODER_TIMEOUT`
6. **T = 55ms:** Encoder error count increments

**Impact:**
- Encoder position data corrupted or lost
- Motion controller uses stale encoder data
- Potential motion stutter or false deviation alarms
- Fault logging overhead (NVS writes)

**Frequency:**
- Altivar31 reads every 1 second
- Encoder reads every 50ms (20 times/second)
- **Collision probability: ~2% per second** (20ms transaction / 1000ms window)
- Over 1 hour: ~72 collisions → 72 encoder timeouts

---

## Multiplexer Implementation

### How the Multiplexer Works

**File:** `src/spindle_current_rs485.cpp`

**State Machine:**
```cpp
typedef enum {
    RS485_DEVICE_ENCODER = 0,  // Default: Encoder owns the bus
    RS485_DEVICE_SPINDLE = 1   // Spindle devices (JXK-10, Altivar31)
} rs485_device_t;

static rs485_mux_state_t mux_state = {
    .current_device = RS485_DEVICE_ENCODER,
    .inter_frame_delay_ms = 10,  // 10ms delay between device switches
    // ...
};
```

**Switching Protocol:**
```cpp
// 1. Request device switch
rs485MuxSwitchDevice(RS485_DEVICE_SPINDLE);

// 2. Wait for inter-frame delay (10ms)
while (!rs485MuxCanSwitch()) {
    vTaskDelay(1);
}
rs485MuxUpdate();  // Completes the switch

// 3. Now safe to use encoderHalSend/Receive for spindle device

// 4. After transaction, switch back
rs485MuxSwitchDevice(RS485_DEVICE_ENCODER);
rs485MuxUpdate();
```

**Why 10ms Delay:**
- Ensures previous transaction fully completes before switching
- Prevents partial frame corruption
- Allows UART buffers to drain

---

## Root Cause Analysis

### Why the Bug Exists

**Historical Context:**
1. **Phase 1:** Encoder driver written first → Direct Serial1 access
2. **Phase 5.0:** Spindle current monitoring added → RS485 multiplexer created
3. **Phase 5.5:** Altivar31 VFD monitoring added **AFTER multiplexer existed**
4. **Bug:** Altivar31 was implemented using `encoderHalSend/Receive` (correct abstraction) but **did not integrate with multiplexer** (incorrect arbitration)

**Code Pattern:**
- **JXK-10 (Phase 5.0):** Implemented WITH multiplexer awareness ✅
- **Altivar31 (Phase 5.5):** Implemented WITHOUT multiplexer awareness ❌

**Why Encoder Doesn't Use Multiplexer:**
- Encoder is the **primary device** (default `RS485_DEVICE_ENCODER`)
- Multiplexer defaults to encoder, so encoder has implicit bus ownership
- Other devices must request bus via multiplexer

---

## Comparison: Correct vs Incorrect Implementation

### ❌ INCORRECT: Altivar31 (Current Implementation)

```cpp
// altivar31_modbus.cpp:161-179
bool altivar31ModbusReadCurrent(void) {
    // Build Modbus RTU request
    uint16_t tx_len = modbusReadRegistersRequest(
        altivar31_state.slave_address,
        ALTIVAR31_REG_DRIVE_CURRENT, 1, modbus_tx_buffer);

    // ❌ BUG: Sends directly without checking multiplexer state
    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        altivar31_state.error_count++;
        return false;
    }

    modbus_request_time_ms = millis();
    return true;
}
```

**Problems:**
1. No `rs485MuxGetCurrentDevice()` check
2. No `rs485MuxSwitchDevice()` call
3. Assumes bus is available
4. Can collide with encoder reads

---

### ✅ CORRECT: JXK-10 (Reference Implementation)

```cpp
// spindle_current_monitor.cpp:132-160
case POLL_STATE_SWITCH_DEVICE: {
    // ✅ Check current device
    if (rs485MuxGetCurrentDevice() != RS485_DEVICE_SPINDLE) {
        rs485MuxSwitchDevice(RS485_DEVICE_SPINDLE);
    }

    // ✅ Wait for switch to complete (10ms inter-frame delay)
    if (rs485MuxUpdate()) {
        poll_state = POLL_STATE_SEND_REQUEST;
    }

    // ✅ Timeout protection
    if ((now - poll_state_time_ms) > 100) {
        monitor_state.error_count++;
        poll_state = POLL_STATE_IDLE;
    }
    return false;
}

case POLL_STATE_SEND_REQUEST: {
    // ✅ Now safe to send - multiplexer switched to SPINDLE
    if (jxk10ModbusReadCurrent()) {
        poll_state = POLL_STATE_WAIT_RESPONSE;
    }
    // ...
}

case POLL_STATE_PARSE_RESPONSE: {
    // Parse response...

    // ✅ Switch back to encoder device
    rs485MuxSwitchDevice(RS485_DEVICE_ENCODER);
    rs485MuxUpdate();
    poll_state = POLL_STATE_IDLE;
    return true;
}
```

**Why This is Correct:**
1. ✅ Checks multiplexer state before sending
2. ✅ Requests device switch if needed
3. ✅ Waits for 10ms inter-frame delay
4. ✅ Switches back to encoder after transaction
5. ✅ Timeout protection (100ms)
6. ✅ Non-blocking state machine

---

## Recommended Fix

### Option 1: Modify Altivar31 Driver (Minimal Change)

**Modify `tasks_telemetry.cpp` to use multiplexer:**

```cpp
// CURRENT (BUGGY):
switch (vfd_telemetry_cycle % 3) {
    case 0:  altivar31ModbusReadCurrent(); break;
    case 1:  altivar31ModbusReadFrequency(); break;
    case 2:  altivar31ModbusReadThermalState(); break;
}
if (altivar31ModbusReceiveResponse()) {
    // Process response...
}

// FIXED (WITH MULTIPLEXER):
static enum {
    VFD_IDLE,
    VFD_SWITCH_DEVICE,
    VFD_SEND_REQUEST,
    VFD_WAIT_RESPONSE
} vfd_poll_state = VFD_IDLE;

static uint32_t vfd_state_time_ms = 0;

switch (vfd_poll_state) {
    case VFD_IDLE: {
        // Time to poll?
        if ((millis() - vfd_last_poll_ms) < 1000) break;
        vfd_poll_state = VFD_SWITCH_DEVICE;
        vfd_state_time_ms = millis();
        break;
    }

    case VFD_SWITCH_DEVICE: {
        // ✅ Switch to spindle device
        if (rs485MuxGetCurrentDevice() != RS485_DEVICE_SPINDLE) {
            rs485MuxSwitchDevice(RS485_DEVICE_SPINDLE);
        }

        // ✅ Wait for switch to complete
        if (rs485MuxUpdate()) {
            vfd_poll_state = VFD_SEND_REQUEST;
        }

        // Timeout protection
        if ((millis() - vfd_state_time_ms) > 100) {
            vfd_poll_state = VFD_IDLE;
        }
        break;
    }

    case VFD_SEND_REQUEST: {
        // ✅ Now safe to send Modbus request
        bool sent = false;
        switch (vfd_telemetry_cycle % 3) {
            case 0:  sent = altivar31ModbusReadCurrent(); break;
            case 1:  sent = altivar31ModbusReadFrequency(); break;
            case 2:  sent = altivar31ModbusReadThermalState(); break;
        }

        if (sent) {
            vfd_poll_state = VFD_WAIT_RESPONSE;
            vfd_state_time_ms = millis();
            vfd_telemetry_cycle++;
        } else {
            vfd_poll_state = VFD_IDLE;
        }
        break;
    }

    case VFD_WAIT_RESPONSE: {
        // Wait minimum 50ms for response
        if ((millis() - vfd_state_time_ms) < 50) break;

        // ✅ Attempt to parse response
        if (altivar31ModbusReceiveResponse()) {
            // Process response...
        }

        // ✅ Switch back to encoder device
        rs485MuxSwitchDevice(RS485_DEVICE_ENCODER);
        rs485MuxUpdate();

        vfd_last_poll_ms = millis();
        vfd_poll_state = VFD_IDLE;
        break;
    }
}
```

**Pros:**
- Minimal changes to altivar31_modbus.cpp (no changes needed)
- Follows JXK-10 pattern (proven correct)
- Non-blocking state machine
- Proper multiplexer arbitration

**Cons:**
- Adds complexity to telemetry task
- State machine overhead

---

### Option 2: Separate UART for VFD (Hardware Change)

**Use Serial2 (GPIO 16/17) for Altivar31:**

| Device | UART | GPIO Pins | Protocol |
|--------|------|-----------|----------|
| Encoder (WJ66) | Serial1 | 14/33 | ASCII |
| VFD (Altivar31) | Serial2 | 16/17 | Modbus RTU |
| Current Sensor (JXK-10) | Serial1 | 14/33 | Modbus RTU |

**Changes Required:**
```cpp
// altivar31_modbus.cpp
HardwareSerial* vfd_serial = &Serial2;  // Instead of encoderHal

bool altivar31ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    vfd_serial->begin(baud_rate, SERIAL_8N1, 16, 17);  // GPIO 16/17
    // ...
}

bool altivar31ModbusReadCurrent(void) {
    // Build request...
    vfd_serial->write(modbus_tx_buffer, tx_len);  // Direct Serial2 access
    // ...
}
```

**Pros:**
- ✅ No bus conflict (dedicated UART)
- ✅ No multiplexer complexity
- ✅ Simpler code
- ✅ Better isolation

**Cons:**
- ❌ Requires hardware wiring changes
- ❌ May not be feasible if pins already used
- ❌ Breaks existing wiring

---

## Recommendation

**Implement Option 1 (Software Fix):**
1. Low risk (no hardware changes)
2. Follows proven JXK-10 pattern
3. Preserves existing wiring
4. Can be implemented immediately

**Priority:** HIGH
**Effort:** Medium (1-2 hours)
**Risk:** Low (tested pattern)

---

## Testing Plan

### 1. Verify Bug Exists (Current Behavior)

**Test Procedure:**
1. Enable encoder and Altivar31 VFD monitoring
2. Monitor encoder error rate over 10 minutes
3. Check for `ENCODER_TIMEOUT` errors in logs
4. Correlate encoder errors with VFD read timestamps

**Expected Result (Before Fix):**
- ~72 encoder timeouts per hour (~1.2 per minute)
- Errors occur at 1-second intervals (aligned with VFD reads)

---

### 2. Verify Fix Works (After Implementation)

**Test Procedure:**
1. Implement Option 1 (multiplexer state machine)
2. Run same test (10 minutes, encoder + VFD active)
3. Monitor encoder error rate
4. Verify VFD data still updates correctly

**Expected Result (After Fix):**
- Zero encoder timeouts caused by VFD reads
- VFD telemetry still updates every 1 second
- Multiplexer switches cleanly (10ms delays)

---

### 3. Stress Test

**Test Procedure:**
1. Increase VFD poll rate to 100ms (10Hz)
2. Run for 1 hour with continuous motion
3. Monitor encoder error rate, motion quality

**Expected Result:**
- Zero bus conflicts (multiplexer arbitrates correctly)
- Motion quality unchanged
- VFD data updates 10x faster

---

## Related Files

### Source Files
- `src/altivar31_modbus.cpp` - VFD Modbus driver (needs multiplexer integration)
- `src/tasks_telemetry.cpp` - Telemetry task (modify to add state machine)
- `src/encoder_wj66.cpp` - Encoder driver (no changes needed)
- `src/spindle_current_monitor.cpp` - Reference implementation (correct pattern)
- `src/spindle_current_rs485.cpp` - Multiplexer implementation
- `src/encoder_hal.cpp` - HAL abstraction layer

### Header Files
- `include/altivar31_modbus.h` - VFD driver API
- `include/spindle_current_rs485.h` - Multiplexer API
- `include/encoder_hal.h` - HAL API
- `include/task_manager.h` - Task periods

### Documentation
- `docs/GEMINI_FINAL_AUDIT.md` - Previous audit findings
- `docs/GEMINI_IMPROVEMENT_ROADMAP.md` - Improvement roadmap
- `ROADMAP.md` - Central tracking document

---

## Conclusion

**Confirmed:** RS485 bus conflict exists between Altivar31 VFD and encoder.

**Root Cause:** Altivar31 driver bypasses existing multiplexer, causing bus collisions.

**Severity:** HIGH - Encoder timeouts → motion quality degradation.

**Fix:** Implement multiplexer state machine in telemetry task (Option 1).

**Effort:** Medium (1-2 hours implementation + testing).

**Risk:** Low (proven pattern from JXK-10 implementation).

---

**Next Steps:**
1. ✅ Document findings (this file)
2. ⏳ Implement Option 1 fix
3. ⏳ Test and validate
4. ⏳ Update ROADMAP.md
5. ⏳ Investigate remaining Gemini findings (logging in critical sections, I2C priority)
