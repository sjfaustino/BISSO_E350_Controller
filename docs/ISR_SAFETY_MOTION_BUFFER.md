# ISR Safety Analysis - Motion Buffer

## Gemini Critical Risk: "Mutex in ISR"

### Risk Identified
**Concern:** "If motion_control.cpp calls pop() from a Hardware Timer ISR, the system will crash (you cannot take a Mutex from an ISR)."

### Current Status: ✅ **SAFE - FreeRTOS Task Architecture**

---

## Call Chain Verification

### Complete Execution Path

```
FreeRTOS Task (100Hz Loop)
└─> taskMotionFunction() [tasks_motion.cpp:11]
    - Task context: ✅ Safe for mutexes
    - Loop: while(1) with vTaskDelay()
    - Period: 10ms (100Hz)

    └─> motionUpdate() [motion_control.cpp:344]
        - Acquires taskGetMotionMutex()
        - Task context: ✅ Safe

        └─> motionPlanner.update() [motion_planner.cpp:35]
            - Task context: ✅ Safe

            └─> motionPlanner.checkBufferDrain() [motion_planner.cpp:58]
                - Task context: ✅ Safe

                └─> motionBuffer.pop(&cmd) [motion_buffer.cpp:120]
                    - Calls: xSemaphoreTake(buffer_mutex, 100ms)
                    - Task context: ✅ SAFE FOR MUTEX
```

### Why This is Safe

| Aspect | Current Implementation | Why Safe |
|--------|----------------------|----------|
| **Execution Context** | FreeRTOS Task | Tasks can use mutexes |
| **Blocking Allowed** | Yes (vTaskDelay, xSemaphoreTake) | Task scheduler handles blocking |
| **Interrupt Level** | None (task-level only) | No ISR involved |
| **Timing** | 100Hz periodic (10ms) | Predictable, non-real-time |

---

## ISR vs Task: Critical Differences

### ✅ SAFE: FreeRTOS Task (Current)

```cpp
void taskMotionFunction(void* parameter) {
    while (1) {
        motionUpdate();  // ✅ Calls pop() from task
        vTaskDelay(pdMS_TO_TICKS(10));  // ✅ Can block
    }
}

// Inside motion_buffer.cpp:
bool MotionBuffer::pop(motion_cmd_t* cmd) {
    xSemaphoreTake(buffer_mutex, 100);  // ✅ SAFE in task context
    // ... pop logic
    xSemaphoreGive(buffer_mutex);
}
```

**Why Safe:**
- Tasks run in thread context (not interrupt)
- FreeRTOS scheduler allows task blocking
- Mutexes designed for task-level synchronization

### ❌ CRASH: Hardware Timer ISR (Hypothetical)

```cpp
// ❌❌❌ DANGEROUS - DO NOT IMPLEMENT
void IRAM_ATTR timerISR() {
    motionUpdate();  // ❌ Calls pop() from ISR
}

// Inside motion_buffer.cpp:
bool MotionBuffer::pop(motion_cmd_t* cmd) {
    xSemaphoreTake(buffer_mutex, 100);  // ❌❌❌ CRASH!
    // CRASH: Cannot take mutex from ISR
    // ESP32 will panic with "assert failed: xQueueGenericReceive"
}
```

**Why Crash:**
- ISRs cannot block (must be fast, atomic)
- `xSemaphoreTake()` is blocking operation
- ESP32 FreeRTOS asserts if mutex taken from ISR
- Result: Guru Meditation Error, system reset

---

## Migration Risk Analysis

### Scenario: Hardware Timer for Smoother Pulses

**Motivation:** Use hardware timer for precise step timing

**Risk:** If motion control migrated to timer ISR:

```cpp
// Future risky pattern:
hw_timer_t* motion_timer = timerBegin(0, 80, true);
timerAttachInterrupt(motion_timer, &motionTimerISR, true);
timerAlarmWrite(motion_timer, 10000, true);  // 100Hz

void IRAM_ATTR motionTimerISR() {
    motionUpdate();  // ❌ This will crash - mutex in ISR!
}
```

