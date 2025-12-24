# Event-Driven Architecture with FreeRTOS Event Groups

## Overview

The BISSO E350 Controller uses FreeRTOS event groups to implement an event-driven architecture, replacing polling with efficient event notifications. This reduces CPU usage, improves response time, and simplifies task synchronization.

## Architecture

### Event Groups

The system provides three event groups:

1. **Safety Events** - Emergency stop, alarms, button presses
2. **Motion Events** - State changes, command completion, homing
3. **System Events** - Configuration changes, communication errors

### Benefits Over Polling

**Before (Polling):**
```cpp
// Task wakes up every 100ms to check if something happened
while (1) {
    if (emergencyStopIsActive()) {
        handleEmergencyStop();
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Wasted CPU cycles
}
```

**After (Event-Driven):**
```cpp
// Task sleeps until event occurs
while (1) {
    EventBits_t events = systemEventsSafetyWait(
        EVENT_SAFETY_ESTOP_PRESSED | EVENT_SAFETY_ALARM_RAISED,
        true,  // Clear on exit
        false, // Wait for any bit
        portMAX_DELAY // Wait forever
    );

    if (events & EVENT_SAFETY_ESTOP_PRESSED) {
        handleEmergencyStop(); // Immediate response, no polling delay
    }
}
```

## Event Definitions

### Safety Events

| Event | Description | Set By | Used By |
|-------|-------------|--------|---------|
| `EVENT_SAFETY_ESTOP_PRESSED` | E-STOP button pressed | fault_logging.cpp | Safety task, UI |
| `EVENT_SAFETY_ESTOP_RELEASED` | E-STOP button released | fault_logging.cpp | Safety task |
| `EVENT_SAFETY_PAUSE_PRESSED` | Pause button pressed | tasks_safety.cpp | Motion task |
| `EVENT_SAFETY_RESUME_PRESSED` | Resume button pressed | tasks_safety.cpp | Motion task |
| `EVENT_SAFETY_ALARM_RAISED` | Safety alarm triggered | fault_logging.cpp | All tasks |
| `EVENT_SAFETY_ALARM_CLEARED` | Safety alarm cleared | fault_logging.cpp | All tasks |
| `EVENT_SAFETY_SOFT_LIMIT_HIT` | Soft limit violation | motion_control.cpp | Safety task |
| `EVENT_SAFETY_ENCODER_DEVIATION` | Encoder deviation | encoder_deviation.cpp | Motion task |

### Motion Events

| Event | Description | Set By | Used By |
|-------|-------------|--------|---------|
| `EVENT_MOTION_IDLE` | Motion entered IDLE state | motion_state_machine.cpp | Telemetry, UI |
| `EVENT_MOTION_STARTED` | Motion started | motion_state_machine.cpp | Telemetry, UI |
| `EVENT_MOTION_COMPLETED` | Motion completed | motion_state_machine.cpp | G-code parser |
| `EVENT_MOTION_STOPPED` | Motion stopped | motion_state_machine.cpp | Safety task |
| `EVENT_MOTION_ERROR` | Motion error | motion_state_machine.cpp | Safety task, UI |
| `EVENT_MOTION_HOMING_START` | Homing started | motion_state_machine.cpp | UI, Telemetry |
| `EVENT_MOTION_HOMING_COMPLETE` | Homing completed | motion_state_machine.cpp | G-code parser |
| `EVENT_MOTION_BUFFER_READY` | Buffer has space | motion_buffer.cpp | G-code parser |
| `EVENT_MOTION_STATE_CHANGE` | Any state change | motion_state_machine.cpp | Debug, logging |

### System Events

| Event | Description | Set By | Used By |
|-------|-------------|--------|---------|
| `EVENT_SYSTEM_CONFIG_CHANGED` | Configuration updated | config_unified.cpp | All tasks |
| `EVENT_SYSTEM_I2C_ERROR` | I2C error | i2c_bus_recovery.cpp | Monitor task |
| `EVENT_SYSTEM_MODBUS_ERROR` | Modbus error | modbus drivers | Monitor task |
| `EVENT_SYSTEM_NETWORK_CONNECTED` | Network up | system_telemetry.cpp | Telemetry |
| `EVENT_SYSTEM_NETWORK_LOST` | Network down | system_telemetry.cpp | Telemetry |
| `EVENT_SYSTEM_LOW_MEMORY` | Low memory | memory_monitor.cpp | All tasks |
| `EVENT_SYSTEM_WATCHDOG_ALERT` | Watchdog warning | watchdog_manager.cpp | Monitor task |
| `EVENT_SYSTEM_OTA_REQUESTED` | OTA update | web_server.cpp | System task |

## Usage Examples

### Example 1: Wait for Emergency Stop

