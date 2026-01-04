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

private:
    File jobFile;
    job_status_t status;
    bool file_open;
    
    // Config
    uint32_t buffer_low_water_mark; // When to resume filling buffer
};

extern JobManager jobManager;

#endif
