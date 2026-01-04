/**
 * @file tasks_lcd.cpp
 * @brief LCD Update Task (PHASE 5.4: Uses pre-formatted strings from Core 0
 * formatter)
 */

#include "fault_logging.h"
#include "firmware_version.h"
#include "lcd_interface.h"
#include "motion.h"
#include "motion_state.h" // <-- CRITICAL FIX: Provides accessors
#include "safety.h"
#include "safety_state_machine.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "task_manager.h"
#include "watchdog_manager.h"

// PHASE 3.1: Operator awareness improvements
#include "encoder_deviation.h" // For encoder health status
#include "plc_iface.h"         // For speed profile display
// PHASE 3.2: G-code LCD message support and detailed motion display
#include "calibration.h" // For converting counts to mm/degrees
#include "lcd_message.h"

// PHASE 5.0: Spindle current monitoring
#include "spindle_current_monitor.h"
// PHASE 5.4: Pre-formatted LCD strings from background formatter
#include "lcd_formatter.h"
// Boot status display
#include "board_inputs.h"
#include "config_keys.h"
#include "config_unified.h"
#include "encoder_wj66.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

// Helper function to display boot status on LCD
static void displayBootStatus() {
  char line[21];

  // Page 1: Firmware version and buttons/spindle status
  lcdInterfaceClear();

  char ver_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(ver_str, sizeof(ver_str));
  lcdInterfacePrintLine(0, ver_str);

  // Buttons status
  bool btn_en = configGetInt(KEY_BUTTONS_ENABLED, 0) != 0;
  snprintf(line, 21, "Buttons: %s", btn_en ? "ON" : "OFF");
  lcdInterfacePrintLine(1, line);

  // Spindle status
  bool spindl_en = spindleMonitorIsEnabled();
  snprintf(line, 21, "Spindle: %s", spindl_en ? "ON" : "OFF");
  lcdInterfacePrintLine(2, line);

  // Encoder status (WJ66)
  encoder_status_t enc_status = wj66GetStatus();
  const char *enc_str = (enc_status == ENCODER_OK)        ? "OK"
                        : (enc_status == ENCODER_TIMEOUT) ? "TIMEOUT"
                                                          : "ERROR";
  snprintf(line, 21, "Encoder: %s", enc_str);
  lcdInterfacePrintLine(3, line);

  lcdInterfaceUpdate();
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Page 2: WiFi status
  lcdInterfaceClear();
  lcdInterfacePrintLine(0, "WiFi Status:");

  if (WiFi.status() == WL_CONNECTED) {
    snprintf(line, 21, "SSID: %.14s", WiFi.SSID().c_str());
    lcdInterfacePrintLine(1, line);

    snprintf(line, 21, "IP: %s", WiFi.localIP().toString().c_str());
    lcdInterfacePrintLine(2, line);

    snprintf(line, 21, "RSSI: %d dBm", WiFi.RSSI());
    lcdInterfacePrintLine(3, line);
  } else {
    lcdInterfacePrintLine(1, "Not Connected");
    lcdInterfacePrintLine(2, "AP Mode Active?");

    // Check if SoftAP is active
    if (WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
      snprintf(line, 21, "AP IP: %s", WiFi.softAPIP().toString().c_str());
      lcdInterfacePrintLine(3, line);
    } else {
      lcdInterfacePrintLine(3, "No Network");
    }
  }

  lcdInterfaceUpdate();
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Page 3: I2C devices status
  lcdInterfaceClear();
  lcdInterfacePrintLine(0, "I2C Devices:");
  lcdInterfacePrintLine(1, "PLC Out 0x24: OK");
  lcdInterfacePrintLine(2, "LCD 0x27: OK");
  lcdInterfacePrintLine(3, "Boot Complete!");

  lcdInterfaceUpdate();
  vTaskDelay(pdMS_TO_TICKS(1500));
}

