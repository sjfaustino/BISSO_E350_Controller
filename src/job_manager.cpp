/**
 * @file job_manager.cpp
 * @brief G-Code Job Streaming Engine (PosiPro)
 * @details Fixed compilation errors (missing includes, API mismatch).
 */

#include "job_manager.h"
#include "gcode_parser.h"
#include "motion.h"        // <-- FIX: Added missing include for motionStop
#include "motion_buffer.h" // Provides MOTION_BUFFER_SIZE
#include "serial_logger.h"
#include "fault_logging.h"
#include "config_unified.h"
#include "config_keys.h"
#include "cutting_analytics.h"  // ITEM 3: Session summary at job end
#include "operator_alerts.h"    // ITEM 3: Job complete alert
#include <LittleFS.h>

JobManager jobManager;

JobManager::JobManager() : file_open(false), buffer_low_water_mark(4) {
    memset(&status, 0, sizeof(status));
    status.state = JOB_IDLE;
}

void JobManager::init() {
    logInfo("[JOB] Initializing Job Engine...");
    if(!LittleFS.begin(true)) {
        logError("[JOB] LittleFS Mount Fail");
    }
}

void JobManager::update() {
    if (status.state != JOB_RUNNING || !file_open) return;

    // PHASE 5.7: Clarification - Buffer Naming Convention
    // NOTE: motionBuffer.available() returns COUNT USED (not count free!)
    // This is opposite of Arduino convention where available() typically means "data ready to read"
    // Here: available() returns number of commands IN buffer (0 = empty, 32 = full)
    // Logic: Stop reading G-Code file if buffer is almost full (only 1 slot free)
    // Prevents buffer overflow while allowing pipeline to continue draining
    if (motionBuffer.available() >= (MOTION_BUFFER_SIZE - 1)) {
        return;  // Buffer almost full - wait for motion task to consume commands
    }

    for (int i = 0; i < 5; i++) {
        if (motionBuffer.isFull()) break;

        if (jobFile.available()) {
            String line = jobFile.readStringUntil('\n');
            line.trim();
            status.current_line++;

            if (line.length() > 0) {
                if (!gcodeParser.processCommand(line.c_str())) {
                    logWarning("[JOB] Line %d ignored: %s", status.current_line, line.c_str());
                }
            }
        } else {
            logInfo("[JOB] File EOF. Waiting for motion to finish...");
            
            jobFile.close();
            file_open = false;
            status.state = JOB_COMPLETED;
            status.duration_ms = millis() - status.start_time;
            
            // ITEM 3: Display session summary at job end
            cuttingEndSession();
            const cutting_session_t* session = cuttingGetSession();
            logPrintln("\n[JOB] ========== JOB COMPLETE ==========");
            logPrintf("[JOB] Duration:   %.1f seconds\n", status.duration_ms / 1000.0f);
            logPrintf("[JOB] Lines:      %lu / %lu\n", 
                     (unsigned long)status.current_line, (unsigned long)status.total_lines);
            if (session && session->total_energy_joules > 0) {
                logPrintf("[JOB] Energy:     %.1f J (%.4f kWh)\n", 
                         session->total_energy_joules, 
                         session->total_energy_joules / 3600000.0f);
                logPrintf("[JOB] Material:   %.1f mm³\n", session->total_material_mm3);
                logPrintf("[JOB] Peak Amps:  %.2f A\n", session->peak_current_amps);
            }
            logPrintln("[JOB] ======================================");
            
            // Trigger job complete alert (buzzer + status light)
            alertJobComplete();
            
            return;
        }
    }
}

bool JobManager::startJob(const char* filename) {
    if (status.state == JOB_RUNNING || status.state == JOB_PAUSED) {
        logError("[JOB] Busy");
        return false;
    }

    configSetInt(KEY_MOTION_BUFFER_ENABLE, 1);

    if (!LittleFS.exists(filename)) {
        logError("[JOB] File not found: %s", filename);
        return false;
    }

    // ITEM 2: Count total lines for ETA estimation
    File countFile = LittleFS.open(filename, "r");
    if (countFile) {
        uint32_t lineCount = 0;
        while (countFile.available()) {
            if (countFile.read() == '\n') lineCount++;
        }
        status.total_lines = lineCount;
        countFile.close();
        logInfo("[JOB] File has %lu lines", (unsigned long)lineCount);
    }

    jobFile = LittleFS.open(filename, "r");
    if (!jobFile) {
        logError("[JOB] Failed to open file");
        return false;
    }

    file_open = true;
    strncpy(status.filename, filename, 63);
    status.current_line = 0;
    status.start_time = millis();
    status.state = JOB_RUNNING;
    
    motionBuffer.clear();
    
    logInfo("[JOB] Started: %s", filename);
    return true;
}

void JobManager::pauseJob() {
    if (status.state == JOB_RUNNING) {
        status.state = JOB_PAUSED;
        logInfo("[JOB] Paused");
        motionPause();
    }
}

void JobManager::resumeJob() {
    if (status.state == JOB_PAUSED) {
        status.state = JOB_RUNNING;
        logInfo("[JOB] Resumed");
        motionResume();
    }
}

void JobManager::abortJob() {
    if (file_open) jobFile.close();
    file_open = false;
    status.state = JOB_IDLE;
    motionBuffer.clear(); 
    
    motionStop(); // FIX: Now compiles because motion.h is included
    
    logWarning("[JOB] Aborted");
}

job_status_t JobManager::getStatus() { return status; }
bool JobManager::isRunning() { return status.state == JOB_RUNNING; }
