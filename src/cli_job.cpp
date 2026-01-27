#include "cli.h"
#include "job_manager.h"
#include "serial_logger.h"

void cmd_job_start(int argc, char** argv) {
    if (argc < 2) {
        CLI_USAGE("job_start", "<filename>");
        return;
    }
    if (jobManager.startJob(argv[1])) {
        logInfo("[CLI] Job Started");
    }
}

void cmd_job_abort(int argc, char** argv) {
    jobManager.abortJob();
    logInfo("[CLI] Job Aborted");
}

void cmd_job_status(int argc, char** argv) {
    job_status_t s = jobManager.getStatus();
    logPrintf("Job: %s\nState: %d\nLine: %lu\n", s.filename, s.state, (unsigned long)s.current_line);
}

// C3: Job ETA command
void cmd_job_eta(int argc, char** argv) {
    (void)argc; (void)argv;
    job_status_t s = jobManager.getStatus();
    
    if (s.state != JOB_RUNNING) {
        logPrintln("[JOB] No job running");
        return;
    }
    
    uint32_t elapsed_ms = millis() - s.start_time;
    float progress = 0.0f;
    
    if (s.total_lines > 0 && s.current_line > 0) {
        progress = (float)s.current_line / (float)s.total_lines;
        
        // Calculate ETA
        if (progress > 0.01f) {
            float total_estimated_ms = elapsed_ms / progress;
            float remaining_ms = total_estimated_ms - elapsed_ms;
            
            uint32_t remaining_sec = (uint32_t)(remaining_ms / 1000);
            uint32_t remaining_min = remaining_sec / 60;
            remaining_sec = remaining_sec % 60;
            
            logPrintln("\n[JOB] === Job Progress ===");
            logPrintf("File:      %s\n", s.filename);
            logPrintf("Progress:  %lu / %lu lines (%.1f%%)\n", 
                     (unsigned long)s.current_line, 
                     (unsigned long)s.total_lines,
                     progress * 100.0f);
            logPrintf("Elapsed:   %lu sec\n", (unsigned long)(elapsed_ms / 1000));
            logPrintf("ETA:       %lu min %lu sec\n", (unsigned long)remaining_min, (unsigned long)remaining_sec);
            
            // Progress bar
            char bar[22];
            int filled = (int)(progress * 20);
            for (int i = 0; i < 20; i++) bar[i] = (i < filled) ? '#' : '-';
            bar[20] = '\0';
            logPrintf("           [%s]\n", bar);
        } else {
            logPrintln("[JOB] Calculating ETA...");
        }
    } else {
        logPrintln("[JOB] Total lines unknown - ETA unavailable");
        logPrintf("Current line: %lu\n", (unsigned long)s.current_line);
    }
}

void cliRegisterJobCommands() {
    cliRegisterCommand("job_start", "Start G-Code Job", cmd_job_start);
    cliRegisterCommand("job_abort", "Abort Job", cmd_job_abort);
    cliRegisterCommand("job_status", "Job Status", cmd_job_status);
    cliRegisterCommand("job_eta", "Show job progress and ETA", cmd_job_eta);
}

