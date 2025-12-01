#include "task_manager.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// The external task function body
void taskFaultLogFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[FAULT_TASK] Started on core 0");
  watchdogTaskAdd("Fault_Log");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Fault_Log");

  while (1) {
    uint32_t task_start = millis();
    
    // Fault logging (500ms = 2 Hz)
    queue_message_t msg;
    while (taskReceiveMessage(taskGetFaultQueue(), &msg, 0)) {
      switch (msg.type) {
        case MSG_FAULT_LOGGED:
          // Placeholder for processing logic to write fault message to NVS
          break;
        case MSG_FAULT_CRITICAL:
          logError("[FAULT_TASK] CRITICAL fault logged!");
          break;
        default:
          break;
      }
    }
    
    // Update stats
    
    watchdogFeed("Fault_Log");
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_FAULT_LOG));
  }
}