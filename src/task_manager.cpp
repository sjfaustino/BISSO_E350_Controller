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
static TaskHandle_t task_lcd = NULL;

static QueueHandle_t queue_motion = NULL;
static QueueHandle_t queue_safety = NULL;
static QueueHandle_t queue_encoder = NULL;
static QueueHandle_t queue_plc = NULL;
static QueueHandle_t queue_fault = NULL;
static QueueHandle_t queue_display = NULL;

static SemaphoreHandle_t mutex_config = NULL;
static SemaphoreHandle_t mutex_i2c = NULL;
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
    faultLogError(FAULT_BOOT_FAILED, "Task queue creation failed");
  }
  
  // Create Mutexes
  mutex_config = xSemaphoreCreateMutex();
  mutex_i2c = xSemaphoreCreateMutex();
  mutex_motion = xSemaphoreCreateMutex();
  
  if (!mutex_config || !mutex_i2c || !mutex_motion) {
    Serial.println("[TASKS] [FAIL] Mutex creation failed!");
    faultLogError(FAULT_BOOT_FAILED, "Mutex creation failed");
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
  taskLcdCreate();
  
  Serial.println("[TASKS] [OK] All tasks active");
}

// ============================================================================
// ACCESSORS
// ============================================================================

QueueHandle_t taskGetMotionQueue() { return queue_motion; }
QueueHandle_t taskGetSafetyQueue() { return queue_safety; }
QueueHandle_t taskGetEncoderQueue() { return queue_encoder; }
QueueHandle_t taskGetPlcQueue() { return queue_plc; }
QueueHandle_t taskGetFaultQueue() { return queue_fault; }
QueueHandle_t taskGetDisplayQueue() { return queue_display; }

SemaphoreHandle_t taskGetConfigMutex() { return mutex_config; }
SemaphoreHandle_t taskGetI2cMutex() { return mutex_i2c; }
SemaphoreHandle_t taskGetMotionMutex() { return mutex_motion; }

bool taskLockMutex(SemaphoreHandle_t mutex, uint32_t timeout_ms) {
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
  if(xTaskCreatePinnedToCore(taskSafetyFunction, "Safety", TASK_STACK_SAFETY, NULL, TASK_PRIORITY_SAFETY, &task_safety, CORE_1) == pdPASS) task_stats[0].handle = task_safety;
}
void taskMotionCreate() {
  if(xTaskCreatePinnedToCore(taskMotionFunction, "Motion", TASK_STACK_MOTION, NULL, TASK_PRIORITY_MOTION, &task_motion, CORE_1) == pdPASS) task_stats[1].handle = task_motion;
}
void taskEncoderCreate() {
  if(xTaskCreatePinnedToCore(taskEncoderFunction, "Encoder", TASK_STACK_ENCODER, NULL, TASK_PRIORITY_ENCODER, &task_encoder, CORE_1) == pdPASS) task_stats[2].handle = task_encoder;
}
void taskPlcCommCreate() {
  if(xTaskCreatePinnedToCore(taskPlcCommFunction, "PLC_Comm", TASK_STACK_PLC_COMM, NULL, TASK_PRIORITY_PLC_COMM, &task_plc_comm, CORE_1) == pdPASS) task_stats[3].handle = task_plc_comm;
}
void taskI2cManagerCreate() {
  if(xTaskCreatePinnedToCore(taskI2cManagerFunction, "I2C_Manager", TASK_STACK_I2C_MANAGER, NULL, TASK_PRIORITY_I2C_MANAGER, &task_i2c_manager, CORE_1) == pdPASS) task_stats[4].handle = task_i2c_manager;
}
void taskCliCreate() {
  if(xTaskCreatePinnedToCore(taskCliFunction, "CLI", TASK_STACK_CLI, NULL, TASK_PRIORITY_CLI, &task_cli, CORE_0) == pdPASS) task_stats[5].handle = task_cli;
}
void taskFaultLogCreate() {
  if(xTaskCreatePinnedToCore(taskFaultLogFunction, "Fault_Log", TASK_STACK_FAULT_LOG, NULL, TASK_PRIORITY_FAULT_LOG, &task_fault_log, CORE_0) == pdPASS) task_stats[6].handle = task_fault_log;
}
void taskMonitorCreate() {
  if(xTaskCreatePinnedToCore(taskMonitorFunction, "Monitor", TASK_STACK_MONITOR, NULL, TASK_PRIORITY_MONITOR, &task_monitor, CORE_1) == pdPASS) task_stats[7].handle = task_monitor;
}
void taskLcdCreate() {
  if(xTaskCreatePinnedToCore(taskLcdFunction, "LCD", TASK_STACK_LCD, NULL, TASK_PRIORITY_LCD, &task_lcd, CORE_1) == pdPASS) task_stats[8].handle = task_lcd;
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
    float avg_time = (task_stats[i].run_count > 0) ? (float)task_stats[i].total_time_ms / task_stats[i].run_count : 0;
    float cpu_percent = (total_time > 0) ? ((float)task_stats[i].total_time_ms / total_time) * 100.0 : 0;
    
    Serial.printf("%-18s %-9lu %-9.2f %-9lu %-6.1f%%\n", 
        task_stats[i].name, task_stats[i].run_count, avg_time, task_stats[i].max_run_time_ms, cpu_percent);
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
    UBaseType_t stack = uxTaskGetStackHighWaterMark(task_stats[i].handle);
    BaseType_t core = xTaskGetAffinity(task_stats[i].handle);
    Serial.printf("%-21s %-9lu %-13lu %ld\n", task_stats[i].name, priority, stack * 4, core);
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