/**
 * @file motion_buffer.cpp
 * @brief Implementation of Ring Buffer (Gemini v3.5.23)
 */

#include "motion_buffer.h"
#include "serial_logger.h"
#include <string.h>

MotionBuffer motionBuffer;

MotionBuffer::MotionBuffer() {
    head = 0;
    tail = 0;
    count = 0;
}

void MotionBuffer::init() {
    clear();
    logInfo("[BUFFER] Initialized (Size: %d)", MOTION_BUFFER_SIZE);
}

void MotionBuffer::clear() {
    head = 0;
    tail = 0;
    count = 0;
}

bool MotionBuffer::push(float x, float y, float z, float a, float speed) {
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

bool MotionBuffer::pop(motion_cmd_t* cmd) {
    if (count == 0) return false;

    if (cmd) *cmd = buffer[tail];

    tail = (tail + 1) % MOTION_BUFFER_SIZE;
    count--;
    return true;
}

bool MotionBuffer::peek(motion_cmd_t* cmd) {
    if (count == 0) return false;
    if (cmd) *cmd = buffer[tail];
    return true;
}

bool MotionBuffer::isFull() {
    return count >= MOTION_BUFFER_SIZE;
}

bool MotionBuffer::isEmpty() {
    return count == 0;
}

// NEW: Return number of elements currently stored
int MotionBuffer::available() {
    return count;
}

int MotionBuffer::getCapacity() {
    return MOTION_BUFFER_SIZE;
}