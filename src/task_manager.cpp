#include "task_manager.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "watchdog_manager.h"
#include "system_constants.h"
#include "safety.h"
#include "motion.h"
#include "encoder_wj66.h"
#include "plc_iface.h"
#include "i2c_bus_recovery.h"
#include "cli.h"
#include "lcd_interface.h"
#include "config_unified.h"
#include "memory_monitor.h"
#include "input_validation.h"
#include "config_schema_versioning.h"
#include "encoder_motion_integration.h"
#include <string.h> 
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

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
static TaskHandle_t task_telemetry = NULL;  // PHASE 5.4: Background telemetry
static TaskHandle_t task_lcd_formatter = NULL;  // PHASE 5.4: LCD formatting
static TaskHandle_t task_lcd = NULL;

static QueueHandle_t queue_motion = NULL;
static QueueHandle_t queue_safety = NULL;
static QueueHandle_t queue_encoder = NULL;
static QueueHandle_t queue_plc = NULL;
static QueueHandle_t queue_fault = NULL;
static QueueHandle_t queue_display = NULL;

static SemaphoreHandle_t mutex_config = NULL;
static SemaphoreHandle_t mutex_i2c = NULL;  // Kept for backwards compatibility
static SemaphoreHandle_t mutex_i2c_board = NULL;  // PHASE 5.4: Board inputs
static SemaphoreHandle_t mutex_i2c_plc = NULL;    // PHASE 5.4: PLC interface
static SemaphoreHandle_t mutex_motion = NULL;

task_stats_t task_stats[] = {
  {NULL, "Safety", TASK_PRIORITY_SAFETY, 0, 0, 0, 0, 0},
  {NULL, "Motion", TASK_PRIORITY_MOTION, 0, 0, 0, 0, 0},
  {NULL, "Encoder", TASK_PRIORITY_ENCODER, 0, 0, 0, 0, 0},
  {NULL, "PLC_Comm", TASK_PRIORITY_PLC_COMM, 0, 0, 0, 0, 0},
  {NULL, "I2C_Manager", TASK_PRIORITY_I2C_MANAGER, 0, 0, 0, 0, 0},
  {NULL, "CLI", TASK_PRIORITY_CLI, 0, 0, 0, 0, 0},
  {NULL, "Fault_Log", TASK_PRIORITY_FAULT_LOG, 0, 0, 0, 0, 0},
  {NULL, "Monitor", TASK_PRIORITY_MONITOR, 0, 0, 0, 0, 0},
  {NULL, "Telemetry", TASK_PRIORITY_TELEMETRY, 0, 0, 0, 0, 0},  // PHASE 5.4
  {NULL, "LCD_Formatter", TASK_PRIORITY_LCD_FORMAT, 0, 0, 0, 0, 0},  // PHASE 5.4
  {NULL, "LCD", TASK_PRIORITY_LCD, 0, 0, 0, 0, 0},
};

static int stats_count = sizeof(task_stats) / sizeof(task_stats_t);
static uint32_t boot_time_ms = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void taskManagerInit() {
  Serial.println("[TASKS] Initializing FreeRTOS manager...");
  boot_time_ms = millis();
  
  // Create Queues
  queue_motion = xQueueCreate(QUEUE_LEN_MOTION, QUEUE_ITEM_SIZE);
  queue_safety = xQueueCreate(QUEUE_LEN_SAFETY, QUEUE_ITEM_SIZE);
  queue_encoder = xQueueCreate(QUEUE_LEN_ENCODER, QUEUE_ITEM_SIZE);
  queue_plc = xQueueCreate(QUEUE_LEN_PLC, QUEUE_ITEM_SIZE);
  queue_fault = xQueueCreate(QUEUE_LEN_FAULT, QUEUE_ITEM_SIZE); // Size 96 for async logs
  queue_display = xQueueCreate(QUEUE_LEN_DISPLAY, QUEUE_ITEM_SIZE);
  
  if (!queue_motion || !queue_safety || !queue_encoder || 
      !queue_plc || !queue_fault || !queue_display) {
    Serial.println("[TASKS] [FAIL] Queue creation failed!");
    // faultLogError(FAULT_BOOT_FAILED, "Task queue creation failed"); // Avoid circular dependency if fault log relies on queue
  }
  
  // Create Mutexes
  mutex_config = xSemaphoreCreateMutex();
  mutex_i2c = xSemaphoreCreateMutex();  // Kept for backwards compatibility
  mutex_i2c_board = xSemaphoreCreateMutex();  // PHASE 5.4: Board inputs
  mutex_i2c_plc = xSemaphoreCreateMutex();    // PHASE 5.4: PLC interface
  mutex_motion = xSemaphoreCreateMutex();

  if (!mutex_config || !mutex_i2c || !mutex_i2c_board || !mutex_i2c_plc || !mutex_motion) {
    Serial.println("[TASKS] [FAIL] Mutex creation failed!");
    // faultLogError(FAULT_BOOT_FAILED, "Mutex creation failed");
  }
  Serial.println("[TASKS] [OK] Primitives created");
}

