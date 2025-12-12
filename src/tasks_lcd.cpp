/**
 * @file tasks_lcd.cpp
 * @brief LCD Update Task
 */

#include "task_manager.h"
#include "lcd_interface.h"
#include "motion.h"
#include "motion_state.h" // <-- CRITICAL FIX: Provides accessors
#include "safety.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "safety_state_machine.h"
#include "fault_logging.h"
#include "firmware_version.h"
// PHASE 3.1: Operator awareness improvements
#include "encoder_deviation.h"  // For encoder health status
#include "plc_iface.h"          // For speed profile display
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

void taskLcdFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[LCD_TASK] [OK] Started on core 1");
  watchdogTaskAdd("LCD");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "LCD");
  
  char ver_str[FIRMWARE_VERSION_STRING_LEN];
  firmwareGetVersionString(ver_str, sizeof(ver_str));
  
  lcdInterfacePrintLine(2, ver_str);
  lcdInterfacePrintLine(3, "System Initializing");
  lcdInterfaceUpdate();

  while (1) {
    // Now visible via motion_state.h
    int32_t x_pos = motionGetPosition(0); 
    int32_t y_pos = motionGetPosition(1);
    int32_t z_pos = motionGetPosition(2);
    int32_t a_pos = motionGetPosition(3);
    
    lcdInterfacePrintAxes(x_pos, y_pos, z_pos, a_pos);
    
    safety_fsm_state_t fsm_state = safetyGetState();
    safety_fault_t current_fault_code = safetyGetCurrentFault();

    // PHASE 3.1: Get speed profile and encoder health for display
    uint8_t speed_profile = elboGetSpeedProfile();
    char speed_char = (speed_profile <= 2) ? ('1' + speed_profile) : '?';

    // Check encoder health across all axes
    char enc_status[4] = "OK";
    for (int axis = 0; axis < MOTION_AXES; axis++) {
      const encoder_deviation_t* dev = encoderGetDeviationData(axis);
      if (dev && dev->status == DEVIATION_WARNING) {
        snprintf(enc_status, sizeof(enc_status), "WN");
        break;
      } else if (dev && dev->status == DEVIATION_ERROR) {
        snprintf(enc_status, sizeof(enc_status), "ER");
        break;
      }
    }

    if (fsm_state == FSM_EMERGENCY || fsm_state == FSM_ALARM) {
        lcdInterfacePrintLine(2, "ALARM: MOTION HALTED");
        // Fault code display with error status
        char fault_line[21];
        snprintf(fault_line, 21, "F#%02X %s", current_fault_code, faultCodeToString((fault_code_t)current_fault_code));
        lcdInterfacePrintLine(3, fault_line);
    } else if (motionIsMoving()) {
        // Line 2: Speed profile + Status + Encoder health
        char status_line[21];
        snprintf(status_line, 21, "SPD[%c] E[%s]", speed_char, enc_status);
        lcdInterfacePrintLine(2, status_line);

        // Line 3: Motion state
        char motion_line[21];
        snprintf(motion_line, 21, "EXEC: %s", motionStateToString(motionGetState(0)));
        lcdInterfacePrintLine(3, motion_line);
    } else {
        // Line 2: Speed profile + Status + Encoder health
        char status_line[21];
        snprintf(status_line, 21, "SPD[%c] E[%s]", speed_char, enc_status);
        lcdInterfacePrintLine(2, status_line);

        // Line 3: Ready message
        lcdInterfacePrintLine(3, "IDLE: System Ready");
    }

    lcdInterfaceUpdate(); 
    watchdogFeed("LCD");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_LCD));
  }
}