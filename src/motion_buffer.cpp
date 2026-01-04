/**
 * @file motion_buffer.cpp
 * @brief Implementation of Ring Buffer (PosiPro)
 * @details THREAD-SAFE implementation with mutex protection.
 *          All public methods are protected by buffer_mutex.
 */

#include "motion_buffer.h"
#include "encoder_calibration.h" // For machine calibration data
#include "serial_logger.h"
#include "system_constants.h" // For MOTION_POSITION_SCALE_FACTOR
#include "system_events.h"    // PHASE 5.10: Event-driven architecture
#include "task_manager.h"     // For taskGetMotionMutex()
#include <string.h>

MotionBuffer motionBuffer;

MotionBuffer::MotionBuffer() {
  head = 0;
  tail = 0;
  count = 0;
  buffer_mutex = NULL; // Will be set by setMutex()
}

void MotionBuffer::init() {
  // CRITICAL FIX: Initialize mutex early to prevent race condition during
  // emergency stop at boot
  if (buffer_mutex == NULL) {
    // Get the separate buffer mutex (avoids deadlock with motion task)
    buffer_mutex = taskGetBufferMutex();
    if (buffer_mutex == NULL) {
      logError("[BUFFER] ERROR: Cannot get motion mutex - task manager not "
               "initialized!");
    } else {
      logInfo("[BUFFER] Mutex initialized from task manager");
    }
  }

  head = 0;
  tail = 0;
  count = 0;
  logInfo("[BUFFER] Initialized (Size: %d)", MOTION_BUFFER_SIZE);
}

void MotionBuffer::setMutex(SemaphoreHandle_t mtx) { buffer_mutex = mtx; }

// ============================================================================
// UNSAFE INTERNAL IMPLEMENTATIONS (called within critical section)
// ============================================================================

bool MotionBuffer::push_unsafe(float x, float y, float z, float a,
                               float speed) {
  if (count >= MOTION_BUFFER_SIZE)
    return false;

  // CRITICAL FIX: Convert from MM to encoder counts to prevent float drift
  // All motion planning uses integer math; only convert to MM for display
  float x_scale = (machineCal.X.pulses_per_mm > 0)
                      ? machineCal.X.pulses_per_mm
                      : MOTION_POSITION_SCALE_FACTOR;
  float y_scale = (machineCal.Y.pulses_per_mm > 0)
                      ? machineCal.Y.pulses_per_mm
                      : MOTION_POSITION_SCALE_FACTOR;
  float z_scale = (machineCal.Z.pulses_per_mm > 0)
                      ? machineCal.Z.pulses_per_mm
                      : MOTION_POSITION_SCALE_FACTOR;
  float a_scale = (machineCal.A.pulses_per_degree > 0)
                      ? machineCal.A.pulses_per_degree
                      : MOTION_POSITION_SCALE_FACTOR_DEG;

  buffer[head].x_counts = (int32_t)(x * x_scale);
  buffer[head].y_counts = (int32_t)(y * y_scale);
  buffer[head].z_counts = (int32_t)(z * z_scale);
  buffer[head].a_counts = (int32_t)(a * a_scale);
  buffer[head].speed_mm_s = speed;

  head = (head + 1) % MOTION_BUFFER_SIZE;
  count++;
  return true;
}

bool MotionBuffer::pop_unsafe(motion_cmd_t *cmd) {
  if (count == 0)
    return false;

  if (cmd)
    *cmd = buffer[tail];

  tail = (tail + 1) % MOTION_BUFFER_SIZE;
  count--;
  return true;
}

bool MotionBuffer::peek_unsafe(motion_cmd_t *cmd) {
  if (count == 0)
    return false;
  if (cmd)
    *cmd = buffer[tail];
  return true;
}

bool MotionBuffer::isFull_unsafe() { return count >= MOTION_BUFFER_SIZE; }

bool MotionBuffer::isEmpty_unsafe() { return count == 0; }

// PHASE 5.7: Clarification - Naming Convention
// NOTE: available() returns COUNT USED (number of items IN buffer)
// This is OPPOSITE of Arduino convention where available() means "data ready to
// read" Returns: 0 = empty, MOTION_BUFFER_SIZE = full
int MotionBuffer::available_unsafe() { return count; }

