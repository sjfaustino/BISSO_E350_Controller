/**
 * @file job_manager.h
 * @brief G-Code File Streaming Engine
 * @project PosiPro
 */

#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <Arduino.h>
#include <FS.h>

typedef enum {
    JOB_IDLE = 0,
    JOB_RUNNING = 1,
    JOB_PAUSED = 2,
    JOB_COMPLETED = 3,
    JOB_ERROR = 4
} job_state_t;

typedef struct {
    char filename[64];
    uint32_t total_lines;
    uint32_t current_line;
    uint32_t start_time;
    uint32_t duration_ms;
    job_state_t state;
} job_status_t;

// Per-job statistics
typedef struct {
    uint32_t move_count;
    float total_distance_mm;
    uint32_t pause_count;
    uint32_t pause_duration_ms;
    uint32_t alarm_count;
    uint32_t elapsed_ms;
} job_stats_t;

class JobManager {
public:
    JobManager();
    void init();
    void update(); // Called by background task

    // Control API
    bool startJob(const char* filename);
    void pauseJob();
    void resumeJob();
    void abortJob();

    job_status_t getStatus();
    bool isRunning();

    // Per-job statistics
    job_stats_t getJobStats();
    void logJobComplete();
    void recordMove(float distance_mm);
    void recordAlarm();

private:
    File jobFile;
    job_status_t status;
    bool file_open;
    
    // Config
    uint32_t buffer_low_water_mark; // When to resume filling buffer

    // Per-job stats tracking
    job_stats_t stats;
    uint32_t pause_start_time;
};

extern JobManager jobManager;

#endif
