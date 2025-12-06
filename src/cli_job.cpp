#include "cli.h"
#include "job_manager.h"
#include "serial_logger.h"

void cmd_job_start(int argc, char** argv) {
    if (argc < 2) {
        Serial.println("Usage: job start <filename>");
        return;
    }
    if (jobManager.startJob(argv[1])) {
        Serial.println("[CLI] Job Started");
    }
}

void cmd_job_abort(int argc, char** argv) {
    jobManager.abortJob();
    Serial.println("[CLI] Job Aborted");
}

void cmd_job_status(int argc, char** argv) {
    job_status_t s = jobManager.getStatus();
    // FIX: Cast s.current_line to unsigned long to match %lu format specifier
    Serial.printf("Job: %s\nState: %d\nLine: %lu\n", s.filename, s.state, (unsigned long)s.current_line);
}

void cliRegisterJobCommands() {
    cliRegisterCommand("job_start", "Start G-Code Job", cmd_job_start);
    cliRegisterCommand("job_abort", "Abort Job", cmd_job_abort);
    cliRegisterCommand("job_status", "Job Status", cmd_job_status);
}