void taskManagerStart() {
  Serial.println("[TASKS] Starting scheduler...");

  taskSafetyCreate();
  taskMotionCreate();
  taskEncoderCreate();
  taskPlcCommCreate();
  taskI2cManagerCreate();
  taskCliCreate();
  taskFaultLogCreate();
  taskMonitorCreate();
  taskTelemetryCreate();  // PHASE 5.4: Background telemetry on Core 0
  taskLcdFormatterCreate();  // PHASE 5.4: LCD formatting on Core 0
  taskLcdCreate();

  // CRITICAL: Validate that all critical tasks were created successfully
  bool critical_failure = false;
  if (task_safety == NULL) {
    logError("[TASKS] CRITICAL FAILURE: Safety task not created - SYSTEM UNSAFE!");
    critical_failure = true;
  }
  if (task_motion == NULL) {
    logError("[TASKS] CRITICAL FAILURE: Motion task not created - CANNOT OPERATE!");
    critical_failure = true;
  }
  if (task_encoder == NULL) {
    logError("[TASKS] CRITICAL FAILURE: Encoder task not created - NO POSITION FEEDBACK!");
    critical_failure = true;
  }

  if (critical_failure) {
    logError("[TASKS] HALTING SYSTEM - Critical task creation failed!");
    logError("[TASKS] Possible causes: Insufficient heap memory, stack size too large");
    logError("[TASKS] Available heap: %u bytes", ESP.getFreeHeap());
    while (1) {
      delay(1000);  // Halt system in infinite loop - DO NOT START SCHEDULER
    }
  }

  Serial.println("[TASKS] [OK] All tasks active");
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

SemaphoreHandle_t taskGetConfigMutex() { return mutex_config; }
SemaphoreHandle_t taskGetI2cMutex() { return mutex_i2c; }  // Kept for backwards compatibility
SemaphoreHandle_t taskGetI2cBoardMutex() { return mutex_i2c_board; }  // PHASE 5.4: Board inputs
SemaphoreHandle_t taskGetI2cPlcMutex() { return mutex_i2c_plc; }      // PHASE 5.4: PLC interface
SemaphoreHandle_t taskGetMotionMutex() { return mutex_motion; }

// NEW: Direct Notification for low-latency wakeups
void taskSignalMotionUpdate() {
    if (task_motion) xTaskNotifyGive(task_motion);
}

bool taskLockMutex(SemaphoreHandle_t mutex, uint32_t timeout_ms) {
  if (!mutex) return false;
  TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTake(mutex, ticks) == pdTRUE;
}

void taskUnlockMutex(SemaphoreHandle_t mutex) {
  if (mutex) xSemaphoreGive(mutex);
}

bool taskSendMessage(QueueHandle_t queue, const queue_message_t* msg) {
  if (!queue || !msg) return false;
  return xQueueSend(queue, (void*)msg, 0) == pdTRUE;
}

bool taskReceiveMessage(QueueHandle_t queue, queue_message_t* msg, uint32_t timeout_ms) {
  if (!queue || !msg) return false;
  TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
  return xQueueReceive(queue, (void*)msg, ticks) == pdTRUE;
}

int taskGetStatsCount() { return stats_count; }
task_stats_t* taskGetStatsArray() { return task_stats; }

// ============================================================================
// TASK DISPATCHERS
// ============================================================================

void taskSafetyCreate() {
  if(xTaskCreatePinnedToCore(taskSafetyFunction, "Safety", TASK_STACK_SAFETY, NULL, TASK_PRIORITY_SAFETY, &task_safety, CORE_1) == pdPASS) {
    task_stats[0].handle = task_safety;
  } else {
    logError("[TASK] CRITICAL: Failed to create Safety task - SYSTEM UNSAFE!");
    task_safety = NULL;
  }
}
void taskMotionCreate() {
  if(xTaskCreatePinnedToCore(taskMotionFunction, "Motion", TASK_STACK_MOTION, NULL, TASK_PRIORITY_MOTION, &task_motion, CORE_1) == pdPASS) {
    task_stats[1].handle = task_motion;
  } else {
    logError("[TASK] CRITICAL: Failed to create Motion task!");
    task_motion = NULL;
  }
}
void taskEncoderCreate() {
  if(xTaskCreatePinnedToCore(taskEncoderFunction, "Encoder", TASK_STACK_ENCODER, NULL, TASK_PRIORITY_ENCODER, &task_encoder, CORE_1) == pdPASS) {
    task_stats[2].handle = task_encoder;
  } else {
    logError("[TASK] CRITICAL: Failed to create Encoder task!");
    task_encoder = NULL;
  }
}
void taskPlcCommCreate() {
  if(xTaskCreatePinnedToCore(taskPlcCommFunction, "PLC_Comm", TASK_STACK_PLC_COMM, NULL, TASK_PRIORITY_PLC_COMM, &task_plc_comm, CORE_1) == pdPASS) {
    task_stats[3].handle = task_plc_comm;
  } else {
    logError("[TASK] ERROR: Failed to create PLC_Comm task!");
    task_plc_comm = NULL;
  }
}
void taskI2cManagerCreate() {
  if(xTaskCreatePinnedToCore(taskI2cManagerFunction, "I2C_Manager", TASK_STACK_I2C_MANAGER, NULL, TASK_PRIORITY_I2C_MANAGER, &task_i2c_manager, CORE_1) == pdPASS) {
    task_stats[4].handle = task_i2c_manager;
  } else {
    logError("[TASK] ERROR: Failed to create I2C_Manager task!");
    task_i2c_manager = NULL;
  }
}
void taskCliCreate() {
  if(xTaskCreatePinnedToCore(taskCliFunction, "CLI", TASK_STACK_CLI, NULL, TASK_PRIORITY_CLI, &task_cli, CORE_0) == pdPASS) {
    task_stats[5].handle = task_cli;
  } else {
    logWarning("[TASK] WARNING: Failed to create CLI task - command interface unavailable");
    task_cli = NULL;
  }
}
void taskFaultLogCreate() {
  if(xTaskCreatePinnedToCore(taskFaultLogFunction, "Fault_Log", TASK_STACK_FAULT_LOG, NULL, TASK_PRIORITY_FAULT_LOG, &task_fault_log, CORE_0) == pdPASS) {
    task_stats[6].handle = task_fault_log;
  } else {
    logError("[TASK] ERROR: Failed to create Fault_Log task - faults not logged!");
    task_fault_log = NULL;
  }
}
void taskMonitorCreate() {
  if(xTaskCreatePinnedToCore(taskMonitorFunction, "Monitor", TASK_STACK_MONITOR, NULL, TASK_PRIORITY_MONITOR, &task_monitor, CORE_1) == pdPASS) {
    task_stats[7].handle = task_monitor;
  } else {
    logWarning("[TASK] WARNING: Failed to create Monitor task - diagnostics unavailable");
    task_monitor = NULL;
  }
}
void taskTelemetryCreate() {  // PHASE 5.4: Background telemetry on Core 0
  if(xTaskCreatePinnedToCore(taskTelemetryFunction, "Telemetry", TASK_STACK_TELEMETRY, NULL, TASK_PRIORITY_TELEMETRY, &task_telemetry, CORE_0) == pdPASS) {
    task_stats[8].handle = task_telemetry;
  } else {
    logWarning("[TASK] WARNING: Failed to create Telemetry task - metrics unavailable");
    task_telemetry = NULL;
  }
}
void taskLcdFormatterCreate() {  // PHASE 5.4: LCD formatting on Core 0
  if(xTaskCreatePinnedToCore(taskLcdFormatterFunction, "LCD_Formatter", TASK_STACK_LCD_FORMAT, NULL, TASK_PRIORITY_LCD_FORMAT, &task_lcd_formatter, CORE_0) == pdPASS) {
    task_stats[9].handle = task_lcd_formatter;
  } else {
    logWarning("[TASK] WARNING: Failed to create LCD_Formatter task - display degraded");
    task_lcd_formatter = NULL;
  }
}
void taskLcdCreate() {
  if(xTaskCreatePinnedToCore(taskLcdFunction, "LCD", TASK_STACK_LCD, NULL, TASK_PRIORITY_LCD, &task_lcd, CORE_1) == pdPASS) {
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
  Serial.println("\n=== TASK STATISTICS ===");
  Serial.println("Task               Runs      Avg(ms)   Max(ms)   CPU%");
  Serial.println("------------------------------------------------------");
  
  uint32_t total_time = 0;
  for (int i = 0; i < stats_count; i++) total_time += task_stats[i].total_time_ms;
  
  for (int i = 0; i < stats_count; i++) {
    float avg_time = (task_stats[i].run_count > 0) ? 
      (float)task_stats[i].total_time_ms / task_stats[i].run_count : 0;
    float cpu_percent = (total_time > 0) ? 
      ((float)task_stats[i].total_time_ms / total_time) * 100.0 : 0;
    
    Serial.printf("%-18s %-9lu %-9.2f %-9lu %-6.1f%%\n", 
        task_stats[i].name, 
        (unsigned long)task_stats[i].run_count, 
        avg_time, 
        (unsigned long)task_stats[i].max_run_time_ms, 
        cpu_percent);
  }
  Serial.println();
}

void taskShowAllTasks() {
  Serial.println("\n=== TASK LIST ===");
  Serial.println("Task                  Priority  Stack(bytes)  Core");
  Serial.println("--------------------------------------------------");
  for (int i = 0; i < stats_count; i++) {
    if (!task_stats[i].handle) continue;
    UBaseType_t priority = uxTaskPriorityGet(task_stats[i].handle);
    UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(task_stats[i].handle);
    BaseType_t core_id = xTaskGetAffinity(task_stats[i].handle);
    
    Serial.printf("%-21s %-9lu %-13lu %ld\n", 
        task_stats[i].name, 
        (unsigned long)priority, 
        (unsigned long)(stack_high_water * 4), 
        (long)core_id);
  }
  Serial.println();
}

uint8_t taskGetCpuUsage() {
  uint32_t total = 0;
  uint32_t elapsed = millis() - boot_time_ms;
  if (elapsed == 0) return 0;
  for (int i = 0; i < stats_count; i++) total += task_stats[i].last_run_time_ms;
  return (uint8_t)((total * 100) / elapsed);
}

uint32_t taskGetUptime() { return (millis() - boot_time_ms) / 1000; }

// ============================================================================
// PHASE 2.5: ADAPTIVE I2C TIMEOUT
// ============================================================================

uint32_t taskGetAdaptiveI2cTimeout() {
  // PHASE 2.5: Scale I2C timeout based on current system load
  // Rationale: Under high load, I2C transactions may be delayed by higher-priority
  // tasks. Adaptive timeout prevents spurious failures while keeping latency low
  // during light load. Scaling formula: timeout = base + (cpu_usage * scale)

  uint8_t cpu_usage = taskGetCpuUsage();

  // Linear scaling: 50ms @ 0% CPU, 500ms @ 100% CPU
  uint32_t timeout_ms = I2C_TIMEOUT_BASE_MS + (uint32_t)((float)cpu_usage * I2C_TIMEOUT_SCALE);

  // Cap at maximum to prevent excessive waits
  if (timeout_ms > I2C_TIMEOUT_MAX_MS) {
    timeout_ms = I2C_TIMEOUT_MAX_MS;
  }

  return timeout_ms;
}