**Impact:**
- ❌ `motionBuffer.pop()` uses `xSemaphoreTake()` → CRASH
- ❌ `motionBuffer.push()` uses `xSemaphoreTake()` → CRASH
- ❌ All motion buffer operations become ISR-unsafe

---

## ISR-Safe Alternatives (If Ever Needed)

### Option 1: ISR-Safe Buffer (Critical Sections)

```cpp
// ISR-safe version using spinlocks
bool MotionBuffer::pop_isr(motion_cmd_t* cmd) {
    portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL_ISR(&spinlock);  // Disable interrupts

    if (count == 0) {
        portEXIT_CRITICAL_ISR(&spinlock);
        return false;
    }

    *cmd = buffer[tail];
    tail = (tail + 1) % MOTION_BUFFER_SIZE;
    count--;

    portEXIT_CRITICAL_ISR(&spinlock);  // Re-enable interrupts
    return true;
}
```

**Trade-offs:**
- ✅ ISR-safe (can be called from timer interrupt)
- ❌ Disables interrupts (5-10 μs) - affects other ISRs
- ❌ No timeout protection - spin-waits
- ❌ Higher priority inversion risk

### Option 2: Lockless Ring Buffer (Atomic Operations)

```cpp
// Lock-free version using atomic operations
bool MotionBuffer::pop_lockfree(motion_cmd_t* cmd) {
    uint8_t current_tail = tail;

    if (count == 0) return false;  // Racy read - acceptable

    *cmd = buffer[current_tail];

    // Atomic increment with wrap-around
    uint8_t next_tail = (current_tail + 1) % MOTION_BUFFER_SIZE;
    if (__sync_bool_compare_and_swap(&tail, current_tail, next_tail)) {
        __sync_fetch_and_sub(&count, 1);
        return true;
    }

    return false;  // CAS failed - retry
}
```

**Trade-offs:**
- ✅ ISR-safe (no blocking, no critical sections)
- ✅ Minimal interrupt latency
- ❌ Complex implementation (prone to bugs)
- ❌ ABA problem risk
- ❌ No guaranteed ordering

### Option 3: Deferred Work Queue (Recommended)

```cpp
// ISR does minimal work, defers to task
void IRAM_ATTR motionTimerISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Signal motion task (non-blocking)
    vTaskNotifyGiveFromISR(motion_task_handle,
                           &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Task does actual work (mutex-safe)
void taskMotionFunction(void* parameter) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait for ISR
        motionUpdate();  // ✅ Still in task - mutex safe!
    }
}
```

