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

// Debounce helpers
static uint32_t last_pause_press = 0;
static uint32_t last_resume_press = 0;
#define BUTTON_DEBOUNCE_MS 200

void taskSafetyFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[SAFETY_TASK] Started on core 1");
  watchdogTaskAdd("Safety");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Safety");
  
  // FIX: Protected Initialization
  // We must acquire the I2C mutex before initializing the board inputs
  // to prevent collision with other I2C tasks starting up.
  if (taskLockMutex(taskGetI2cMutex(), 100)) {
      boardInputsInit();
      taskUnlockMutex(taskGetI2cMutex());
  } else {
      logError("[SAFETY] Failed to acquire I2C mutex for init!");
      faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, 0, "Safety Task Init Mutex Timeout");
  }

  while (1) {
    uint32_t now = millis();
    
    // 1. Critical Safety Operations (Internal logic - no I2C)
    safetyUpdate();
    
    // 2. Poll Physical Buttons (Safety/UI) - PROTECTED WITH MUTEX
    // We use a short timeout (2ms) because this is a high-freq task (5ms period).
    // If we can't get the bus immediately (e.g. PLC task has it), we skip this 
    // SINGLE poll and try again in the next 5ms cycle. This prevents delaying 
    // the critical safety loop.
    if (taskLockMutex(taskGetI2cMutex(), 2)) {
        
        button_state_t btns = boardInputsUpdate();
        taskUnlockMutex(taskGetI2cMutex()); // Release immediately after read
        
        if (btns.connection_ok) {
            // --- E-STOP (Highest Priority) ---
            if (btns.estop_active) {
                // Check if we are already in E-Stop to avoid spamming logs
                if (!emergencyStopIsActive()) {
                    faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 1, "Physical E-STOP Button Pressed");
                    emergencyStopSetActive(true); // Triggers motionEmergencyStop internally
                }
            } 
            
            // --- PAUSE ---
            if (btns.pause_pressed && !btns.estop_active) {
                if (now - last_pause_press > BUTTON_DEBOUNCE_MS) {
                    if (motionIsMoving()) { // Only pause if moving
                        logInfo("[SAFETY] Physical PAUSE button pressed");
                        motionPause();
                    }
                    last_pause_press = now;
                }
            }
            
            // --- RESUME ---
            if (btns.resume_pressed && !btns.estop_active) {
                if (now - last_resume_press > BUTTON_DEBOUNCE_MS) {
                    // Only try to resume if not alarmed
                    if (!safetyIsAlarmed()) {
                        logInfo("[SAFETY] Physical RESUME button pressed");
                        motionResume();
                    }
                    last_resume_press = now;
                }
            }
        } else {
            // Critical: Can't read inputs
            // Throttle this error log to avoid flooding the fault history
            static uint32_t last_io_err = 0;
            if (now - last_io_err > 5000) {
                faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, BOARD_INPUT_I2C_ADDR, 
                              "Failed to read Safety Inputs (Pause/Resume/Estop)");
                last_io_err = now;
            }
        }
    } 
    // Else: Mutex contention. Skip this button poll cycle.

    // 3. Check for Queued Messages
    queue_message_t msg;
    while (taskReceiveMessage(taskGetSafetyQueue(), &msg, 0)) {
      switch (msg.type) {
        case MSG_SAFETY_ESTOP_REQUESTED:
          faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 0, 
                        "E-STOP requested via Task Queue (Source: %lu)", msg.param1);
          emergencyStopSetActive(true);
          break;
        default:
          break;
      }
    }
    
    watchdogFeed("Safety");
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_SAFETY));
  }
}