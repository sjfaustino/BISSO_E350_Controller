#include "job_manager.h"
#include "gcode_parser.h"
#include "motion_buffer.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "config_unified.h"
#include "config_keys.h"
#include <SPIFFS.h>

JobManager jobManager;

JobManager::JobManager() : file_open(false), buffer_low_water_mark(4) {
    memset(&status, 0, sizeof(status));
    status.state = JOB_IDLE;
}

void JobManager::init() {
    logInfo("[JOB] Initializing Job Engine...");
    // Ensure SPIFFS is mounted (usually handled by web_server, but safe to check)
    if(!SPIFFS.begin(true)) {
        logError("[JOB] SPIFFS Mount Fail");
    }
}

void JobManager::update() {
    if (status.state != JOB_RUNNING || !file_open) return;

    // 1. Flow Control: Check Buffer Space
    // Only parse more lines if the buffer has room.
    // We leave 1 slot free to prevent overflow race conditions.
    if (motionBuffer.getCount() >= (MOTION_BUFFER_DEPTH - 1)) {
        return; // Buffer full, yield
    }

    // 2. Read & Parse Loop
    // Process up to 5 lines per update to keep buffer full without hogging CPU
    for (int i = 0; i < 5; i++) {
        if (motionBuffer.isFull()) break;

        if (jobFile.available()) {
            String line = jobFile.readStringUntil('\n');
            line.trim();
            status.current_line++;

            if (line.length() > 0) {
                // Pass to G-Code Parser
                // Note: Parser handles buffering if enabled
                if (!gcodeParser.processCommand(line.c_str())) {
                    logWarning("[JOB] Line %d ignored: %s", status.current_line, line.c_str());
                }
            }
        } else {
            // EOF Reached
            logInfo("[JOB] File EOF. Waiting for motion to finish...");
            // We don't switch to COMPLETE yet; we wait for buffer to drain.
            // But for simplicity in v1, we mark complete here.
            // Ideal: Check motionBuffer.isEmpty() && motionIsIdle().
            
            jobFile.close();
            file_open = false;
            status.state = JOB_COMPLETED;
            status.duration_ms = millis() - status.start_time;
            logInfo("[JOB] Job Completed in %lu ms", status.duration_ms);
            return;
        }
    }
}

bool JobManager::startJob(const char* filename) {
    if (status.state == JOB_RUNNING || status.state == JOB_PAUSED) {
        logError("[JOB] Busy");
        return false;
    }

    // Force Buffer Mode ON for Jobs
    configSetInt(KEY_MOTION_BUFFER_ENABLE, 1);

    if (!SPIFFS.exists(filename)) {
        logError("[JOB] File not found: %s", filename);
        return false;
    }

    jobFile = SPIFFS.open(filename, "r");
    if (!jobFile) {
        logError("[JOB] Failed to open file");
        return false;
    }

    file_open = true;
    strncpy(status.filename, filename, 63);
    status.current_line = 0;
    status.total_lines = 0; // Could pre-scan if needed
    status.start_time = millis();
    status.state = JOB_RUNNING;
    
    // Clear buffer before starting
    motionBuffer.clear();
    
    logInfo("[JOB] Started: %s", filename);
    return true;
}

void JobManager::pauseJob() {
    if (status.state == JOB_RUNNING) {
        status.state = JOB_PAUSED;
        logInfo("[JOB] Paused");
        // Note: Motion continues until buffer empties unless we also motionPause()
        // Ideally:
        // motionPause();
    }
}

void JobManager::resumeJob() {
    if (status.state == JOB_PAUSED) {
        status.state = JOB_RUNNING;
        logInfo("[JOB] Resumed");
        // motionResume();
    }
}

void JobManager::abortJob() {
    if (file_open) jobFile.close();
    file_open = false;
    status.state = JOB_IDLE;
    motionBuffer.clear(); // Kill pending moves
    motionStop(); // Stop hardware
    logWarning("[JOB] Aborted");
}

job_status_t JobManager::getStatus() { return status; }
bool JobManager::isRunning() { return status.state == JOB_RUNNING; }