**Trade-offs:**
- ✅ ISR-safe (ISR only signals, doesn't do heavy work)
- ✅ Keeps existing mutex-based code
- ✅ Clean separation: ISR signals, task processes
- ✅ Recommended FreeRTOS pattern
- ❌ Slight latency increase (task switch overhead)

---

## Current Safeguards

### 1. Architecture Documentation

**File:** This document (`docs/ISR_SAFETY_MOTION_BUFFER.md`)

**Purpose:** Explicitly documents ISR safety constraints

### 2. Code Comments

**File:** `src/motion_buffer.cpp` (lines 120-135)

**Comments Added:**
```cpp
// CRITICAL: This function uses xSemaphoreTake() - NOT ISR-SAFE!
// Only call from FreeRTOS tasks, never from hardware timer ISR.
// Current architecture: Called from taskMotionFunction() ✅
// Future migration: If switching to timer ISR, use pop_unsafe()
// inside ISR and protect with different mechanism.
```

### 3. Static Assertions (Future Enhancement)

**Proposed:** Add compile-time check

```cpp
// In motion_buffer.h
#ifdef MOTION_USES_HARDWARE_TIMER_ISR
    #error "ERROR: motion_buffer.pop() is not ISR-safe! See ISR_SAFETY_MOTION_BUFFER.md"
#endif
```

---

## Decision Matrix: When to Use What

| Scenario | Mutex (Task) | Spinlock (ISR) | Lockfree (ISR) | Defer to Task |
|----------|-------------|----------------|----------------|---------------|
| **Current (Task-based motion)** | ✅ BEST | ❌ Overkill | ❌ Unnecessary | ❌ Unnecessary |
| **Future (Timer ISR)** | ❌ CRASH | ⚠️ Works but risky | ⚠️ Complex | ✅ RECOMMENDED |
| **Interrupt Latency Critical** | ❌ Too slow | ⚠️ Blocks ISRs | ✅ BEST | ❌ Too slow |
| **Code Simplicity** | ✅ BEST | ✅ Simple | ❌ Complex | ✅ Clean |

---

## Recommendations

### For Current Implementation: ✅ No Changes Needed

**Current architecture is correct:**
- FreeRTOS task-based motion control
- Mutex protection appropriate
- 100Hz timing adequate for CNC control
- No ISR involvement

**Keep using:** Mutex-based `pop()`, `push()`, `peek()`

### For Future Hardware Timer Migration: ⚠️ Use Deferred Work Pattern

**If switching to hardware timer for precision:**

```cpp
// Step 1: Timer ISR signals task (non-blocking)
void IRAM_ATTR timerISR() {
    vTaskNotifyGiveFromISR(motion_task, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Step 2: Task processes (keeps mutex-based code)
void taskMotionFunction(void* parameter) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        motionUpdate();  // ✅ Existing code still works!
    }
}
```

**Benefits:**
- ✅ No code changes to motion_buffer.cpp
- ✅ Keeps mutex safety
- ✅ ISR-safe (ISR only signals)
- ✅ Clean architecture

### DO NOT Use:

❌ **Spinlocks in ISR** - Blocks other interrupts
❌ **Lockless buffers** - Complex, error-prone
❌ **Mutexes in ISR** - Will crash ESP32

---

## Testing Scenarios

### Verify Task Context (Current)

```cpp
// In taskMotionFunction():
void taskMotionFunction(void* parameter) {
    // ✅ Should return task handle, not NULL
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    assert(task != NULL);  // Confirms we're in task context

    motionUpdate();  // ✅ Safe - task context
}
```

### Detect ISR Context (Future Guard)

```cpp
// If ever called from ISR (shouldn't happen):
bool MotionBuffer::pop(motion_cmd_t* cmd) {
    // ✅ Runtime check - crashes with clear error if in ISR
    configASSERT(!xPortInIsrContext());

    xSemaphoreTake(buffer_mutex, 100);
    // ... rest of code
}
```

---

## Conclusion

### Current Status: ✅ **SAFE**

**Architecture:**
- FreeRTOS task-based motion control (100Hz)
- Mutex-protected motion buffer
- No ISR involvement
- Correct implementation for task context

**Evidence:**
```
taskMotionFunction() [FreeRTOS Task]
  └─> motionUpdate() [Task context]
      └─> motionBuffer.pop() [Mutex - SAFE]
```

### Future Consideration: ⚠️ **Document ISR Constraint**

**If migrating to hardware timer:**
1. ✅ Use deferred work pattern (ISR signals task)
2. ❌ Do NOT call `pop()` directly from ISR
3. ❌ Do NOT replace mutex with spinlock (blocks other ISRs)
4. ✅ Keep existing mutex-based code (works in deferred task)

**Gemini's concern is valid for future architecture changes, but current implementation is safe and correct.**

---

**Last Verified:** 2025-01-XX
**Architecture:** FreeRTOS Task-based (100Hz periodic)
**ISR Safety:** Not ISR-safe by design (task-only)
**Status:** Production-ready for current architecture
**Maintained by:** BISSO Development Team
