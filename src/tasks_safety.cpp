/**
 * @file tasks_safety.cpp
 * @brief Safety Monitor Task
 */

#include "board_inputs.h"
#include "boot_validation.h"
#include "fault_logging.h"
#include "motion.h"
#include "motion_state.h" // <-- CRITICAL FIX: Provides motionIsMoving
#include "safety.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include "task_performance_monitor.h"
#include "task_manager.h"
#include "watchdog_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static uint32_t last_pause_press = 0;
static uint32_t last_resume_press = 0;
#define BUTTON_DEBOUNCE_MS 200

void taskSafetyFunction(void *parameter) {
  TickType_t last_wake = xTaskGetTickCount();

  logInfo("[SAFETY_TASK] [OK] Started on core 1");
  watchdogTaskAdd("Safety");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Safety");

  // PHASE 5.4: Use dedicated board input mutex for initialization
  // PHASE 5.4: Use dedicated board input mutex for initialization
  // NOTE: boardInputsInit() manages its own mutex internally
  boardInputsInit();

  while (1) {
    perfMonitorTaskStart(PERF_TASK_ID_SAFETY);
    uint32_t now = millis();

    // 1. Critical Safety Operations
    safetyUpdate();

    // 2. Poll Physical Buttons
    // PHASE 5.4: Use dedicated board input mutex instead of generic I2C mutex
    // Prevents button polling from blocking PLC communication
    // NOTE: boardInputsUpdate() manages its own mutex internally

    button_state_t btns = boardInputsUpdate();
    elboI73Refresh(); // Ensure auxiliary inputs (Bank 2) are also polled for diagnostics

    if (btns.connection_ok) {
      // E-STOP
      if (btns.estop_active) {
        if (!emergencyStopIsActive()) {
          faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 1,
                        "Physical E-STOP Button Pressed");
          emergencyStopSetActive(true);
        }
      }

      // PAUSE
      if (btns.pause_pressed && !btns.estop_active) {
        if (now - last_pause_press > BUTTON_DEBOUNCE_MS) {
          // Now visible
          if (motionIsMoving()) {
            logInfo("[SAFETY] Physical PAUSE button pressed");

            // PHASE 5.10: Signal event before action
            systemEventsSafetySet(EVENT_SAFETY_PAUSE_PRESSED);

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

            // PHASE 5.10: Signal event before action
            systemEventsSafetySet(EVENT_SAFETY_RESUME_PRESSED);

            motionResume();
          }
          last_resume_press = now;
        }
      }
    } else if (bootIsSubsystemHealthy("Inputs")) { 
      // PHASE 16: Only log periodic errors if the device was actually detected at boot.
      // If the board is known-missing (bare DevKit), stay silent to avoid CLI spam.
      static uint32_t last_io_err = 0;
      if (now - last_io_err > 5000) {
        faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, BOARD_INPUT_I2C_ADDR,
                      "Failed to read Safety Inputs");
        last_io_err = now;
      }
    }

    // 3. Queue Messages
    queue_message_t msg;
    while (taskReceiveMessage(taskGetSafetyQueue(), &msg, 0)) {
      switch (msg.type) {
      case MSG_SAFETY_ESTOP_REQUESTED:
        faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 0,
                      "E-STOP requested via Task Queue");
        emergencyStopSetActive(true);
        break;
      default:
        break;
      }
    }

    watchdogFeed("Safety");
    perfMonitorTaskEnd(PERF_TASK_ID_SAFETY);
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_SAFETY));
  }
}
