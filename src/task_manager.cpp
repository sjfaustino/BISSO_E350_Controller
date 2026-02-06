#include "task_manager.h"
#include "cli.h"
#include "config_schema_versioning.h"
#include "config_unified.h"
#include "encoder_motion_integration.h"
#include "encoder_wj66.h"
#include "fault_logging.h"
#include "i2c_bus_recovery.h"
#include "input_validation.h"
#include "lcd_interface.h"
#include "memory_monitor.h"
#include "motion.h"
#include "plc_iface.h"
#include "safety.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include "watchdog_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

// ============================================================================
// RTOS HANDLES
// ============================================================================

static TaskHandle_t task_safety = NULL;
static TaskHandle_t task_motion = NULL;
static TaskHandle_t task_encoder = NULL;
static TaskHandle_t task_plc_comm = NULL;
static TaskHandle_t task_i2c_manager = NULL;
static TaskHandle_t task_cli = NULL;
static TaskHandle_t task_fault_log = NULL;
static TaskHandle_t task_monitor = NULL;
static TaskHandle_t task_telemetry = NULL; // PHASE 5.4: Background telemetry
static TaskHandle_t task_lcd_formatter = NULL; // PHASE 5.4: LCD formatting
static TaskHandle_t task_lcd = NULL;

static QueueHandle_t queue_motion = NULL;
static QueueHandle_t queue_safety = NULL;
static QueueHandle_t queue_encoder = NULL;
static QueueHandle_t queue_plc = NULL;
static QueueHandle_t queue_fault = NULL;
static QueueHandle_t queue_display = NULL;

static SemaphoreHandle_t mutex_i2c = NULL; // Shared Bus Mutex
// mutex_i2c_board, mutex_i2c_plc, mutex_lcd are aliased to mutex_i2c
static SemaphoreHandle_t mutex_motion = NULL;
static SemaphoreHandle_t mutex_buffer = NULL; // NEW: Separate buffer mutex

task_stats_t task_stats[] = {
    {NULL, "Safety", TASK_PRIORITY_SAFETY, 0, 0, 0, 0, 0},
    {NULL, "Motion", TASK_PRIORITY_MOTION, 0, 0, 0, 0, 0},
    {NULL, "Encoder", TASK_PRIORITY_ENCODER, 0, 0, 0, 0, 0},
    {NULL, "PLC_Comm", TASK_PRIORITY_PLC_COMM, 0, 0, 0, 0, 0},
    {NULL, "I2C_Manager", TASK_PRIORITY_I2C_MANAGER, 0, 0, 0, 0, 0},
    {NULL, "CLI", TASK_PRIORITY_CLI, 0, 0, 0, 0, 0},
    {NULL, "Fault_Log", TASK_PRIORITY_FAULT_LOG, 0, 0, 0, 0, 0},
    {NULL, "Monitor", TASK_PRIORITY_MONITOR, 0, 0, 0, 0, 0},
    {NULL, "Telemetry", TASK_PRIORITY_TELEMETRY, 0, 0, 0, 0, 0}, // PHASE 5.4
    {NULL, "LCD_Formatter", TASK_PRIORITY_LCD_FORMAT, 0, 0, 0, 0,
     0}, // PHASE 5.4
    {NULL, "LCD", TASK_PRIORITY_LCD, 0, 0, 0, 0, 0},
};

