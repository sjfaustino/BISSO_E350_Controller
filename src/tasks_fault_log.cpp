#include "task_manager.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void taskFaultLogFunction(void* parameter) {
  // Removed unused last_wake variable since this task blocks on the queue
  
  logInfo("[FAULT_TASK] [OK] Started on core 0");
  watchdogTaskAdd("Fault_Log");
  watchdogSubscribeTask(xTaskGetCurrentTaskHandle(), "Fault_Log");

  while (1) {
    // Process Fault Queue
    queue_message_t msg;
    
    // Block up to 500ms waiting for message.
    // This timeout acts as our loop delay if no messages arrive.
    if (xQueueReceive(taskGetFaultQueue(), &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
      if (msg.type == MSG_FAULT_LOGGED) {
          // Reconstruct and write to NVS (Slow Blocking Operation)
          const fault_entry_t* entry = (const fault_entry_t*)msg.data;
          faultLogToNVS(entry);
          yield();  // Yield after slow NVS operation
          watchdogFeed("Fault_Log");  // Feed watchdog immediately after slow op
      }
      else if (msg.type == MSG_FAULT_CRITICAL) {
          logError("[FAULT_TASK] [CRIT] Critical signal received.");
      }
    }
    
    // Feed watchdog every loop (at least every 500ms due to timeout)
    watchdogFeed("Fault_Log");
  }
}