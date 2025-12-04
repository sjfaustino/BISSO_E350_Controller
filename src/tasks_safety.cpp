#include "task_manager.h"
#include "safety.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "fault_logging.h"
#include "board_inputs.h" 
#include "motion.h"       
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static uint32_t last_pause_press = 0;
static uint32_t last_resume_press = 0;
#define BUTTON_DEBOUNCE_MS 200

void taskSafetyFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[SAFETY_TASK] [OK] Started on core 1");
  watchdogTaskAdd("Safety");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Safety");
  
  // Protected Initialization
  if (taskLockMutex(taskGetI2cMutex(), 100)) {
      boardInputsInit();
      taskUnlockMutex(taskGetI2cMutex());
  } else {
      logError("[SAFETY] [FAIL] Failed to acquire I2C mutex for init!");
  }

  while (1) {
    uint32_t now = millis();
    
    // 1. Critical Safety Operations (Internal)
    safetyUpdate();
    
    // 2. Poll Physical Buttons (Protected)
    if (taskLockMutex(taskGetI2cMutex(), 2)) {
        
        button_state_t btns = boardInputsUpdate();
        taskUnlockMutex(taskGetI2cMutex()); 
        
        if (btns.connection_ok) {
            // E-STOP
            if (btns.estop_active) {
                if (!emergencyStopIsActive()) {
                    faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 1, "Physical E-STOP Button Pressed");
                    emergencyStopSetActive(true);
                }
            } 
            
            // PAUSE
            if (btns.pause_pressed && !btns.estop_active) {
                if (now - last_pause_press > BUTTON_DEBOUNCE_MS) {
                    if (motionIsMoving()) { 
                        logInfo("[SAFETY] Physical PAUSE button pressed");
                        motionPause();
                    }
                    last_pause_press = now;
                }
            }
            
            // RESUME
            if (btns.resume_pressed && !btns.estop_active) {
                if (now - last_resume_press > BUTTON_DEBOUNCE_MS) {
                    if (!safetyIsAlarmed()) {
                        logInfo("[SAFETY] Physical RESUME button pressed");
                        motionResume();
                    }
                    last_resume_press = now;
                }
            }
        } else {
            // Throttled error logging
            static uint32_t last_io_err = 0;
            if (now - last_io_err > 5000) {
                faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, BOARD_INPUT_I2C_ADDR, "Failed to read Safety Inputs");
                last_io_err = now;
            }
        }
    } 

    // 3. Queue Messages
    queue_message_t msg;
    while (taskReceiveMessage(taskGetSafetyQueue(), &msg, 0)) {
      switch (msg.type) {
        case MSG_SAFETY_ESTOP_REQUESTED:
          faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 0, "E-STOP requested via Task Queue");
          emergencyStopSetActive(true);
          break;
        default: break;
      }
    }
    
    watchdogFeed("Safety");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_SAFETY));
  }
}