static int stats_count = sizeof(task_stats) / sizeof(task_stats_t);
static uint32_t boot_time_ms = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void taskManagerInit() {
  logPrintln("[TASKS] Initializing FreeRTOS manager...");
  boot_time_ms = millis();

  // Create Queues with individual error checking
  bool queue_failure = false;

  // PHASE 5.10: Use sizeof(queue_message_t) to prevent memory corruption
  queue_motion = xQueueCreate(QUEUE_LEN_MOTION, sizeof(queue_message_t));
  if (!queue_motion) {
    logError("[TASKS] Motion queue creation failed!");
    queue_failure = true;
  }

  queue_safety = xQueueCreate(QUEUE_LEN_SAFETY, sizeof(queue_message_t));
  if (!queue_safety) {
    logError("[TASKS] Safety queue creation failed!");
    queue_failure = true;
  }

  queue_encoder = xQueueCreate(QUEUE_LEN_ENCODER, sizeof(queue_message_t));
  if (!queue_encoder) {
    logError("[TASKS] Encoder queue creation failed!");
    queue_failure = true;
  }

  queue_plc = xQueueCreate(QUEUE_LEN_PLC, sizeof(queue_message_t));
  if (!queue_plc) {
    logError("[TASKS] PLC queue creation failed!");
    queue_failure = true;
  }

  queue_fault = xQueueCreate(QUEUE_LEN_FAULT, sizeof(queue_message_t));
  if (!queue_fault) {
    logError("[TASKS] Fault queue creation failed!");
    queue_failure = true;
  }

  queue_display = xQueueCreate(QUEUE_LEN_DISPLAY, sizeof(queue_message_t));
  if (!queue_display) {
    logError("[TASKS] Display queue creation failed!");
    queue_failure = true;
  }

  // PHASE 5.10: Initialize event groups for event-driven architecture
  logPrintln("[TASKS] Initializing event groups...");
  if (!systemEventsInit()) {
    logError("[TASKS] Event group initialization failed!");
    queue_failure = true; // Reuse queue_failure flag for init error
  } else {
    logInfo("[TASKS] [OK] Event groups initialized");
  }

  // Create Mutexes with individual error checking
  bool mutex_failure = false;


  mutex_i2c = xSemaphoreCreateMutex();
  if (!mutex_i2c) {
    logError("[TASKS] I2C mutex creation failed!");
    mutex_failure = true;
  }

  // NOTE: mutex_i2c_board, mutex_i2c_plc, mutex_lcd rely on mutex_i2c
  // to ensure exclusive access to the shared Wire bus.

  mutex_motion = xSemaphoreCreateRecursiveMutex();
  if (!mutex_motion) {
    logError("[TASKS] Motion recursive mutex creation failed!");
    mutex_failure = true;
  }

  mutex_buffer = xSemaphoreCreateMutex();
  if (!mutex_buffer) {
    logError("[TASKS] Buffer mutex creation failed!");
    mutex_failure = true;
  }

  // CRITICAL: Halt system if any queue or mutex creation failed
  if (queue_failure || mutex_failure) {
    logError("[TASKS] CRITICAL: FreeRTOS primitives creation failed!");
    logError("[TASKS] Available heap: %u bytes", ESP.getFreeHeap());
    logError("[TASKS] This usually indicates insufficient memory.");
    logError("[TASKS] HALTING SYSTEM!");

    while (1) {
      delay(1000); // Halt system - DO NOT CONTINUE
    }
  }

  logInfo("[TASKS] [OK] Primitives created");
}

void taskManagerStart() {
  logPrintln("[TASKS] Starting scheduler...");

  // Log queue now handles serialization - no delays needed between task creations
  taskSafetyCreate();
  taskMotionCreate();
  taskEncoderCreate();
  taskPlcCommCreate();
  taskI2cManagerCreate();
  logInfo("[TASKS] [OK] All tasks active");
  taskCliCreate();
  taskFaultLogCreate();
  taskMonitorCreate();
  taskTelemetryCreate();    // PHASE 5.4: Background telemetry on Core 0
  taskLcdFormatterCreate(); // PHASE 5.4: LCD formatting on Core 0
  taskLcdCreate();

  // CRITICAL: Validate that all critical tasks were created successfully

  bool critical_failure = false;
  if (task_safety == NULL) {
    logError(
        "[TASKS] CRITICAL FAILURE: Safety task not created - SYSTEM UNSAFE!");
    critical_failure = true;
  }
  if (task_motion == NULL) {
    logError(
        "[TASKS] CRITICAL FAILURE: Motion task not created - CANNOT OPERATE!");
    critical_failure = true;
  }
  if (task_encoder == NULL) {
    logError("[TASKS] CRITICAL FAILURE: Encoder task not created - NO POSITION "
             "FEEDBACK!");
    critical_failure = true;
  }

  if (critical_failure) {
    logError("[TASKS] HALTING SYSTEM - Critical task creation failed!");
    logError("[TASKS] Possible causes: Insufficient heap memory, stack size "
             "too large");
    logError("[TASKS] Available heap: %u bytes", ESP.getFreeHeap());
    while (1) {
      delay(1000); // Halt system in infinite loop - DO NOT START SCHEDULER
    }
  }
}

// ============================================================================
// ACCESSORS & SIGNALS
// ============================================================================

