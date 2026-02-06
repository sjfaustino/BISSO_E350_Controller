/**
 * @file gcode_queue.cpp
 * @brief G-Code Job Queue Manager Implementation
 */

#include "gcode_queue.h"
#include "motion.h"
#include "motion_state.h"
#include "serial_logger.h"
#include "psram_alloc.h"    // PSRAM allocations
#include <Arduino.h>
#include <cstring>

// Ring buffer for job history
static gcode_job_t* jobs = nullptr; // Pointer for PSRAM allocation
static uint16_t job_head = 0;        // Next write position
static uint16_t job_count = 0;       // Total jobs in buffer
static uint16_t next_job_id = 1;     // Auto-increment ID
static uint16_t current_job_idx = UINT16_MAX;  // Currently executing job
static bool queue_running = false;   // Queue processor active
static bool queue_paused = false;    // Paused due to error

// Spinlock for thread safety
static portMUX_TYPE queueSpinlock = portMUX_INITIALIZER_UNLOCKED;

void gcodeQueueInit() {
    // CRITICAL: Allocate jobs array in PSRAM for ESP32-S3
    if (jobs == nullptr) {
        jobs = (gcode_job_t*)psramCalloc(GCODE_QUEUE_MAX_JOBS, sizeof(gcode_job_t));
        if (jobs == nullptr) {
            logError("[QUEUE] CRITICAL: Failed to allocate G-Code queue in PSRAM!");
            return;
        }
        logInfo("[QUEUE] Allocated %d job entries in PSRAM (%u bytes)", 
                GCODE_QUEUE_MAX_JOBS, (uint32_t)(GCODE_QUEUE_MAX_JOBS * sizeof(gcode_job_t)));
    }

    portENTER_CRITICAL(&queueSpinlock);
    memset(jobs, 0, GCODE_QUEUE_MAX_JOBS * sizeof(gcode_job_t));
    job_head = 0;
    job_count = 0;
    next_job_id = 1;
    current_job_idx = UINT16_MAX;
    queue_running = false;
    queue_paused = false;
    portEXIT_CRITICAL(&queueSpinlock);
    logInfo("[QUEUE] Initialized with capacity %d", GCODE_QUEUE_MAX_JOBS);
}

uint16_t gcodeQueueAdd(const char* command) {
    if (!command || strlen(command) == 0) return 0;
    
    portENTER_CRITICAL(&queueSpinlock);
    
    // Store at head position
    gcode_job_t* job = &jobs[job_head];
    memset(job, 0, sizeof(gcode_job_t));
    
    job->id = next_job_id++;
    strncpy(job->command, command, GCODE_CMD_MAX_LEN - 1);
    job->command[GCODE_CMD_MAX_LEN - 1] = '\0';
    
    // Capture current position as start position
    job->start_pos[0] = motionGetPositionMM(0);
    job->start_pos[1] = motionGetPositionMM(1);
    job->start_pos[2] = motionGetPositionMM(2);
    job->start_pos[3] = motionGetPositionMM(3);
    
    job->queued_time_ms = millis();
    job->status = JOB_PENDING;
    
    uint16_t id = job->id;
    
    // Advance head
    job_head = (job_head + 1) % GCODE_QUEUE_MAX_JOBS;
    if (job_count < GCODE_QUEUE_MAX_JOBS) job_count++;
    
    portEXIT_CRITICAL(&queueSpinlock);
    
    logInfo("[QUEUE] Added job #%d: %s", id, command);
    return id;
}

gcode_job_t* gcodeQueueGetCurrent() {
    if (current_job_idx >= GCODE_QUEUE_MAX_JOBS) return nullptr;
    return &jobs[current_job_idx];
}

gcode_job_t* gcodeQueueGetJob(uint16_t id) {
    portENTER_CRITICAL(&queueSpinlock);
    for (uint16_t i = 0; i < job_count; i++) {
        uint16_t idx = (job_head - 1 - i + GCODE_QUEUE_MAX_JOBS) % GCODE_QUEUE_MAX_JOBS;
        if (jobs[idx].id == id) {
            portEXIT_CRITICAL(&queueSpinlock);
            return &jobs[idx];
        }
    }
    portEXIT_CRITICAL(&queueSpinlock);
    return nullptr;
}

gcode_queue_state_t gcodeQueueGetState() {
    gcode_queue_state_t state = {0, 0, 0, 0, 0, false};
    
    portENTER_CRITICAL(&queueSpinlock);
    state.total_jobs = job_count;
    state.paused = queue_paused;
    
    for (uint16_t i = 0; i < job_count; i++) {
        uint16_t idx = (job_head - 1 - i + GCODE_QUEUE_MAX_JOBS) % GCODE_QUEUE_MAX_JOBS;
        switch (jobs[idx].status) {
            case JOB_PENDING: state.pending_count++; break;
            case JOB_RUNNING: 
                state.current_job_id = jobs[idx].id; 
                break;
            case JOB_COMPLETED: state.completed_count++; break;
            case JOB_FAILED: state.failed_count++; break;
            default: break;
        }
    }
    portEXIT_CRITICAL(&queueSpinlock);
    
    return state;
}

