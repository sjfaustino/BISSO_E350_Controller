/**
 * @file motion_buffer.h
 * @brief Ring Buffer for Motion Commands (Gemini v3.5.23)
 */

#ifndef MOTION_BUFFER_H
#define MOTION_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#define MOTION_BUFFER_SIZE 32 // Power of 2 for efficiency

typedef struct {
    float x, y, z, a;
    float speed_mm_s;
} motion_cmd_t;

class MotionBuffer {
public:
    MotionBuffer();
    void init();
    
    // Core Ops
    bool push(float x, float y, float z, float a, float speed);
    bool pop(motion_cmd_t* cmd);
    bool peek(motion_cmd_t* cmd);
    
    // State Ops
    bool isFull();
    bool isEmpty();
    void clear();
    
    // NEW: Added for Grbl Status Reporting
    int available();     // Returns number of items currently in buffer
    int getCapacity();   // Returns total size (32)

private:
    motion_cmd_t buffer[MOTION_BUFFER_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
    volatile int count;
};

extern MotionBuffer motionBuffer;

#endif