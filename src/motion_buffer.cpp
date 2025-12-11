/**
 * @file motion_buffer.cpp
 * @brief Implementation of Ring Buffer (Gemini v3.5.23)
 * @details THREAD-SAFE implementation with mutex protection.
 *          All public methods are protected by buffer_mutex.
 */

#include "motion_buffer.h"
#include "serial_logger.h"
#include <string.h>

MotionBuffer motionBuffer;

MotionBuffer::MotionBuffer() {
    head = 0;
    tail = 0;
    count = 0;
    buffer_mutex = NULL;  // Will be set by setMutex()
}

void MotionBuffer::init() {
    if (buffer_mutex == NULL) {
        logWarning("[BUFFER] Warning: mutex not set. Call setMutex() first!");
    }
    head = 0;
    tail = 0;
    count = 0;
    logInfo("[BUFFER] Initialized (Size: %d)", MOTION_BUFFER_SIZE);
}

void MotionBuffer::setMutex(SemaphoreHandle_t mtx) {
    buffer_mutex = mtx;
}

// ============================================================================
// UNSAFE INTERNAL IMPLEMENTATIONS (called within critical section)
// ============================================================================

bool MotionBuffer::push_unsafe(float x, float y, float z, float a, float speed) {
    if (count >= MOTION_BUFFER_SIZE) return false;

    buffer[head].x = x;
    buffer[head].y = y;
    buffer[head].z = z;
    buffer[head].a = a;
    buffer[head].speed_mm_s = speed;

    head = (head + 1) % MOTION_BUFFER_SIZE;
    count++;
    return true;
}

bool MotionBuffer::pop_unsafe(motion_cmd_t* cmd) {
    if (count == 0) return false;

    if (cmd) *cmd = buffer[tail];

    tail = (tail + 1) % MOTION_BUFFER_SIZE;
    count--;
    return true;
}

bool MotionBuffer::peek_unsafe(motion_cmd_t* cmd) {
    if (count == 0) return false;
    if (cmd) *cmd = buffer[tail];
    return true;
}

bool MotionBuffer::isFull_unsafe() {
    return count >= MOTION_BUFFER_SIZE;
}

bool MotionBuffer::isEmpty_unsafe() {
    return count == 0;
}

int MotionBuffer::available_unsafe() {
    return count;
}

// ============================================================================
// THREAD-SAFE PUBLIC API (protected by mutex)
// ============================================================================

bool MotionBuffer::push(float x, float y, float z, float a, float speed) {
    if (buffer_mutex == NULL) {
        logError("[BUFFER] ERROR: mutex not initialized");
        return false;
    }

    if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
        logWarning("[BUFFER] Push timeout - buffer locked");
        return false;
    }

    bool result = push_unsafe(x, y, z, a, speed);

    xSemaphoreGive(buffer_mutex);
    return result;
}

bool MotionBuffer::pop(motion_cmd_t* cmd) {
    if (buffer_mutex == NULL) {
        logError("[BUFFER] ERROR: mutex not initialized");
        return false;
    }

    if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
        logWarning("[BUFFER] Pop timeout - buffer locked");
        return false;
    }

    bool result = pop_unsafe(cmd);

    xSemaphoreGive(buffer_mutex);
    return result;
}

bool MotionBuffer::peek(motion_cmd_t* cmd) {
    if (buffer_mutex == NULL) {
        logError("[BUFFER] ERROR: mutex not initialized");
        return false;
    }

    if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
        logWarning("[BUFFER] Peek timeout - buffer locked");
        return false;
    }

    bool result = peek_unsafe(cmd);

    xSemaphoreGive(buffer_mutex);
    return result;
}

bool MotionBuffer::isFull() {
    if (buffer_mutex == NULL) {
        logError("[BUFFER] ERROR: mutex not initialized");
        return true;  // Return safe default (full)
    }

    if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
        logWarning("[BUFFER] IsFull timeout - assuming full");
        return true;  // Return safe default
    }

    bool result = isFull_unsafe();

    xSemaphoreGive(buffer_mutex);
    return result;
}

bool MotionBuffer::isEmpty() {
    if (buffer_mutex == NULL) {
        logError("[BUFFER] ERROR: mutex not initialized");
        return true;  // Return safe default (empty)
    }

    if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
        logWarning("[BUFFER] IsEmpty timeout - assuming empty");
        return true;  // Return safe default
    }

    bool result = isEmpty_unsafe();

    xSemaphoreGive(buffer_mutex);
    return result;
}

void MotionBuffer::clear() {
    if (buffer_mutex == NULL) {
        logWarning("[BUFFER] WARNING: clear() called with uninitialized mutex");
        head = tail = count = 0;
        return;
    }

    if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
        logWarning("[BUFFER] Clear timeout - force clearing");
    }

    head = 0;
    tail = 0;
    count = 0;

    if (buffer_mutex != NULL) {
        xSemaphoreGive(buffer_mutex);
    }
}

int MotionBuffer::available() {
    if (buffer_mutex == NULL) {
        logWarning("[BUFFER] WARNING: available() called with uninitialized mutex");
        return count;  // Return stale value as fallback
    }

    if (!xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100))) {
        logWarning("[BUFFER] Available timeout - returning stale count");
        return count;  // Return stale value
    }

    int result = available_unsafe();

    xSemaphoreGive(buffer_mutex);
    return result;
}

int MotionBuffer::getCapacity() {
    return MOTION_BUFFER_SIZE;  // This is constant, no lock needed
}