/**
 * @file cli_diag_motion.cpp
 * @brief Motion-related diagnostic CLI commands
 */

#include "cli.h"
#include "motion.h"
#include "encoder_wj66.h"
#include "encoder_comm_stats.h"
#include "encoder_hal.h"
#include "encoder_motion_integration.h"
#include "encoder_calibration.h"
#include "axis_utilities.h"
#include "axis_synchronization.h"
#include "job_manager.h"
#include "config_unified.h"
#include "config_keys.h"
#include "rtc_manager.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include <Arduino.h>

// Shared CLI utilities provided by cli.h

// Handler for 'encoder status' (extracted from cli_diag.cpp)
void cmd_encoder_status(int argc, char** argv) {
    (void)argc; (void)argv;
    const encoder_hal_config_t* config = encoderHalGetConfig();
    if (!config) return;
    
    if (!serialLoggerLock()) return;

    logDirectPrintln("\n=== Encoder Dashboard (WJ66) ===\n");
    
    // Summary Table
    cliPrintTableHeader(16, 38, 0, 0, 0);
    cliPrintTableRow("Property", "Value", nullptr, 16, 38, 0, nullptr, 0, nullptr, 0);
    cliPrintTableDivider(16, 38, 0, 0, 0);
    
    cliPrintTableRow("Interface", encoderHalGetInterfaceName(config->interface), nullptr, 16, 38, 0, nullptr, 0, nullptr, 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "RX:%d TX:%d", config->rx_pin, config->tx_pin);
    cliPrintTableRow("Pins", buf, nullptr, 16, 38, 0, nullptr, 0, nullptr, 0);
    snprintf(buf, sizeof(buf), "%lu baud", (unsigned long)config->baud_rate);
    cliPrintTableRow("Baud Rate", buf, nullptr, 16, 38, 0, nullptr, 0, nullptr, 0);
    
    int proto = configGetInt(KEY_ENC_PROTO, 0);
    cliPrintTableRow("Protocol", (proto == 1) ? "Modbus RTU" : "ASCII (#XX\\r)", nullptr, 16, 38, 0, nullptr, 0, nullptr, 0);
    snprintf(buf, sizeof(buf), "%d", configGetInt(KEY_ENC_ADDR, 0));
    cliPrintTableRow("Slave ID", buf, nullptr, 16, 38, 0, nullptr, 0, nullptr, 0);
    
    // PHASE 7: Added Latency & Errors to summary
    // Use axis age instead of wall clock for summary
    snprintf(buf, sizeof(buf), "%d ms", (int)wj66GetPollCount() > 0 ? 50 : 0); // Polling interval
    cliPrintTableRow("Poll Interval", buf, nullptr, 16, 38, 0, nullptr, 0, nullptr, 0);
    
    cliPrintTableFooter(16, 38, 0, 0, 0);

    logDirectPrintln("\n=== Axis Real-Time Metrics ===\n");
    cliPrintTableHeader(5, 5, 12, 12, 14);
    cliPrintTableRow("Axis", "Name", "Pos (Pulse)", 5, 5, 12, "Pos (mm)", 12, "Status/Age", 14);
    cliPrintTableDivider(5, 5, 12, 12, 14);

    for (int i = 0; i < 4; i++) {
        int32_t pos = wj66GetPosition(i);
        uint32_t age = wj66GetAxisAge(i);
        bool stale = wj66IsStale(i);
        
        float ppm = motionGetAxisScale(i);
        float pos_mm = (ppm > 0.001f) ? (float)pos / ppm : 0.0f;
        
        char name[2] = {"XYZA"[i], '\0'};
        char pos_pulse_str[16];
        char pos_mm_str[16];
        char status_str[24];
        char axis_idx_str[4];
        
        snprintf(axis_idx_str, sizeof(axis_idx_str), "%d", i);
        snprintf(pos_pulse_str, sizeof(pos_pulse_str), "%ld", (long)pos);
        snprintf(pos_mm_str, sizeof(pos_mm_str), "%.2f", pos_mm);
        
        if (stale) {
            snprintf(status_str, sizeof(status_str), "STALE (%lu ms)", (unsigned long)age);
        } else {
            snprintf(status_str, sizeof(status_str), "OK (%lu ms)", (unsigned long)age);
        }
        
        cliPrintTableRow(axis_idx_str, name, pos_pulse_str, 5, 5, 12, pos_mm_str, 12, status_str, 14);
    }
    cliPrintTableFooter(5, 5, 12, 12, 14);
    
    Serial.flush();
    serialLoggerUnlock();
}

void cmd_encoder_read(int argc, char** argv) {
    int n_reads = 10;
    if (argc >= 3) {
        n_reads = atoi(argv[2]);
        if (n_reads <= 0) n_reads = 1;
    }

    logPrintf("Reading %d times (0.5s interval)...\r\n", n_reads);
    logPrintln("| Axis 0    | Axis 1    | Axis 2    | Axis 3    |");
    logPrintln("+-----------+-----------+-----------+-----------+");

    for (int i = 0; i < n_reads; i++) {
        logPrintf("| %9ld | %9ld | %9ld | %9ld |\r\n",
                  (long)wj66GetPosition(0),
                  (long)wj66GetPosition(1),
                  (long)wj66GetPosition(2),
                  (long)wj66GetPosition(3));
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void cmd_encoder_diag(int argc, char** argv) { (void)argc; (void)argv; encoderMotionDiagnostics(); }
void cmd_encoder_test(int argc, char** argv) { (void)argc; (void)argv; wj66Diagnostics(); }
void cmd_encoder_baud_detect(int argc, char** argv) { (void)argc; (void)argv; wj66Autodetect(); }
void cmd_encoder_deviation_diag(int argc, char** argv) { (void)argc; (void)argv; encoderHalPrintStatus(); }

void cmd_axis_sync_diag(int argc, char** argv) {
    (void)argc; (void)argv;
    axisSynchronizationPrintSummary();
}
