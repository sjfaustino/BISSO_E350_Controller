#include "task_manager.h"
#include "motion.h"
#include "serial_logger.h"
#include "task_performance_monitor.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "encoder_deviation.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

void taskMotionFunction(void* parameter) {
  // 100Hz Motion Loop
  const TickType_t xPeriod = pdMS_TO_TICKS(TASK_PERIOD_MOTION);
  TickType_t xLastWakeTime = xTaskGetTickCount();
  
  logInfo("[MOTION_TASK] [OK] Started on core 1");
  watchdogTaskAdd("Motion");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Motion");

  while (1) {
    perfMonitorTaskStart(PERF_TASK_ID_MOTION);
    
    // High-Resolution Jitter Measurement (PHASE 5.5)
    static uint64_t last_wake_us = 0;
    uint64_t now_us = esp_timer_get_time();
    if (last_wake_us != 0) {
        uint64_t interval_us = now_us - last_wake_us;
        uint64_t expected_us = TASK_PERIOD_MOTION * 1000;
        if (interval_us > expected_us) {
            motionTrackJitterUS((uint32_t)(interval_us - expected_us));
        }
    }
    last_wake_us = now_us;

    // Core motion operations
    motionUpdate();
    perfMonitorTaskEnd(PERF_TASK_ID_MOTION);

    // PHASE 2 FIX: Encoder deviation detection
    // Monitor each axis for deviation (stalls, loss of sync, mechanical problems)
    for (int axis = 0; axis < MOTION_AXES; axis++) {
      int32_t expected_pos = motionGetTarget(axis);
      int32_t actual_pos = motionGetPosition(axis);
      float velocity_mm_s = motionGetVelocity(axis); // Get actual velocity from motion state

      encoderDeviationUpdate(axis, expected_pos, actual_pos, velocity_mm_s);
    }

    // Check for encoder alarms and trigger fault recovery if needed
    if (encoderHasDeviationAlarm()) {
      logError("[MOTION_TASK] Encoder deviation alarm detected!");
      // Fault recovery would go here (Phase 2.5)
      motionEmergencyStop();
    }

    // Check for motion commands
    queue_message_t msg;
    while (taskReceiveMessage(taskGetMotionQueue(), &msg, 0)) {
      switch (msg.type) {
        case MSG_MOTION_START: logInfo("[MOTION_TASK] Start received"); break;
        case MSG_MOTION_STOP:  logInfo("[MOTION_TASK] Stop received"); break;
        case MSG_MOTION_EMERGENCY_HALT: logError("[MOTION_TASK] Emergency Halt"); break;
        default: break;
      }
    }

    watchdogFeed("Motion");

    // PHASE 5.7: PLC Watchdog (Ghost Task Optimization)
    // PLC task removed (was doing nothing except feeding watchdog)
    // Motion task now feeds PLC watchdog since Motion uses PLC I/O heavily
    watchdogFeed("PLC");

    watchdogFeed("PLC");

    // Periodic Wait (PHASE 5.5: Fixed jitter logic)
    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}
