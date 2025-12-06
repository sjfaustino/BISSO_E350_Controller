#include "motion_buffer.h"
#include "serial_logger.h"

MotionBuffer motionBuffer;

MotionBuffer::MotionBuffer() {
    clear();
}

void MotionBuffer::init() {
    clear();
    logInfo("[BUFFER] Initialized (Depth: %d)", MOTION_BUFFER_DEPTH);
}

void MotionBuffer::clear() {
    head = 0;
    tail = 0;
    count = 0;
    for(int i=0; i<MOTION_BUFFER_DEPTH; i++) buffer[i].is_valid = false;
}

bool MotionBuffer::push(float x, float y, float z, float a, float speed) {
    if (isFull()) return false;
    
    buffer[head].x = x;
    buffer[head].y = y;
    buffer[head].z = z;
    buffer[head].a = a;
    buffer[head].speed_mm_s = speed;
    buffer[head].is_valid = true;
    
    head = (head + 1) % MOTION_BUFFER_DEPTH;
    count++;
    return true;
}

bool MotionBuffer::pop(motion_cmd_t* cmd) {
    if (isEmpty() || !cmd) return false;
    
    *cmd = buffer[tail];
    buffer[tail].is_valid = false;
    
    tail = (tail + 1) % MOTION_BUFFER_DEPTH;
    count--;
    return true;
}

bool MotionBuffer::peek(motion_cmd_t* cmd) {
    if (isEmpty() || !cmd) return false;
    *cmd = buffer[tail];
    return true;
}

bool MotionBuffer::isFull() { return count >= MOTION_BUFFER_DEPTH; }
bool MotionBuffer::isEmpty() { return count == 0; }
uint32_t MotionBuffer::getCount() { return count; }