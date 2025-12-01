#include "task_manager.h"
#include "safety.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "fault_logging.h"
#include "board_inputs.h" // <-- NEW
#include "motion.h"       // Need access to Pause/Resume
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
  
  // Init Board Inputs
  boardInputsInit();

  while (1) {
    uint32_t now = millis();
    
    // 1. Critical Safety Operations (Internal logic)
    safetyUpdate();
    
    // 2. Poll Physical Buttons (Safety/UI)
    button_state_t btns = boardInputsUpdate();
    
    if (btns.connection_ok) {
        // --- E-STOP (Highest Priority) ---
        if (btns.estop_active) {
            // Check if we are already in E-Stop to avoid spamming logs
            if (!emergencyStopIsActive()) {
                faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 1, "Physical E-STOP Button Pressed");
                emergencyStopSetActive(true); // Triggers motionEmergencyStop internally
            }
        } 
        // Logic to allow clearing? 
        // Prompt said: "does not accept anymore commands... until manually disarmed"
        // We assume disarmed means button released AND user acknowledges via reset command.
        // We do NOT auto-clear via code here, we only enforce the active state.
        
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
        // Throttle this error log
        static uint32_t last_io_err = 0;
        if (now - last_io_err > 5000) {
            faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, BOARD_INPUT_I2C_ADDR, 
                          "Failed to read Safety Inputs (Pause/Resume/Estop)");
            last_io_err = now;
        }
    }
    
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