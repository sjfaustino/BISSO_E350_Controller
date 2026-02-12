/**
 * @file sd_telemetry_logger.cpp
 * @brief Background machine state logging to SD card
 */

#include "sd_telemetry_logger.h"
#include <SD.h>
#include "sd_card_manager.h"
#include "system_telemetry.h"
#include "serial_logger.h"
#include "rtc_manager.h"

static File logFile;
static bool logging_active = false;
static uint32_t last_sync_ms = 0;
static char log_filename[32] = {0};

bool sdTelemetryLoggerInit() {
    if (!sdCardIsMounted()) {
        logWarning("[SD_LOG] Cannot start logger: SD not mounted");
        return false;
    }

    // Create logs directory if missing
    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }

    // Generate filename based on timestamp or sequential ID
    uint32_t timestamp = 0;
    #if BOARD_HAS_RTC_DS3231
        timestamp = rtcGetCurrentEpoch();
    #endif

    if (timestamp > 1700000000) { // Valid RTC time
        snprintf(log_filename, sizeof(log_filename), "/logs/diag_%lu.csv", (unsigned long)timestamp);
    } else {
        // Fallback to sequential search
        for (int i = 0; i < 1000; i++) {
            snprintf(log_filename, sizeof(log_filename), "/logs/diag_%03d.csv", i);
            if (!SD.exists(log_filename)) break;
        }
    }

    logFile = SD.open(log_filename, FILE_WRITE);
    if (!logFile) {
        logError("[SD_LOG] Failed to open %s for writing", log_filename);
        return false;
    }

    // Write header
    logFile.println("uptime_sec,health,x_mm,y_mm,z_mm,spindle_a,spindle_rpm,cpu_pct,heap_bytes");
    logFile.flush();
    
    logInfo("[SD_LOG] [OK] Logging started to %s", log_filename);
    logging_active = true;
    return true;
}

void sdTelemetryLoggerUpdate() {
    if (!logging_active || !sdCardIsMounted()) {
        // Try to re-init if SD card was recently mounted
        if (sdCardIsMounted() && !logging_active) {
            sdTelemetryLoggerInit();
        }
        return;
    }

    system_telemetry_t t = telemetryGetSnapshot();

    // Format: CSV
    char line[128];
    snprintf(line, sizeof(line), "%lu,%d,%.3f,%.3f,%.3f,%.2f,%u,%u,%lu",
        (unsigned long)t.uptime_seconds,
        (int)t.health_status,
        t.axis_x_mm, t.axis_y_mm, t.axis_z_mm,
        t.spindle_current_amps, t.spindle_rpm,
        t.cpu_usage_percent,
        (unsigned long)t.free_heap_bytes
    );

    logFile.println(line);

    // Sync every 10 seconds to protect against power loss without killing performance
    if (millis() - last_sync_ms > 10000) {
        logFile.flush();
        last_sync_ms = millis();
        
        // Auto-rotate if file too big (> 2MB)
        if (logFile.size() > 2 * 1024 * 1024) {
            logInfo("[SD_LOG] File too large, rotating...");
            logFile.close();
            logging_active = false;
            sdTelemetryLoggerInit();
        }
    }
}