// ============================================================================
// THREAD-SAFE PUBLIC API (protected by mutex)
// ============================================================================
//
// ⚠️ CRITICAL ISR SAFETY WARNING (Code Audit Finding):
// ========================================================
// All public functions use xSemaphoreTake() - NOT ISR-SAFE!
//
// Current Architecture: ✅ SAFE
// - Called from: taskMotionFunction() [FreeRTOS Task]
// - Execution: Task context (100Hz loop with vTaskDelay)
// - Safety: Mutexes are safe in task context
//
// Future Risk: ❌ CRASH if migrated to hardware timer ISR!
// - If motion control switches to timer ISR, this will crash
// - Cannot take mutexes from ISR context
// - ESP32 will panic: "assert failed: xQueueGenericReceive"
//
// Migration Path (if needed):
// 1. Keep task-based: Use deferred work pattern (ISR signals task)
// 2. OR use pop_unsafe() inside portENTER_CRITICAL_ISR (blocks interrupts)
// 3. OR implement lockless ring buffer (complex, error-prone)
//
// Recommendation: Keep current task-based architecture ✅
// See: docs/ISR_SAFETY_MOTION_BUFFER.md for full analysis
// ============================================================================

bool MotionBuffer::push(float x, float y, float z, float a, float speed) {
  if (buffer_mutex == NULL) {
    logError("[BUFFER] ERROR: mutex not initialized");
    return false;
  }

  if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 5000) {
      logWarning("[BUFFER] Push timeout - buffer locked");
      last_log = millis();
    }
    return false;
  }

  bool result = push_unsafe(x, y, z, a, speed);

  xSemaphoreGive(buffer_mutex);
  return result;
}

bool MotionBuffer::pop(motion_cmd_t *cmd) {
  if (buffer_mutex == NULL) {
    logError("[BUFFER] ERROR: mutex not initialized");
    return false;
  }

  if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 5000) {
      logWarning("[BUFFER] Pop timeout - buffer locked");
      last_log = millis();
    }
    return false;
  }

  bool was_full = isFull_unsafe();
  bool result = pop_unsafe(cmd);

  // PHASE 5.10: Signal event if buffer has space now
  if (was_full && result) {
    systemEventsMotionSet(EVENT_MOTION_BUFFER_READY);
  }

  xSemaphoreGive(buffer_mutex);
  return result;
}

bool MotionBuffer::peek(motion_cmd_t *cmd) {
  if (buffer_mutex == NULL) {
    logError("[BUFFER] ERROR: mutex not initialized");
    return false;
  }

  if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 5000) {
      logWarning("[BUFFER] Peek timeout - buffer locked");
      last_log = millis();
    }
    return false;
  }

  bool result = peek_unsafe(cmd);

  xSemaphoreGive(buffer_mutex);
  return result;
}

bool MotionBuffer::isFull() {
  if (buffer_mutex == NULL) {
    logError("[BUFFER] ERROR: mutex not initialized");
    return true; // Return safe default (full)
  }

  if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 5000) {
      logWarning("[BUFFER] IsFull timeout - assuming full");
      last_log = millis();
    }
    return true; // Return safe default
  }

  bool result = isFull_unsafe();

  xSemaphoreGive(buffer_mutex);
  return result;
}

bool MotionBuffer::isEmpty() {
  if (buffer_mutex == NULL) {
    logError("[BUFFER] ERROR: mutex not initialized");
    return true; // Return safe default (empty)
  }

  if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 5000) {
      logWarning("[BUFFER] IsEmpty timeout - assuming empty");
      last_log = millis();
    }
    return true; // Return safe default
  }

  bool result = isEmpty_unsafe();

  xSemaphoreGive(buffer_mutex);
  return result;
}

void MotionBuffer::clear() {
  if (buffer_mutex == NULL) {
    logError("[BUFFER] CRITICAL: clear() called with uninitialized mutex - "
             "cannot proceed");
    return;
  }

  // PHASE 5.1: Must acquire mutex before clearing to prevent race conditions
  if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
    logError("[BUFFER] CRITICAL: Clear timeout - cannot clear buffer safely");
    return; // Do NOT clear without mutex - risk of data corruption
  }

  // Safe to clear while holding mutex
  head = 0;
  tail = 0;
  count = 0;

  xSemaphoreGive(buffer_mutex);
}

// PHASE 5.7: Clarification - Naming Convention
// NOTE: available() returns COUNT USED (not count free!)
// Arduino convention: available() = "data ready to read" (e.g.,
// Serial.available()) This buffer: available() = "items in buffer" (0 = empty,
// 32 = full) Use case: Job manager checks if buffer is almost full to pause
// G-Code reading
int MotionBuffer::available() {
  if (buffer_mutex == NULL) {
    logWarning("[BUFFER] WARNING: available() called with uninitialized mutex");
    return count; // Return stale value as fallback
  }

  if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
    static uint32_t last_log = 0;
    if (millis() - last_log > 5000) {
      logWarning("[BUFFER] Available timeout - returning stale count");
      last_log = millis();
    }
    return count; // Return stale value
  }

  int result = available_unsafe();

  xSemaphoreGive(buffer_mutex);
  return result;
}

int MotionBuffer::getCapacity() {
  return MOTION_BUFFER_SIZE; // This is constant, no lock needed
}