```cpp
void safetyTaskFunction(void* parameter) {
    while (1) {
        // Wait for any safety event
        EventBits_t events = systemEventsSafetyWait(
            EVENT_SAFETY_ALL_BITS, // Wait for any safety event
            true,                  // Clear events after reading
            false,                 // Wait for ANY bit (not all)
            pdMS_TO_TICKS(1000)   // 1 second timeout
        );

        // Check which event(s) occurred
        if (events & EVENT_SAFETY_ESTOP_PRESSED) {
            logError("[SAFETY] E-STOP activated!");
            handleEmergencyStop();
        }

        if (events & EVENT_SAFETY_ALARM_RAISED) {
            logWarning("[SAFETY] Alarm raised!");
            handleAlarm();
        }

        // Timeout occurred - do periodic checks
        if (events == 0) {
            doPeriodicSafetyCheck();
        }
    }
}
```

### Example 2: Wait for Motion Completion

```cpp
bool waitForMotionComplete(uint32_t timeout_ms) {
    // Wait for motion to complete or error
    EventBits_t events = systemEventsMotionWait(
        EVENT_MOTION_COMPLETED | EVENT_MOTION_ERROR,
        true,                    // Clear on exit
        false,                   // Wait for any bit
        pdMS_TO_TICKS(timeout_ms)
    );

    if (events & EVENT_MOTION_COMPLETED) {
        return true; // Success
    } else if (events & EVENT_MOTION_ERROR) {
        logError("[MOTION] Motion error during wait");
        return false;
    } else {
        logWarning("[MOTION] Timeout waiting for motion");
        return false;
    }
}
```

### Example 3: Signal Event from ISR

```cpp
void IRAM_ATTR emergencyStopISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Signal event from ISR
    xEventGroupSetBitsFromISR(
        systemEventsGetSafety(),
        EVENT_SAFETY_ESTOP_PRESSED,
        &xHigherPriorityTaskWoken
    );

    // Yield if higher priority task was woken
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### Example 4: Hybrid Periodic + Event-Driven

```cpp
void motionTaskFunction(void* parameter) {
    const TickType_t periodicInterval = pdMS_TO_TICKS(10); // 100Hz
    TickType_t lastWake = xTaskGetTickCount();

    while (1) {
        // Do periodic motion update
        motionUpdate();

        // Calculate time until next periodic wake
        TickType_t now = xTaskGetTickCount();
        TickType_t nextWake = lastWake + periodicInterval;
        TickType_t remaining = (nextWake > now) ? (nextWake - now) : 0;

        // Wait for events OR periodic timeout (whichever comes first)
        EventBits_t events = systemEventsMotionWait(
            EVENT_MOTION_STATE_CHANGE,
            true,     // Clear on exit
            false,    // Any bit
            remaining // Time until next periodic wake
        );

        if (events & EVENT_MOTION_STATE_CHANGE) {
            // Event occurred - handle immediately
            handleStateChange();
        }

        // Update last wake time for next iteration
        lastWake = nextWake;
    }
}
```

### Example 5: Check Event Status (Non-Blocking)

```cpp
void telemetryUpdate() {
    // Non-blocking check of current event status
    EventBits_t safetyStatus = systemEventsGetSafetyStatus();

    if (safetyStatus & EVENT_SAFETY_ESTOP_PRESSED) {
        webServer.setEmergencyStopActive(true);
    }

    if (safetyStatus & EVENT_SAFETY_ALARM_RAISED) {
        webServer.setAlarmStatus(true);
    }
}
```

## API Reference

### Initialization

```cpp
bool systemEventsInit(void);
```
Initialize all event groups. Called automatically by task manager.

### Event Group Accessors

```cpp
EventGroupHandle_t systemEventsGetSafety(void);
EventGroupHandle_t systemEventsGetMotion(void);
EventGroupHandle_t systemEventsGetSystem(void);
```
Get handle to event group for direct FreeRTOS API calls.

### Signaling Events

```cpp
void systemEventsSafetySet(EventBits_t event_bits);
void systemEventsMotionSet(EventBits_t event_bits);
void systemEventsSystemSet(EventBits_t event_bits);
```
Set (signal) one or more event bits. Can be called from tasks or ISRs (use FromISR variant for ISRs).

### Clearing Events

```cpp
void systemEventsSafetyClear(EventBits_t event_bits);
void systemEventsMotionClear(EventBits_t event_bits);
void systemEventsSystemClear(EventBits_t event_bits);
```
Clear one or more event bits.

### Waiting for Events

```cpp
EventBits_t systemEventsSafetyWait(EventBits_t event_bits,
                                    bool clear_on_exit,
                                    bool wait_all,
                                    TickType_t ticks_to_wait);
