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

    if (fsm_state == FSM_EMERGENCY || fsm_state == FSM_ALARM) {
        lcdInterfacePrintLine(2, "ALARM: MOTION HALTED");
        lcdInterfacePrintLine(3, faultCodeToString((fault_code_t)current_fault_code)); 
    } else if (motionIsMoving()) {
        lcdInterfacePrintLine(2, "STATUS: EXECUTING");
        lcdInterfacePrintLine(3, motionStateToString(motionGetState(0)));
    } else {
        lcdInterfacePrintLine(2, "STATUS: IDLE");
        lcdInterfacePrintLine(3, "INFO: System Ready");
    }

    lcdInterfaceUpdate(); 
    watchdogFeed("LCD");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_LCD));
  }
}