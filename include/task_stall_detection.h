#ifndef TASK_STALL_DETECTION_H
#define TASK_STALL_DETECTION_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// PHASE 2.5: TASK STALL DETECTION
// ============================================================================

// Threshold for detecting task stall (milliseconds)
// If a task hasn't fed watchdog within this time, it's considered stalled
#define TASK_STALL_THRESHOLD_MS 5000  // 5 seconds

// Maximum consecutive stalls before escalating to CRITICAL fault
#define TASK_STALL_ESCALATION_COUNT 2

// ============================================================================
// TASK STALL STATUS
// ============================================================================

typedef enum {
  STALL_UNKNOWN = 0,
  STALL_NORMAL = 1,       // Task is healthy
  STALL_WARNING = 2,      // Task missed one feed cycle
  STALL_CRITICAL = 3,     // Task missed multiple cycles
} task_stall_status_t;

typedef struct {
  const char* task_name;
  task_stall_status_t status;
  uint32_t stall_count;
  uint32_t last_stall_ms;
  uint32_t recovery_count;
} task_stall_info_t;

// ============================================================================
// TASK STALL DETECTION API
// ============================================================================

// Initialize task stall detection system
void taskStallDetectionInit();

// Check all tasks for stalls and log faults (call periodically)
// Returns number of stalled tasks detected
uint8_t taskStallDetectionUpdate();

// Get stall status for a specific task
task_stall_status_t taskGetStallStatus(const char* task_name);

// Get detailed stall info for all tasks
task_stall_info_t* taskGetStallInfo(uint8_t* out_count);

// Reset stall detection statistics
void taskStallDetectionReset();

#endif