uint16_t gcodeQueueGetAll(gcode_job_t* out_jobs, uint16_t max_count) {
    portENTER_CRITICAL(&queueSpinlock);
    uint16_t count = (job_count < max_count) ? job_count : max_count;
    
    // Copy jobs in reverse order (newest first)
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (job_head - 1 - i + GCODE_QUEUE_MAX_JOBS) % GCODE_QUEUE_MAX_JOBS;
        memcpy(&out_jobs[i], &jobs[idx], sizeof(gcode_job_t));
    }
    portEXIT_CRITICAL(&queueSpinlock);
    
    return count;
}

void gcodeQueueMarkRunning() {
    // Find next pending job and mark it running
    portENTER_CRITICAL(&queueSpinlock);
    for (uint16_t i = 0; i < job_count; i++) {
        uint16_t idx = (job_head - job_count + i + GCODE_QUEUE_MAX_JOBS) % GCODE_QUEUE_MAX_JOBS;
        if (jobs[idx].status == JOB_PENDING) {
            jobs[idx].status = JOB_RUNNING;
            jobs[idx].start_time_ms = millis();
            current_job_idx = idx;
            portEXIT_CRITICAL(&queueSpinlock);
            logInfo("[QUEUE] Job #%d started", jobs[idx].id);
            return;
        }
    }
    current_job_idx = UINT16_MAX;
    portEXIT_CRITICAL(&queueSpinlock);
}

void gcodeQueueMarkCompleted() {
    if (current_job_idx >= GCODE_QUEUE_MAX_JOBS) return;
    
    portENTER_CRITICAL(&queueSpinlock);
    jobs[current_job_idx].status = JOB_COMPLETED;
    jobs[current_job_idx].end_time_ms = millis();
    uint16_t id = jobs[current_job_idx].id;
    current_job_idx = UINT16_MAX;
    portEXIT_CRITICAL(&queueSpinlock);
    
    logInfo("[QUEUE] Job #%d completed", id);
}

void gcodeQueueMarkFailed(const char* error) {
    if (current_job_idx >= GCODE_QUEUE_MAX_JOBS) return;
    
    portENTER_CRITICAL(&queueSpinlock);
    jobs[current_job_idx].status = JOB_FAILED;
    jobs[current_job_idx].end_time_ms = millis();
    if (error) {
        strncpy(jobs[current_job_idx].error, error, GCODE_ERR_MAX_LEN - 1);
        jobs[current_job_idx].error[GCODE_ERR_MAX_LEN - 1] = '\0';
    }
    uint16_t id = jobs[current_job_idx].id;
    queue_paused = true;  // Pause queue on error
    portEXIT_CRITICAL(&queueSpinlock);
    
    logWarning("[QUEUE] Job #%d failed: %s", id, error ? error : "Unknown");
}

bool gcodeQueueRetry() {
    if (current_job_idx >= GCODE_QUEUE_MAX_JOBS) return false;
    
    gcode_job_t* job = &jobs[current_job_idx];
    if (job->status != JOB_FAILED) return false;
    
    // Move back to starting position
    logInfo("[QUEUE] Retrying job #%d - moving to start position", job->id);
    
    // Execute move to start position
    if (!motionMoveAbsolute(job->start_pos[0], job->start_pos[1], 
                            job->start_pos[2], job->start_pos[3], 
                            300.0f)) {  // Slow speed for recovery
        logError("[QUEUE] Failed to move to start position");
        return false;
    }
    
    // Reset job status and retry
    portENTER_CRITICAL(&queueSpinlock);
    job->status = JOB_PENDING;
    job->error[0] = '\0';
    queue_paused = false;
    portEXIT_CRITICAL(&queueSpinlock);
    
    return true;
}

bool gcodeQueueResume() {
    if (current_job_idx >= GCODE_QUEUE_MAX_JOBS) return false;
    
    gcode_job_t* job = &jobs[current_job_idx];
    if (job->status != JOB_FAILED) return false;
    
    logInfo("[QUEUE] Resuming queue from current position");
    
    // Mark current job as completed (operator says it's fine)
    portENTER_CRITICAL(&queueSpinlock);
    job->status = JOB_COMPLETED;
    job->end_time_ms = millis();
    queue_paused = false;
    current_job_idx = UINT16_MAX;
    portEXIT_CRITICAL(&queueSpinlock);
    
    return true;
}

bool gcodeQueueSkip() {
    if (current_job_idx >= GCODE_QUEUE_MAX_JOBS) return false;
    
    gcode_job_t* job = &jobs[current_job_idx];
    if (job->status != JOB_FAILED) return false;
    
    logInfo("[QUEUE] Skipping job #%d", job->id);
    
    portENTER_CRITICAL(&queueSpinlock);
    job->status = JOB_SKIPPED;
    job->end_time_ms = millis();
    queue_paused = false;
    current_job_idx = UINT16_MAX;
    portEXIT_CRITICAL(&queueSpinlock);
    
    return true;
}

void gcodeQueueClear() {
    portENTER_CRITICAL(&queueSpinlock);
    memset(jobs, 0, sizeof(jobs));
    job_head = 0;
    job_count = 0;
    current_job_idx = UINT16_MAX;
    queue_paused = false;
    portEXIT_CRITICAL(&queueSpinlock);
    logInfo("[QUEUE] Cleared");
}

void gcodeQueueStart() {
    queue_running = true;
    queue_paused = false;
    logInfo("[QUEUE] Started");
}

void gcodeQueuePause() {
    queue_paused = true;
    logInfo("[QUEUE] Paused");
}

bool gcodeQueueIsRunning() {
    return queue_running && !queue_paused;
}

bool gcodeQueueIsPaused() {
    return queue_paused;
}
