#include "task_manager.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskFaultLogFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  logInfo("[FAULT_TASK] Started on core 0");
  watchdogTaskAdd("Fault_Log");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Fault_Log");

  while (1) {
    // Process queue with a timeout (wait for message)
    queue_message_t msg;
    
    // We block for up to 500ms waiting for a message. 
    // This allows the task to sleep when idle but wake up to feed watchdog.
    if (xQueueReceive(taskGetFaultQueue(), &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
      
      if (msg.type == MSG_FAULT_LOGGED) {
          // 1. Reconstruct the entry from the raw data buffer
          const fault_entry_t* entry = (const fault_entry_t*)msg.data;
          
          // 2. Perform the slow NVS write
          faultLogToNVS(entry);
      }
      else if (msg.type == MSG_FAULT_CRITICAL) {
          logError("[FAULT_TASK] CRITICAL signal received.");
      }
    }
    
    // Feed watchdog periodically (even if no faults)
    watchdogFeed("Fault_Log");
  }
}