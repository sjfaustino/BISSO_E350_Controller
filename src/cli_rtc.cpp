/**
 * @file cli_rtc.cpp
 * @brief RTC CLI Commands for KC868-A16 v3.1
 * @project BISSO E350 Controller
 */

#include "cli.h"
#include "serial_logger.h"
#include "board_variant.h"
#include "rtc_manager.h"
#include <stdlib.h>

#if BOARD_HAS_RTC_DS3231

// =============================================================================
// RTC STATUS
// =============================================================================

void cmd_rtc_status(int argc, char** argv) {
    (void)argc; (void)argv;
    
    logPrintln("\n[RTC] === DS3231 RTC Status ===");
    
    if (!rtcIsAvailable()) {
        logPrintln("  Status: NOT AVAILABLE");
        logPrintln("");
        return;
    }
    
    logPrintln("  Status:      Available");
    
    char dateStr[16], timeStr[16];
    rtcGetDateString(dateStr, sizeof(dateStr));
    rtcGetTimeString(timeStr, sizeof(timeStr));
    
    logPrintf("  Date:        %s\n", dateStr);
    logPrintf("  Time:        %s\n", timeStr);
    logPrintf("  Temperature: %.1f C\n", rtcGetTemperature());
    logPrintln("");
}

// =============================================================================
// GET DATE/TIME
// =============================================================================

void cmd_rtc_get(int argc, char** argv) {
    (void)argc; (void)argv;
    
    if (!rtcIsAvailable()) {
        logError("[RTC] RTC not available");
        return;
    }
    
    int y, m, d, h, min, s;
    if (rtcGetDateTime(&y, &m, &d, &h, &min, &s)) {
        logPrintf("[RTC] %04d-%02d-%02d %02d:%02d:%02d\n", y, m, d, h, min, s);
    } else {
        logError("[RTC] Failed to read time");
    }
}

// =============================================================================
// SET DATE
// =============================================================================

void cmd_rtc_date(int argc, char** argv) {
    if (argc < 3) {
        logError("[RTC] Usage: rtc date YYYY-MM-DD");
        logInfo("[RTC] Example: rtc date 2026-02-05");
        return;
    }
    
    if (!rtcIsAvailable()) {
        logError("[RTC] RTC not available");
        return;
    }
    
    // Parse YYYY-MM-DD format
    int y, m, d;
    if (sscanf(argv[2], "%d-%d-%d", &y, &m, &d) != 3) {
        logError("[RTC] Invalid format. Use: YYYY-MM-DD");
        return;
    }
    
    // Keep current time, just update date
    int cy, cm, cd, ch, cmin, cs;
    if (!rtcGetDateTime(&cy, &cm, &cd, &ch, &cmin, &cs)) {
        logError("[RTC] Failed to read current time");
        return;
    }
    
    if (rtcSetDateTime(y, m, d, ch, cmin, cs)) {
        logInfo("[RTC] [OK] Date set to: %04d-%02d-%02d", y, m, d);
    } else {
        logError("[RTC] Failed to set date");
    }
}

// =============================================================================
// SET TIME
// =============================================================================

void cmd_rtc_time(int argc, char** argv) {
    if (argc < 3) {
        logError("[RTC] Usage: rtc time HH:MM:SS");
        logInfo("[RTC] Example: rtc time 14:30:00");
        return;
    }
    
    if (!rtcIsAvailable()) {
        logError("[RTC] RTC not available");
        return;
    }
    
    // Parse HH:MM:SS format
    int h, m, s;
    if (sscanf(argv[2], "%d:%d:%d", &h, &m, &s) != 3) {
        // Try HH:MM format
        s = 0;
        if (sscanf(argv[2], "%d:%d", &h, &m) != 2) {
            logError("[RTC] Invalid format. Use: HH:MM:SS or HH:MM");
            return;
        }
    }
    
    // Keep current date, just update time
    int cy, cm, cd, ch, cmin, cs;
    if (!rtcGetDateTime(&cy, &cm, &cd, &ch, &cmin, &cs)) {
        logError("[RTC] Failed to read current date");
        return;
    }
    
    if (rtcSetDateTime(cy, cm, cd, h, m, s)) {
        logInfo("[RTC] [OK] Time set to: %02d:%02d:%02d", h, m, s);
    } else {
        logError("[RTC] Failed to set time");
    }
}

// =============================================================================
// SET BOTH DATE AND TIME
// =============================================================================

void cmd_rtc_set(int argc, char** argv) {
    if (argc < 4) {
        logError("[RTC] Usage: rtc set YYYY-MM-DD HH:MM:SS");
        logInfo("[RTC] Example: rtc set 2026-02-05 18:54:00");
        return;
    }
    
    if (!rtcIsAvailable()) {
        logError("[RTC] RTC not available");
        return;
    }
    
    // Parse date and time
    int y, mo, d, h, mi, s;
    if (sscanf(argv[2], "%d-%d-%d", &y, &mo, &d) != 3) {
        logError("[RTC] Invalid date format. Use: YYYY-MM-DD");
        return;
    }
    
    if (sscanf(argv[3], "%d:%d:%d", &h, &mi, &s) != 3) {
        s = 0;
        if (sscanf(argv[3], "%d:%d", &h, &mi) != 2) {
            logError("[RTC] Invalid time format. Use: HH:MM:SS or HH:MM");
            return;
        }
    }
    
    if (rtcSetDateTime(y, mo, d, h, mi, s)) {
        logInfo("[RTC] [OK] DateTime set to: %04d-%02d-%02d %02d:%02d:%02d", 
                y, mo, d, h, mi, s);
    } else {
        logError("[RTC] Failed to set date/time");
    }
}

// =============================================================================
// SYNC SYSTEM TIME FROM RTC
// =============================================================================

void cmd_rtc_sync(int argc, char** argv) {
    (void)argc; (void)argv;
    
    if (!rtcIsAvailable()) {
        logError("[RTC] RTC not available");
        return;
    }
    
    rtcSyncSystemTime();
    logInfo("[RTC] [OK] System time synced from RTC");
}

// =============================================================================
// TEMPERATURE
// =============================================================================

void cmd_rtc_temp(int argc, char** argv) {
    (void)argc; (void)argv;
    
    if (!rtcIsAvailable()) {
        logError("[RTC] RTC not available");
        return;
    }
    
    float temp = rtcGetTemperature();
    if (temp > -100) {
        logPrintf("[RTC] Temperature: %.1f C\n", temp);
    } else {
        logError("[RTC] Failed to read temperature");
    }
}

// =============================================================================
// MAIN COMMAND DISPATCHER
// =============================================================================

void cmd_rtc_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"status",  cmd_rtc_status,  "Show RTC status"},
        {"get",     cmd_rtc_get,     "Get current date/time"},
        {"date",    cmd_rtc_date,    "Set date (YYYY-MM-DD)"},
        {"time",    cmd_rtc_time,    "Set time (HH:MM:SS)"},
        {"set",     cmd_rtc_set,     "Set date and time"},
        {"sync",    cmd_rtc_sync,    "Sync system time from RTC"},
        {"temp",    cmd_rtc_temp,    "Get RTC temperature"}
    };
    
    cliDispatchSubcommand("[RTC]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

void cliRegisterRTCCommands() {
    cliRegisterCommand("rtc", "Real-time clock (DS3231)", cmd_rtc_main);
}

#else

// Stub for boards without RTC
void cliRegisterRTCCommands() {
    // No RTC on this board variant
}

#endif // BOARD_HAS_RTC_DS3231