QueueHandle_t taskGetMotionQueue() { return queue_motion; }
QueueHandle_t taskGetSafetyQueue() { return queue_safety; }
QueueHandle_t taskGetEncoderQueue() { return queue_encoder; }
QueueHandle_t taskGetPlcQueue() { return queue_plc; }
QueueHandle_t taskGetFaultQueue() { return queue_fault; }
QueueHandle_t taskGetDisplayQueue() { return queue_display; }

SemaphoreHandle_t taskGetI2cMutex() { return mutex_i2c; }
// PHASE 5.4: Consolidated I2C Mutexes to prevent bus contention
// All devices on the single Wire bus MUST use the same mutex.
SemaphoreHandle_t taskGetI2cBoardMutex() { return mutex_i2c; }
SemaphoreHandle_t taskGetI2cPlcMutex() { return mutex_i2c; }
SemaphoreHandle_t taskGetLcdMutex() { return mutex_i2c; }
SemaphoreHandle_t taskGetMotionMutex() { return mutex_motion; }
SemaphoreHandle_t taskGetBufferMutex() { return mutex_buffer; }

// NEW: Direct Notification for low-latency wakeups
void taskSignalMotionUpdate() {
  if (task_motion)
    xTaskNotifyGive(task_motion);
}

bool taskLockMutex(SemaphoreHandle_t mutex, uint32_t timeout_ms) {
  if (!mutex)
    return false;
  TickType_t ticks =
      (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  
  // PHASE 8: Handle recursive mutexes specifically for motion
  if (mutex == mutex_motion) {
    return xSemaphoreTakeRecursive(mutex, ticks) == pdTRUE;
  }
  
  return xSemaphoreTake(mutex, ticks) == pdTRUE;
}

void taskUnlockMutex(SemaphoreHandle_t mutex) {
  if (!mutex) return;
  
  if (mutex == mutex_motion) {
    xSemaphoreGiveRecursive(mutex);
  } else {
    xSemaphoreGive(mutex);
  }
}

bool taskSendMessage(QueueHandle_t queue, const queue_message_t *msg) {
  if (!queue || !msg)
    return false;
  return xQueueSend(queue, (void *)msg, 0) == pdTRUE;
}

bool taskReceiveMessage(QueueHandle_t queue, queue_message_t *msg,
                        uint32_t timeout_ms) {
  if (!queue || !msg)
    return false;
  TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
  return xQueueReceive(queue, (void *)msg, ticks) == pdTRUE;
}

int taskGetStatsCount() { return stats_count; }
task_stats_t *taskGetStatsArray() { return task_stats; }

// ============================================================================
// TASK DISPATCHERS
// ============================================================================

void taskSafetyCreate() {
  if (xTaskCreatePinnedToCore(taskSafetyFunction, "Safety", TASK_STACK_SAFETY,
                              NULL, TASK_PRIORITY_SAFETY, &task_safety,
                              CORE_1) == pdPASS) {
    task_stats[0].handle = task_safety;
  } else {
    logError("[TASK] CRITICAL: Failed to create Safety task - SYSTEM UNSAFE!");
    task_safety = NULL;
  }
}
void taskMotionCreate() {
  if (xTaskCreatePinnedToCore(taskMotionFunction, "Motion", TASK_STACK_MOTION,
                              NULL, TASK_PRIORITY_MOTION, &task_motion,
                              CORE_1) == pdPASS) {
    task_stats[1].handle = task_motion;
  } else {
    logError("[TASK] CRITICAL: Failed to create Motion task!");
    task_motion = NULL;
  }
}
void taskEncoderCreate() {
  if (xTaskCreatePinnedToCore(taskEncoderFunction, "Encoder",
                              TASK_STACK_ENCODER, NULL, TASK_PRIORITY_ENCODER,
                              &task_encoder, CORE_1) == pdPASS) {
    task_stats[2].handle = task_encoder;
  } else {
    logError("[TASK] CRITICAL: Failed to create Encoder task!");
    task_encoder = NULL;
  }
}
void taskPlcCommCreate() {
  // PHASE 5.7: Ghost Task Removed (Optimization)
  // CRITICAL: This task did NOTHING except feed watchdog every 50ms
  // Waste: 2KB RAM (TASK_STACK_PLC_COMM = 2048 bytes)
  // Fix: Watchdog feed moved to Motion task (Motion already uses PLC I/O)
  // Impact: Saves 2KB RAM, reduces task switching overhead
  // See: docs/PosiPro_IMPROVEMENT_ROADMAP.md and docs/PosiPro_FINAL_AUDIT.md

  // ❌ DISABLED: Ghost task creation
  // if(xTaskCreatePinnedToCore(taskPlcCommFunction, "PLC_Comm",
  // TASK_STACK_PLC_COMM, NULL, TASK_PRIORITY_PLC_COMM, &task_plc_comm, CORE_1)
  // == pdPASS) {
  //   task_stats[3].handle = task_plc_comm;
  // } else {
  //   logError("[TASK] ERROR: Failed to create PLC_Comm task!");
  //   task_plc_comm = NULL;
  // }

  // ✅ Task disabled - PLC watchdog now fed by Motion task
  task_plc_comm = NULL;
  task_stats[3].handle = NULL;
  logInfo("[TASK] PLC task disabled (ghost task optimization - saves 2KB RAM)");
}
void taskI2cManagerCreate() {
  if (xTaskCreatePinnedToCore(
          taskI2cManagerFunction, "I2C_Manager", TASK_STACK_I2C_MANAGER, NULL,
          TASK_PRIORITY_I2C_MANAGER, &task_i2c_manager, CORE_0) == pdPASS) {
    task_stats[4].handle = task_i2c_manager;
  } else {
    logError("[TASK] ERROR: Failed to create I2C_Manager task!");
    task_i2c_manager = NULL;
  }
}
void taskCliCreate() {
  if (xTaskCreatePinnedToCore(taskCliFunction, "CLI", TASK_STACK_CLI, NULL,
                              TASK_PRIORITY_CLI, &task_cli, CORE_0) == pdPASS) {
    task_stats[5].handle = task_cli;
  } else {
    logWarning("[TASK] WARNING: Failed to create CLI task - command interface "
               "unavailable");
    task_cli = NULL;
  }
}
void taskFaultLogCreate() {
  if (xTaskCreatePinnedToCore(
          taskFaultLogFunction, "Fault_Log", TASK_STACK_FAULT_LOG, NULL,
          TASK_PRIORITY_FAULT_LOG, &task_fault_log, CORE_0) == pdPASS) {
    task_stats[6].handle = task_fault_log;
  } else {
    logError(
        "[TASK] ERROR: Failed to create Fault_Log task - faults not logged!");
    task_fault_log = NULL;
  }
}
void taskMonitorCreate() {
  if (xTaskCreatePinnedToCore(taskMonitorFunction, "Monitor",
                              TASK_STACK_MONITOR, NULL, TASK_PRIORITY_MONITOR,
                              &task_monitor, CORE_0) == pdPASS) {
    task_stats[7].handle = task_monitor;
  } else {
    logWarning("[TASK] WARNING: Failed to create Monitor task - diagnostics "
               "unavailable");
    task_monitor = NULL;
  }
}
void taskTelemetryCreate() { // PHASE 5.4: Background telemetry on Core 0
  if (xTaskCreatePinnedToCore(
          taskTelemetryFunction, "Telemetry", TASK_STACK_TELEMETRY, NULL,
          TASK_PRIORITY_TELEMETRY, &task_telemetry, CORE_0) == pdPASS) {
    task_stats[8].handle = task_telemetry;
  } else {
    logWarning("[TASK] WARNING: Failed to create Telemetry task - metrics "
               "unavailable");
    task_telemetry = NULL;
  }
}
void taskLcdFormatterCreate() { // PHASE 5.4: LCD formatting on Core 0
  if (xTaskCreatePinnedToCore(taskLcdFormatterFunction, "LCD_Formatter",
                              TASK_STACK_LCD_FORMAT, NULL,
                              TASK_PRIORITY_LCD_FORMAT, &task_lcd_formatter,
                              CORE_0) == pdPASS) {
    task_stats[9].handle = task_lcd_formatter;
  } else {
    logWarning("[TASK] WARNING: Failed to create LCD_Formatter task - display "
               "degraded");
    task_lcd_formatter = NULL;
  }
}
void taskLcdCreate() {
  if (xTaskCreatePinnedToCore(taskLcdFunction, "LCD", TASK_STACK_LCD, NULL,
                              TASK_PRIORITY_LCD, &task_lcd, CORE_0) == pdPASS) {
    task_stats[10].handle = task_lcd;
  } else {
    logWarning("[TASK] WARNING: Failed to create LCD task - no display");
    task_lcd = NULL;
  }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void taskShowStats() {
  logPrintln("\r\n=== TASK STATISTICS ===");
  logPrintln("Task               Runs      Avg(ms)   Max(ms)   CPU%%");
  logPrintln("------------------------------------------------------");

  uint32_t total_time = 0;
  for (int i = 0; i < stats_count; i++)
    total_time += task_stats[i].total_time_ms;

  for (int i = 0; i < stats_count; i++) {
    float avg_time =
        (task_stats[i].run_count > 0)
            ? (float)task_stats[i].total_time_ms / task_stats[i].run_count
            : 0;
    float cpu_percent =
        (total_time > 0)
            ? ((float)task_stats[i].total_time_ms / total_time) * 100.0
            : 0;

    logPrintf("%-18s %-9lu %-9.2f %-9lu %-6.1f%%\r\n", task_stats[i].name,
                  (unsigned long)task_stats[i].run_count, avg_time,
                  (unsigned long)task_stats[i].max_run_time_ms, cpu_percent);
  }
  logPrintln("");
}

void taskShowAllTasks() {
  logPrintln("\r\n=== TASK LIST ===");
  logPrintln("Task                  Priority  Stack(bytes)  Core");
  logPrintln("--------------------------------------------------");
  for (int i = 0; i < stats_count; i++) {
    if (!task_stats[i].handle)
      continue;
    UBaseType_t priority = uxTaskPriorityGet(task_stats[i].handle);
    UBaseType_t stack_high_water =
        uxTaskGetStackHighWaterMark(task_stats[i].handle);
    BaseType_t core_id = xTaskGetAffinity(task_stats[i].handle);

    logPrintf("%-21s %-9lu %-13lu %ld\r\n", task_stats[i].name,
                  (unsigned long)priority,
                  (unsigned long)stack_high_water, (long)core_id);
  }
  logPrintln("");
}

extern volatile uint32_t accumulated_loop_count;

uint8_t taskGetCpuUsage() {
    static uint32_t last_count = 0;
    static uint32_t last_time = 0;
    static uint8_t cpu = 0;
    static bool first_run = true;
    
    // PHASE 5.3: Heartbeat from loop() task via accumulated_loop_count
    uint32_t now = millis();
    if (now - last_time >= 1000) {
        uint32_t current = accumulated_loop_count;
        uint32_t delta = current - last_count;
        last_count = current;
        last_time = now;
        
        // Loop runs with delay(10), so theoretical max is ~100.
        // If system is healthy, loop runs frequently.
        // If delta is low, CPU is busy with RTOS tasks.
        if (delta > 100) delta = 100;
        
        uint8_t measured_cpu = (uint8_t)(100 - delta);
        
        // At boot, the loop might not have run yet. 
        // If it's the first run and delta is 0, don't report 100%.
        if (first_run && delta == 0) {
            cpu = 0;
            first_run = false;
        } else {
            // Simple smoothing: 50% previous, 50% current
            cpu = (uint8_t)((cpu + measured_cpu) / 2);
            first_run = false;
        }
    }
    return cpu;
}

uint32_t taskGetUptime() { return (millis() - boot_time_ms) / 1000; }

// Update stack usage statistics for all tracked tasks
void taskUpdateStackUsage() {
    for (int i = 0; i < stats_count; i++) {
        if (task_stats[i].handle) {
            // Returns minimum free stack space in bytes on ESP32
            task_stats[i].stack_high_water = uxTaskGetStackHighWaterMark(task_stats[i].handle);
        }
    }
}

// ============================================================================
// PHASE 2.5: ADAPTIVE I2C TIMEOUT
// ============================================================================

uint32_t taskGetAdaptiveI2cTimeout() {
  // PHASE 2.5: Scale I2C timeout based on current system load
  // Rationale: Under high load, I2C transactions may be delayed by
  // higher-priority tasks. Adaptive timeout prevents spurious failures while
  // keeping latency low during light load. Scaling formula: timeout = base +
  // (cpu_usage * scale)

  uint8_t cpu_usage = taskGetCpuUsage();

  // Linear scaling: 50ms @ 0% CPU, 500ms @ 100% CPU
  uint32_t timeout_ms =
      I2C_TIMEOUT_BASE_MS + (uint32_t)((float)cpu_usage * I2C_TIMEOUT_SCALE);

  // Cap at maximum to prevent excessive waits
  if (timeout_ms > I2C_TIMEOUT_MAX_MS) {
    timeout_ms = I2C_TIMEOUT_MAX_MS;
  }

  return timeout_ms;
}
