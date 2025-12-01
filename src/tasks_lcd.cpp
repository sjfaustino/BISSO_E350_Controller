#include "task_manager.h"
#include "lcd_interface.h"
#include "motion.h"
#include "safety.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "safety_state_machine.h"
#include "fault_logging.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

// The external task function body
void taskLcdFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[LCD_TASK] Started on core 1");
  watchdogTaskAdd("LCD");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "LCD");

  while (1) {
    uint32_t task_start = millis();
    
    // 1. Read Current Encoder Positions 
    int32_t x_pos = motionGetPosition(0); 
    int32_t y_pos = motionGetPosition(1);
    int32_t z_pos = motionGetPosition(2);
    int32_t a_pos = motionGetPosition(3);
    
    // 2. Update Axes Display (Line 1 & 2 - 10 Hz positional data)
    lcdInterfacePrintAxes(x_pos, y_pos, z_pos, a_pos);
    
    // 3. Display Status Messages (Line 3 & 4 - System state/Faults)
    safety_fsm_state_t fsm_state = safetyGetState();
    safety_fault_t current_fault_code = safetyGetCurrentFault();

    if (fsm_state == FSM_EMERGENCY || fsm_state == FSM_ALARM) {
        // FIX: Explicitly cast safety_fault_t to fault_code_t
        lcdInterfacePrintLine(2, "ALARM: MOTION HALTED");
        lcdInterfacePrintLine(3, faultCodeToString((fault_code_t)current_fault_code)); 
    } else if (motionIsMoving()) {
        lcdInterfacePrintLine(2, "STATUS: EXECUTING");
        lcdInterfacePrintLine(3, motionStateToString(motionGetState(0)));
    } else {
        lcdInterfacePrintLine(2, "STATUS: IDLE");
        lcdInterfacePrintLine(3, "INFO: System Ready");
    }

    // 4. Push Updated Display Content to I2C Hardware
    lcdInterfaceUpdate(); 
    
    // Update stats
    
    watchdogFeed("LCD");
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_LCD));
  }
}