/**
 * @file motion_buffer.h
 * @brief Ring Buffer for G-Code Look-Ahead
 * @project Gemini v1.3.0
 */

#ifndef MOTION_BUFFER_H
#define MOTION_BUFFER_H

#include <Arduino.h>
#include "motion.h"

// Buffer Depth (Number of pending moves)
#define MOTION_BUFFER_DEPTH 16

typedef struct {
    float x, y, z, a;
    float speed_mm_s;
    bool is_valid;
} motion_cmd_t;

class MotionBuffer {
public:
    MotionBuffer();
    void init();
    
    // Producer Methods
    bool push(float x, float y, float z, float a, float speed);
    bool isFull();
    bool isEmpty();
    
    // Consumer Methods
    bool pop(motion_cmd_t* cmd);
    bool peek(motion_cmd_t* cmd);
    
    // Management
    void clear();
    uint32_t getCount();

private:
    motion_cmd_t buffer[MOTION_BUFFER_DEPTH];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
};

extern MotionBuffer motionBuffer;

#endif