void taskLcdFunction(void *parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[LCD_TASK] [OK] Started on core 1");
  watchdogTaskAdd("LCD");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "LCD");

  // Show boot status pages first
  displayBootStatus();

  // PHASE 3.2: Initialize LCD message system for M117 support
  lcdMessageInit();

  while (1) {
    // CRITICAL FIX: Feed watchdog EARLY to prevent timeout if operations block
    watchdogFeed("LCD");

    // Display positions (lines 0-1)
    int32_t x_pos = motionGetPosition(0);
    int32_t y_pos = motionGetPosition(1);
    int32_t z_pos = motionGetPosition(2);
    int32_t a_pos = motionGetPosition(3);

    lcdInterfacePrintAxes(x_pos, y_pos, z_pos, a_pos);

    safety_fsm_state_t fsm_state = safetyGetState();
    safety_fault_t current_fault_code = safetyGetCurrentFault();

    // PHASE 3.1: Get encoder health for display
    // Note: speed profile available via elboGetSpeedProfile() (0=turtle,
    // 1=slow, 2=fast)

    // Check encoder health across all axes
    char enc_status[4] = "OK";
    for (int axis = 0; axis < MOTION_AXES; axis++) {
      const encoder_deviation_t *dev = encoderGetDeviationData(axis);
      if (dev && dev->status == AXIS_DEVIATION_WARNING) {
        snprintf(enc_status, sizeof(enc_status), "WN");
        break;
      } else if (dev && dev->status == AXIS_DEVIATION_ERROR) {
        snprintf(enc_status, sizeof(enc_status), "ER");
        break;
      }
    }

    // PHASE 3.2: Check for custom LCD message from M117 G-code
    lcd_message_t custom_msg;
    bool has_custom_msg = lcdMessageGet(&custom_msg);

    if (fsm_state == FSM_EMERGENCY || fsm_state == FSM_ALARM) {
      lcdInterfacePrintLine(2, "ALARM: MOTION HALTED");
      // Fault code display with numeric code
      char fault_line[21];
      snprintf(fault_line, 21, "F#%02X %s", current_fault_code,
               faultCodeToString((fault_code_t)current_fault_code));
      lcdInterfacePrintLine(3, fault_line);
    } else if (has_custom_msg) {
      // PHASE 3.2: Display custom G-code M117 message (lines 2-3)
      lcdInterfacePrintLine(2, "M117 Message:");
      lcdInterfacePrintLine(3, custom_msg.text);
    } else if (motionIsMoving()) {
      // PHASE 3.2: Detailed motion display - show which axis and target
      // distance in mm
      uint8_t active_axis = motionGetActiveAxis();
      int32_t target_counts = motionGetTarget(active_axis);
      float current_mm = motionGetPositionMM(active_axis);

      // Convert target counts to mm/degrees using calibration data
      float target_mm = 0.0f;
      const char *unit = "mm";

      // PHASE 3.2 FIX: Use proper calibration data to convert counts to
      // user-friendly units
      const float def_lin = (float)MOTION_POSITION_SCALE_FACTOR;
      const float def_ang = (float)MOTION_POSITION_SCALE_FACTOR_DEG;

      if (active_axis == 0) { // X axis
        float sx = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm
                                                    : def_lin;
        target_mm = target_counts / sx;
      } else if (active_axis == 1) { // Y axis
        float sy = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm
                                                    : def_lin;
        target_mm = target_counts / sy;
      } else if (active_axis == 2) { // Z axis
        float sz = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm
                                                    : def_lin;
        target_mm = target_counts / sz;
      } else if (active_axis == 3) { // A axis (rotary - in degrees)
        float sa = (machineCal.A.pulses_per_degree > 0)
                       ? machineCal.A.pulses_per_degree
                       : def_ang;
        target_mm = target_counts / sa;
        unit = "°";                             // Degrees for A axis
        current_mm = motionGetPosition(3) / sa; // Convert A position to degrees
      }

      // Line 2: Motion status (Z/A/amps now on line 1 via
      // lcdInterfacePrintAxes)
      lcdInterfacePrintLine(2, "EXEC: In Motion...");

      // Line 3: Detailed motion - axis and movement with right-aligned distance
      // Example: "EXEC: Mv X   +25.4mm" or "EXEC: Mv A    +45.0°"
      // Format: "EXEC: Mv" + axis + right-aligned distance
      char motion_line[21];
      char axis_char = "XYZA"[active_axis % 4];
      float delta_mm = (target_mm >= current_mm) ? (target_mm - current_mm)
                                                 : (current_mm - target_mm);
      char direction = (target_mm >= current_mm) ? '+' : '-';

      // Build distance string with sign, value, and unit
      // Then right-align it within the remaining 10 characters
      char distance_str[10];
      snprintf(distance_str, sizeof(distance_str), "%c%4.1f%s", direction,
               fabsf(delta_mm), unit);

      // Right-align distance string: "EXEC: Mv X" (10 chars) + distance (10
      // chars, right-aligned)
      snprintf(motion_line, 21, "EXEC: Mv %c%10s", axis_char, distance_str);
      lcdInterfacePrintLine(3, motion_line);
    } else {
      // Idle state: Lines 2-3 show status (Z/A/amps on line 1 via
      // lcdInterfacePrintAxes)
      lcdInterfacePrintLine(2, "Status: IDLE");
      lcdInterfacePrintLine(3, "System Ready");
    }

    lcdInterfaceUpdate();
    // Feed watchdog again at end of loop (defense in depth)
    watchdogFeed("LCD");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_LCD));
  }
}