```

**Parameters:**
- `event_bits` - Bit mask of events to wait for
- `clear_on_exit` - If true, clear bits when waking up
- `wait_all` - If true, wait for ALL bits; if false, wait for ANY bit
- `ticks_to_wait` - Maximum time to wait (`portMAX_DELAY` for infinite)

**Returns:** Event bits that were set when function returned

### Checking Status

```cpp
EventBits_t systemEventsGetSafetyStatus(void);
EventBits_t systemEventsGetMotionStatus(void);
EventBits_t systemEventsGetSystemStatus(void);
```
Non-blocking check of current event status.

## Best Practices

### 1. Always Clear Events After Handling

```cpp
// GOOD - Clear after handling
EventBits_t events = systemEventsMotionWait(..., true, ...);
if (events & EVENT_MOTION_COMPLETED) {
    handleCompletion();
}

// BAD - Events accumulate, causing spurious wakeups
EventBits_t events = systemEventsMotionWait(..., false, ...);
```

### 2. Use Timeouts for Robustness

```cpp
// GOOD - Timeout prevents deadlock
EventBits_t events = systemEventsMotionWait(
    EVENT_MOTION_COMPLETED,
    true,
    false,
    pdMS_TO_TICKS(5000) // 5 second timeout
);
if (events == 0) {
    logWarning("[MOTION] Timeout waiting for completion");
}

// BAD - Infinite wait can deadlock
EventBits_t events = systemEventsMotionWait(..., portMAX_DELAY);
```

### 3. Combine with Periodic Updates When Needed

Some tasks need both event-driven responses AND periodic updates (e.g., motion control needs 100Hz update rate). Use the hybrid approach shown in Example 4.

### 4. Signal Events Close to Source

Signal events as close as possible to where the condition occurs:

```cpp
// GOOD - Signal immediately when state changes
void MotionStateMachine::transitionTo(Axis* axis, motion_state_t new_state) {
    // ... state transition logic ...
    systemEventsMotionSet(EVENT_MOTION_STATE_CHANGE);
}

// BAD - Polling to detect change and then signaling
void motionUpdate() {
    if (oldState != newState) {
        systemEventsMotionSet(EVENT_MOTION_STATE_CHANGE); // Delayed!
    }
}
```

### 5. Use Descriptive Event Names

Event names should clearly indicate what happened, not what action to take:

```cpp
// GOOD
EVENT_SAFETY_ESTOP_PRESSED

// BAD
EVENT_STOP_MOTION // Ambiguous - is this a command or notification?
```

## Performance Considerations

### CPU Usage Reduction

**Before (Polling):**
- 10 tasks polling at 100Hz = 1000 unnecessary wake-ups/second
- Each wake-up: context switch + check condition + sleep
- Estimated CPU overhead: 5-10%

**After (Event-Driven):**
- Tasks sleep until event occurs
- Only wake when work is needed
- Estimated CPU overhead: <1%

### Response Time Improvement

**Polling:** 0-100ms latency (depends on poll interval)
**Event-Driven:** <1ms latency (immediate wake-up)

### Memory Usage

Each event group uses 24 bytes of RAM.
Total overhead: 72 bytes for 3 event groups.

## Migration Guide

### Converting Polling Loop to Event-Driven

**Before:**
```cpp
void taskFunction(void* parameter) {
    while (1) {
        if (condition) {
            handleEvent();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

**After:**
```cpp
void taskFunction(void* parameter) {
    while (1) {
        EventBits_t events = systemEventsWait(
            RELEVANT_EVENTS,
            true,
            false,
            portMAX_DELAY
        );

        if (events & SPECIFIC_EVENT) {
            handleEvent();
        }
    }
}
```

### Adding Event Signaling to Existing Code

1. Identify where the condition changes
2. Add event signal at that location
3. Update tasks to wait for event instead of polling

**Example:**
```cpp
// In motion_state_machine.cpp
void MotionStateMachine::transitionTo(...) {
    // ... existing logic ...

    // Add event signaling
    systemEventsMotionSet(EVENT_MOTION_STATE_CHANGE);
}
```

## Troubleshooting

### Events Not Triggering

**Symptom:** Task doesn't wake up when expected

**Causes:**
1. Event not being signaled - Add debug logging where event is set
2. Wrong event mask - Double-check event bits in wait call
3. Event cleared before wait - Ensure proper ordering

### Spurious Wake-ups

**Symptom:** Task wakes up when no event occurred

**Causes:**
1. Events not cleared after handling
2. Multiple tasks setting same event
3. Timeout occurred (check return value)

### Deadlocks

**Symptom:** Task never wakes up

**Causes:**
1. Infinite timeout with no event signaling
2. Event set before wait (use persistent events or redesign)
3. Wrong event group handle

**Solution:** Always use finite timeouts for debugging:
```cpp
EventBits_t events = systemEventsWait(..., pdMS_TO_TICKS(10000));
if (events == 0) {
    logError("[TASK] Timeout waiting for event - possible deadlock");
}
```

## References

- [FreeRTOS Event Groups Documentation](https://www.freertos.org/event-groups-API.html)
- `include/system_events.h` - API reference
- `src/system_events.cpp` - Implementation
- `src/motion_state_machine.cpp` - Motion events example
- `src/fault_logging.cpp` - Safety events example
