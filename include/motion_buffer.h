/**
 * @file motion_buffer.h
 * @brief Ring Buffer for Motion Commands (PosiPro)
 * @details Thread-safe implementation with mutex protection for multi-task access
 */

#ifndef MOTION_BUFFER_H
#define MOTION_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define MOTION_BUFFER_SIZE 1024 // Increased from 32 using PSRAM on ESP32-S3

// CRITICAL FIX: Store positions as integer counts to prevent float drift
// Float arithmetic accumulates rounding errors over long jobs (hours/days)
// Commands stored in encoder counts/steps, only converted to MM for display
typedef struct {
    int32_t x_counts;      // X position in encoder counts
    int32_t y_counts;      // Y position in encoder counts
    int32_t z_counts;      // Z position in encoder counts
    int32_t a_counts;      // A position in encoder counts
    float speed_mm_s;      // Speed (not accumulated, safe to use float)
} motion_cmd_t;

class MotionBuffer {
public:
    MotionBuffer();
    void init();
    void setMutex(SemaphoreHandle_t mtx);  // Called by task manager during init

    // Core Ops (THREAD-SAFE with mutex)
    bool push(float x, float y, float z, float a, float speed);
    bool pop(motion_cmd_t* cmd);
    bool peek(motion_cmd_t* cmd);

    // State Ops (THREAD-SAFE with mutex)
    bool isFull();
    bool isEmpty();
    void clear();

    // Status Queries (THREAD-SAFE with mutex)
    int available();     // Returns number of items currently in buffer
    int getCapacity();   // Returns total size (1024)

private:
    motion_cmd_t* buffer; // Pointer for PSRAM allocation
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    SemaphoreHandle_t buffer_mutex;  // Protects head, tail, count modifications

    // Internal unsafe versions (called within critical section)
    bool push_unsafe(float x, float y, float z, float a, float speed);
    bool pop_unsafe(motion_cmd_t* cmd);
    bool peek_unsafe(motion_cmd_t* cmd);
    bool isFull_unsafe();
    bool isEmpty_unsafe();
    int available_unsafe(); 
};

extern MotionBuffer motionBuffer;

#endif
