/**
 * @file lcd_formatter.cpp
 * @brief LCD String Formatter Implementation (PHASE 5.4)
 * @details Formats all LCD strings on Core 0 in background to reduce Core 1 load
 */

#include "lcd_formatter.h"
#include "motion.h"
#include "motion_state.h"
#include "safety.h"
#include "safety_state_machine.h"
#include "encoder_deviation.h"
#include "plc_iface.h"
#include "lcd_message.h"
#include "calibration.h"
#include "spindle_current_monitor.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Static buffer to hold pre-formatted LCD strings
static lcd_format_buffer_t format_buffer = {
    .line0 = "",
    .line1 = "",
    .line2 = "",
    .line3 = "",
    .last_update_ms = 0
};

// Mutex to protect the buffer from concurrent access
static SemaphoreHandle_t format_mutex = NULL;

void lcdFormatterInit() {
    format_mutex = xSemaphoreCreateMutex();
    if (!format_mutex) {
        logError("[LCD_FORMATTER] Failed to create mutex");
    }
    memset(&format_buffer, 0, sizeof(format_buffer));
    logInfo("[LCD_FORMATTER] [OK] Initialized on Core 0");
}

void lcdFormatterUpdate() {
    // Read motion state (avoid extensive I/O here)
    int32_t x_pos = motionGetPosition(0);
    int32_t y_pos = motionGetPosition(1);
    int32_t z_pos = motionGetPosition(2);
    int32_t a_pos = motionGetPosition(3);

    float x_mm = motionGetPositionMM(0);
    float y_mm = motionGetPositionMM(1);
    float z_mm = motionGetPositionMM(2);

    safety_fsm_state_t fsm_state = safetyGetState();
    safety_fault_t current_fault_code = safetyGetCurrentFault();

    // Get speed profile and encoder health
    uint8_t speed_profile = elboGetSpeedProfile();
    char speed_char = (speed_profile <= 2) ? ('1' + speed_profile) : '?';

    // Check encoder health
    char enc_status[4] = "OK";
    for (int axis = 0; axis < 4; axis++) {
        const encoder_deviation_t* dev = encoderGetDeviationData(axis);
        if (dev && dev->status == DEVIATION_WARNING) {
            snprintf(enc_status, sizeof(enc_status), "WN");
            break;
        } else if (dev && dev->status == DEVIATION_ERROR) {
            snprintf(enc_status, sizeof(enc_status), "ER");
            break;
        }
    }

    // Check for custom LCD message
    lcd_message_t custom_msg;
    bool has_custom_msg = lcdMessageGet(&custom_msg);

    // PHASE 5.4: Acquire mutex before writing to shared buffer
    lcd_format_buffer_t temp_buffer;
    memset(&temp_buffer, 0, sizeof(temp_buffer));

    // Format line 0: Axis positions
    snprintf(temp_buffer.line0, 21, "X:%6.1f Y:%6.1f", x_mm, y_mm);

    // Format line 1: Status or second position line
    if (motionIsMoving()) {
        uint8_t active_axis = motionGetActiveAxis();
        float current_mm = motionGetPositionMM(active_axis);
        int32_t target_counts = motionGetTarget(active_axis);

        float target_mm = 0.0f;
        if (active_axis == 0) target_mm = (float)target_counts / (machineCal.X.pulses_per_mm > 0 ? machineCal.X.pulses_per_mm : 1000);
        else if (active_axis == 1) target_mm = (float)target_counts / (machineCal.Y.pulses_per_mm > 0 ? machineCal.Y.pulses_per_mm : 1000);
        else if (active_axis == 2) target_mm = (float)target_counts / (machineCal.Z.pulses_per_mm > 0 ? machineCal.Z.pulses_per_mm : 1000);
        else if (active_axis == 3) target_mm = (float)target_counts / (machineCal.A.pulses_per_degree > 0 ? machineCal.A.pulses_per_degree : 1000);

        const char* axis_name = (active_axis <= 3) ? "XYZA"[active_axis] + 0 : '?';
        snprintf(temp_buffer.line1, 21, "Z:%6.1f A:%6.1f", z_mm, 0.0f);
    } else {
        snprintf(temp_buffer.line1, 21, "Z:%6.1f A:%6.1f", z_mm, 0.0f);
    }

    // Format line 2 & 3: Alarm, message, or status
    if (fsm_state == FSM_EMERGENCY || fsm_state == FSM_ALARM) {
        snprintf(temp_buffer.line2, 21, "ALARM: HALTED");
        snprintf(temp_buffer.line3, 21, "F#%02X", current_fault_code);
    } else if (has_custom_msg) {
        snprintf(temp_buffer.line2, 21, "M117:");
        snprintf(temp_buffer.line3, 21, "%.19s", custom_msg.text);
    } else if (motionIsMoving()) {
        snprintf(temp_buffer.line2, 21, "MOVING S:%c", speed_char);
        snprintf(temp_buffer.line3, 21, "ENC:%s", enc_status);
    } else {
        snprintf(temp_buffer.line2, 21, "READY S:%c", speed_char);
        snprintf(temp_buffer.line3, 21, "ENC:%s", enc_status);
    }

    temp_buffer.last_update_ms = millis();

    // PHASE 5.4: Safely update the shared buffer
    if (format_mutex && xSemaphoreTake(format_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&format_buffer, &temp_buffer, sizeof(lcd_format_buffer_t));
        xSemaphoreGive(format_mutex);
    }
}

const lcd_format_buffer_t* lcdFormatterGetBuffer() {
    // Read-only access, but still should be protected
    // The LCD task calls this frequently, so we use a short timeout
    static lcd_format_buffer_t cached_buffer;

    if (format_mutex && xSemaphoreTake(format_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        memcpy(&cached_buffer, &format_buffer, sizeof(lcd_format_buffer_t));
        xSemaphoreGive(format_mutex);
    }

    return &cached_buffer;
}
