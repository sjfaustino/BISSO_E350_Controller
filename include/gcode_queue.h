/**
 * @file gcode_queue.h
 * @brief G-Code Job Queue Manager with Execution History and Error Recovery
 * 
 * Provides server-side tracking of G-code command execution with
 * support for batch operations and failure recovery.
 */

#ifndef GCODE_QUEUE_H
#define GCODE_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of jobs to track in history
#define GCODE_QUEUE_MAX_JOBS 20
#define GCODE_CMD_MAX_LEN 64
#define GCODE_ERR_MAX_LEN 32

// Job execution status
typedef enum {
    JOB_PENDING = 0,    // Queued, not yet started
    JOB_RUNNING,        // Currently executing
    JOB_COMPLETED,      // Finished successfully
    JOB_FAILED,         // Execution failed
    JOB_SKIPPED         // Skipped by operator
} gcode_job_status_t;

// Single job entry in the queue
typedef struct {
    uint16_t id;                        // Unique job ID
    char command[GCODE_CMD_MAX_LEN];    // G-code command string
    float start_pos[4];                 // Position before execution (X, Y, Z, A)
    uint32_t queued_time_ms;            // Timestamp when queued
    uint32_t start_time_ms;             // Timestamp when started
    uint32_t end_time_ms;               // Timestamp when completed/failed
    gcode_job_status_t status;          // Current status
    char error[GCODE_ERR_MAX_LEN];      // Error message if failed
} gcode_job_t;

// Queue state for API responses
typedef struct {
    uint16_t total_jobs;                // Total jobs in history
    uint16_t pending_count;             // Jobs waiting to execute
    uint16_t completed_count;           // Jobs completed successfully
    uint16_t failed_count;              // Jobs that failed
    uint16_t current_job_id;            // ID of currently running job (0 if none)
    bool paused;                        // Queue paused due to error
} gcode_queue_state_t;

// Initialize the queue system
void gcodeQueueInit();

// Add a new job to the queue (returns job ID, 0 on failure)
uint16_t gcodeQueueAdd(const char* command);

// Get current job being executed (NULL if none)
gcode_job_t* gcodeQueueGetCurrent();

// Get job by ID (NULL if not found)
gcode_job_t* gcodeQueueGetJob(uint16_t id);

// Get queue state summary
gcode_queue_state_t gcodeQueueGetState();

// Get all jobs as array (for API, returns count)
uint16_t gcodeQueueGetAll(gcode_job_t* jobs, uint16_t max_count);

// Mark current job as running (called when execution starts)
void gcodeQueueMarkRunning();

// Mark current job as completed
void gcodeQueueMarkCompleted();

// Mark current job as failed with error message
void gcodeQueueMarkFailed(const char* error);

// Recovery actions (called by operator)
bool gcodeQueueRetry();      // Retry failed job from start position
bool gcodeQueueResume();     // Resume from current position
bool gcodeQueueSkip();       // Skip to next job

// Clear queue history
void gcodeQueueClear();

// Start/stop queue processing
void gcodeQueueStart();
void gcodeQueuePause();
bool gcodeQueueIsRunning();
bool gcodeQueueIsPaused();

#endif // GCODE_QUEUE_H
