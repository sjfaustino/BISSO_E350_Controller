#include "task_manager.h"
#include "safety.h"
#include "motion.h"
#include "encoder_wj66.h"
#include "plc_iface.h"
#include "i2c_bus_recovery.h"
#include "cli.h"
#include "fault_logging.h"
#include "lcd_interface.h"
#include "config_unified.h"
#include "watchdog_manager.h"

// ============================================================================
// TASK HANDLES - Global references to all tasks
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

// ============================================================================
// MESSAGE QUEUES - Inter-task communication
// ============================================================================

static QueueHandle_t queue_motion = NULL;
static QueueHandle_t queue_safety = NULL;
static QueueHandle_t queue_encoder = NULL;
static QueueHandle_t queue_plc = NULL;
static QueueHandle_t queue_fault = NULL;
static QueueHandle_t queue_display = NULL;

// ============================================================================
// SYNCHRONIZATION PRIMITIVES - Mutex protection
// ============================================================================

static SemaphoreHandle_t mutex_config = NULL;      // Configuration access
static SemaphoreHandle_t mutex_i2c = NULL;         // I2C bus access
static SemaphoreHandle_t mutex_motion = NULL;      // Motion state

// ============================================================================
// TASK STATISTICS
// ============================================================================

static task_stats_t task_stats[] = {
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
// TASK MANAGER INITIALIZATION
// ============================================================================

void taskManagerInit() {
  Serial.println("\n[TASKS] Initializing FreeRTOS task manager...");
  
  boot_time_ms = millis();
  
  // Create message queues
  Serial.println("[TASKS] Creating message queues...");
  queue_motion = xQueueCreate(QUEUE_LEN_MOTION, QUEUE_ITEM_SIZE);
  queue_safety = xQueueCreate(QUEUE_LEN_SAFETY, QUEUE_ITEM_SIZE);
  queue_encoder = xQueueCreate(QUEUE_LEN_ENCODER, QUEUE_ITEM_SIZE);
  queue_plc = xQueueCreate(QUEUE_LEN_PLC, QUEUE_ITEM_SIZE);
  queue_fault = xQueueCreate(QUEUE_LEN_FAULT, QUEUE_ITEM_SIZE);
  queue_display = xQueueCreate(QUEUE_LEN_DISPLAY, QUEUE_ITEM_SIZE);
  
  if (!queue_motion || !queue_safety || !queue_encoder || 
      !queue_plc || !queue_fault || !queue_display) {
    Serial.println("[TASKS] ERROR: Failed to create queues!");
    return;
  }
  Serial.println("[TASKS] ✅ Message queues created");
  
  // Create mutexes for shared resources
  Serial.println("[TASKS] Creating synchronization primitives...");
  mutex_config = xSemaphoreCreateMutex();
  mutex_i2c = xSemaphoreCreateMutex();
  mutex_motion = xSemaphoreCreateMutex();
  
  if (!mutex_config || !mutex_i2c || !mutex_motion) {
    Serial.println("[TASKS] ERROR: Failed to create mutexes!");
    return;
  }
  Serial.println("[TASKS] ✅ Mutexes created");
  
  // Print task configuration
  Serial.println("\n[TASKS] Task Priority Configuration:");
  Serial.println("[TASKS] ┌─────────────────┬──────────────────────────┐");
  Serial.println("[TASKS] │ Task            │ Priority (Stack)         │");
  Serial.println("[TASKS] ├─────────────────┼──────────────────────────┤");
  Serial.print("[TASKS] │ Safety          │ "); Serial.print(TASK_PRIORITY_SAFETY); Serial.print(" ("); Serial.print(TASK_STACK_SAFETY); Serial.println(" w)     │");
  Serial.print("[TASKS] │ Motion          │ "); Serial.print(TASK_PRIORITY_MOTION); Serial.print(" ("); Serial.print(TASK_STACK_MOTION); Serial.println(" w)     │");
  Serial.print("[TASKS] │ Encoder         │ "); Serial.print(TASK_PRIORITY_ENCODER); Serial.print(" ("); Serial.print(TASK_STACK_ENCODER); Serial.println(" w)     │");
  Serial.print("[TASKS] │ PLC Comm        │ "); Serial.print(TASK_PRIORITY_PLC_COMM); Serial.print(" ("); Serial.print(TASK_STACK_PLC_COMM); Serial.println(" w)     │");
  Serial.print("[TASKS] │ I2C Manager     │ "); Serial.print(TASK_PRIORITY_I2C_MANAGER); Serial.print(" ("); Serial.print(TASK_STACK_I2C_MANAGER); Serial.println(" w)     │");
  Serial.print("[TASKS] │ CLI             │ "); Serial.print(TASK_PRIORITY_CLI); Serial.print(" ("); Serial.print(TASK_STACK_CLI); Serial.println(" w)     │");
  Serial.print("[TASKS] │ Fault Log       │ "); Serial.print(TASK_PRIORITY_FAULT_LOG); Serial.print(" ("); Serial.print(TASK_STACK_FAULT_LOG); Serial.println(" w)     │");
  Serial.print("[TASKS] │ Monitor         │ "); Serial.print(TASK_PRIORITY_MONITOR); Serial.print(" ("); Serial.print(TASK_STACK_MONITOR); Serial.println(" w)     │");
  Serial.print("[TASKS] │ LCD             │ "); Serial.print(TASK_PRIORITY_LCD); Serial.print(" ("); Serial.print(TASK_STACK_LCD); Serial.println(" w)     │");
  Serial.println("[TASKS] └─────────────────┴──────────────────────────┘");
  
  Serial.println("[TASKS] ✅ Task manager initialized");
}

void taskManagerStart() {
  Serial.println("\n[TASKS] Starting all FreeRTOS tasks...\n");
  
  // Create tasks in order of priority (highest first)
  taskSafetyCreate();
  taskMotionCreate();
  taskEncoderCreate();
  taskPlcCommCreate();
  taskI2cManagerCreate();
  taskCliCreate();
  taskFaultLogCreate();
  taskMonitorCreate();
  taskLcdCreate();
  
  Serial.println("\n[TASKS] ✅ All tasks started\n");
}

// ============================================================================
// QUEUE ACCESSORS
// ============================================================================

QueueHandle_t taskGetMotionQueue() { return queue_motion; }
QueueHandle_t taskGetSafetyQueue() { return queue_safety; }
QueueHandle_t taskGetEncoderQueue() { return queue_encoder; }
QueueHandle_t taskGetPlcQueue() { return queue_plc; }
QueueHandle_t taskGetFaultQueue() { return queue_fault; }
QueueHandle_t taskGetDisplayQueue() { return queue_display; }

// ============================================================================
// MESSAGE PASSING FUNCTIONS
// ============================================================================

bool taskSendMessage(QueueHandle_t queue, const queue_message_t* msg) {
  if (!queue || !msg) return false;
  return xQueueSend(queue, (void*)msg, 0) == pdTRUE;  // Non-blocking
}

bool taskReceiveMessage(QueueHandle_t queue, queue_message_t* msg, uint32_t timeout_ms) {
  if (!queue || !msg) return false;
  TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
  return xQueueReceive(queue, (void*)msg, ticks) == pdTRUE;
}

// ============================================================================
// MUTEX FUNCTIONS
// ============================================================================

SemaphoreHandle_t taskGetConfigMutex() { return mutex_config; }
SemaphoreHandle_t taskGetI2cMutex() { return mutex_i2c; }
SemaphoreHandle_t taskGetMotionMutex() { return mutex_motion; }

bool taskLockMutex(SemaphoreHandle_t mutex, uint32_t timeout_ms) {
  if (!mutex) return false;
  TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTake(mutex, ticks) == pdTRUE;
}

void taskUnlockMutex(SemaphoreHandle_t mutex) {
  if (mutex) xSemaphoreGive(mutex);
}

// ============================================================================
// TASK: SAFETY (Highest Priority - Core 1)
// ============================================================================

void taskSafetyCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskSafetyFunction,           // Task function
    "Safety",                     // Task name
    TASK_STACK_SAFETY,           // Stack size (words)
    NULL,                        // Parameters
    TASK_PRIORITY_SAFETY,        // Priority
    &task_safety,                // Task handle
    CORE_1                       // Core affinity
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create Safety task!");
  } else {
    Serial.println("[TASKS] ✓ Safety task created (priority 24, core 1)");
    task_stats[0].handle = task_safety;
  }
}

void taskSafetyFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t loop_count = 0;
  
  Serial.println("[SAFETY_TASK] Started on core 1");
  watchdogTaskAdd("Safety");
  
  while (1) {
    uint32_t task_start = millis();
    loop_count++;
    
    // Critical safety operations
    safetyUpdate();  // Check e-stop, interlocks, alarms
    
    // Check for safety messages
    queue_message_t msg;
    while (taskReceiveMessage(queue_safety, &msg, 0)) {
      switch (msg.type) {
        case MSG_SAFETY_ESTOP_REQUESTED:
          Serial.println("[SAFETY_TASK] E-STOP requested!");
          break;
        case MSG_SAFETY_ALARM_TRIGGERED:
          Serial.println("[SAFETY_TASK] Alarm triggered!");
          break;
        default:
          break;
      }
    }
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[0].run_count++;
    task_stats[0].total_time_ms += task_time;
    task_stats[0].last_run_time_ms = task_time;
    if (task_time > task_stats[0].max_run_time_ms) {
      task_stats[0].max_run_time_ms = task_time;
    }
    
    // Feed watchdog (task is alive!)
    watchdogFeed("Safety");
    
    // Delay to maintain period (5ms = 200 Hz)
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_SAFETY));
  }
}

// ============================================================================
// TASK: MOTION (High Priority - Core 1)
// ============================================================================

void taskMotionCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskMotionFunction,
    "Motion",
    TASK_STACK_MOTION,
    NULL,
    TASK_PRIORITY_MOTION,
    &task_motion,
    CORE_1
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create Motion task!");
  } else {
    Serial.println("[TASKS] ✓ Motion task created (priority 22, core 1)");
    task_stats[1].handle = task_motion;
  }
}

void taskMotionFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t loop_count = 0;
  
  Serial.println("[MOTION_TASK] Started on core 1");
  watchdogTaskAdd("Motion");
  
  while (1) {
    uint32_t task_start = millis();
    loop_count++;
    
    // Motion control operations (every 10ms = 100 Hz)
    if (taskLockMutex(mutex_motion, 5)) {
      motionUpdate();  // Process motion, speed control, acceleration
      taskUnlockMutex(mutex_motion);
    }
    
    // Check for motion commands
    queue_message_t msg;
    while (taskReceiveMessage(queue_motion, &msg, 0)) {
      switch (msg.type) {
        case MSG_MOTION_START:
          Serial.println("[MOTION_TASK] Start command received");
          break;
        case MSG_MOTION_STOP:
          Serial.println("[MOTION_TASK] Stop command received");
          break;
        case MSG_MOTION_EMERGENCY_HALT:
          Serial.println("[MOTION_TASK] Emergency halt!");
          break;
        default:
          break;
      }
    }
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[1].run_count++;
    task_stats[1].total_time_ms += task_time;
    task_stats[1].last_run_time_ms = task_time;
    if (task_time > task_stats[1].max_run_time_ms) {
      task_stats[1].max_run_time_ms = task_time;
    }
    
    // Feed watchdog
    watchdogFeed("Motion");
    
    // Delay (10ms = 100 Hz)
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MOTION));
  }
}

// ============================================================================
// TASK: ENCODER (High Priority - Core 1)
// ============================================================================

void taskEncoderCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskEncoderFunction,
    "Encoder",
    TASK_STACK_ENCODER,
    NULL,
    TASK_PRIORITY_ENCODER,
    &task_encoder,
    CORE_1
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create Encoder task!");
  } else {
    Serial.println("[TASKS] ✓ Encoder task created (priority 20, core 1)");
    task_stats[2].handle = task_encoder;
  }
}

void taskEncoderFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  Serial.println("[ENCODER_TASK] Started on core 1");
  
  while (1) {
    uint32_t task_start = millis();
    
    // Encoder operations (every 20ms = 50 Hz)
    wj66Update();  // Read encoder, process feedback
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[2].run_count++;
    task_stats[2].total_time_ms += task_time;
    task_stats[2].last_run_time_ms = task_time;
    if (task_time > task_stats[2].max_run_time_ms) {
      task_stats[2].max_run_time_ms = task_time;
    }
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_ENCODER));
  }
}

// ============================================================================
// TASK: PLC COMMUNICATION (Medium-High Priority - Core 1)
// ============================================================================

void taskPlcCommCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskPlcCommFunction,
    "PLC_Comm",
    TASK_STACK_PLC_COMM,
    NULL,
    TASK_PRIORITY_PLC_COMM,
    &task_plc_comm,
    CORE_1
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create PLC_Comm task!");
  } else {
    Serial.println("[TASKS] ✓ PLC_Comm task created (priority 18, core 1)");
    task_stats[3].handle = task_plc_comm;
  }
}

void taskPlcCommFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  Serial.println("[PLC_TASK] Started on core 1");
  
  while (1) {
    uint32_t task_start = millis();
    
    // PLC communication (every 50ms = 20 Hz)
    if (taskLockMutex(mutex_i2c, 10)) {
      plcIfaceUpdate();  // I2C communication with Siemens S5
      taskUnlockMutex(mutex_i2c);
    }
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[3].run_count++;
    task_stats[3].total_time_ms += task_time;
    task_stats[3].last_run_time_ms = task_time;
    if (task_time > task_stats[3].max_run_time_ms) {
      task_stats[3].max_run_time_ms = task_time;
    }
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_PLC_COMM));
  }
}

// ============================================================================
// TASK: I2C MANAGER (Medium-High Priority - Core 1)
// ============================================================================

void taskI2cManagerCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskI2cManagerFunction,
    "I2C_Manager",
    TASK_STACK_I2C_MANAGER,
    NULL,
    TASK_PRIORITY_I2C_MANAGER,
    &task_i2c_manager,
    CORE_1
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create I2C_Manager task!");
  } else {
    Serial.println("[TASKS] ✓ I2C_Manager task created (priority 17, core 1)");
    task_stats[4].handle = task_i2c_manager;
  }
}

void taskI2cManagerFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  Serial.println("[I2C_TASK] Started on core 1");
  
  while (1) {
    uint32_t task_start = millis();
    
    // I2C management (every 50ms = 20 Hz)
    // Monitor bus health, statistics
    i2cGetStats();  // Collect diagnostics
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[4].run_count++;
    task_stats[4].total_time_ms += task_time;
    task_stats[4].last_run_time_ms = task_time;
    if (task_time > task_stats[4].max_run_time_ms) {
      task_stats[4].max_run_time_ms = task_time;
    }
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_I2C_MANAGER));
  }
}

// ============================================================================
// TASK: CLI (Medium Priority - Core 1)
// ============================================================================

void taskCliCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskCliFunction,
    "CLI",
    TASK_STACK_CLI,
    NULL,
    TASK_PRIORITY_CLI,
    &task_cli,
    CORE_1
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create CLI task!");
  } else {
    Serial.println("[TASKS] ✓ CLI task created (priority 15, core 1)");
    task_stats[5].handle = task_cli;
  }
}

void taskCliFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  Serial.println("[CLI_TASK] Started on core 1");
  
  while (1) {
    uint32_t task_start = millis();
    
    // CLI processing (every 100ms = 10 Hz)
    cliUpdate();  // Non-blocking user input processing
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[5].run_count++;
    task_stats[5].total_time_ms += task_time;
    task_stats[5].last_run_time_ms = task_time;
    if (task_time > task_stats[5].max_run_time_ms) {
      task_stats[5].max_run_time_ms = task_time;
    }
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_CLI));
  }
}

// ============================================================================
// TASK: FAULT LOGGING (Medium Priority - Core 1)
// ============================================================================

void taskFaultLogCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskFaultLogFunction,
    "Fault_Log",
    TASK_STACK_FAULT_LOG,
    NULL,
    TASK_PRIORITY_FAULT_LOG,
    &task_fault_log,
    CORE_1
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create Fault_Log task!");
  } else {
    Serial.println("[TASKS] ✓ Fault_Log task created (priority 14, core 1)");
    task_stats[6].handle = task_fault_log;
  }
}

void taskFaultLogFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  Serial.println("[FAULT_TASK] Started on core 1");
  
  while (1) {
    uint32_t task_start = millis();
    
    // Fault logging (every 500ms = 2 Hz)
    // Process fault queue and log to NVS
    queue_message_t msg;
    while (taskReceiveMessage(queue_fault, &msg, 0)) {
      // Log fault to NVS persistent storage
      switch (msg.type) {
        case MSG_FAULT_LOGGED:
          // Log to fault history
          break;
        case MSG_FAULT_CRITICAL:
          Serial.println("[FAULT_TASK] CRITICAL fault logged!");
          break;
        default:
          break;
      }
    }
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[6].run_count++;
    task_stats[6].total_time_ms += task_time;
    task_stats[6].last_run_time_ms = task_time;
    if (task_time > task_stats[6].max_run_time_ms) {
      task_stats[6].max_run_time_ms = task_time;
    }
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_FAULT_LOG));
  }
}

// ============================================================================
// TASK: MONITOR (Medium-Low Priority - Core 1)
// ============================================================================

void taskMonitorCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskMonitorFunction,
    "Monitor",
    TASK_STACK_MONITOR,
    NULL,
    TASK_PRIORITY_MONITOR,
    &task_monitor,
    CORE_1
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create Monitor task!");
  } else {
    Serial.println("[TASKS] ✓ Monitor task created (priority 12, core 1)");
    task_stats[7].handle = task_monitor;
  }
}

void taskMonitorFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  Serial.println("[MONITOR_TASK] Started on core 1");
  
  while (1) {
    uint32_t task_start = millis();
    
    // Diagnostics (every 1000ms = 1 Hz)
    // Collect and display system statistics
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[7].run_count++;
    task_stats[7].total_time_ms += task_time;
    task_stats[7].last_run_time_ms = task_time;
    if (task_time > task_stats[7].max_run_time_ms) {
      task_stats[7].max_run_time_ms = task_time;
    }
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MONITOR));
  }
}

// ============================================================================
// TASK: LCD DISPLAY (Low Priority - Core 1)
// ============================================================================

void taskLcdCreate() {
  BaseType_t result = xTaskCreatePinnedToCore(
    taskLcdFunction,
    "LCD",
    TASK_STACK_LCD,
    NULL,
    TASK_PRIORITY_LCD,
    &task_lcd,
    CORE_1
  );
  
  if (result != pdPASS) {
    Serial.println("[TASKS] ERROR: Failed to create LCD task!");
  } else {
    Serial.println("[TASKS] ✓ LCD task created (priority 10, core 1)");
    task_stats[8].handle = task_lcd;
  }
}

void taskLcdFunction(void* parameter) {
  TickType_t last_wake = xTaskGetTickCount();
  
  Serial.println("[LCD_TASK] Started on core 1");
  
  while (1) {
    uint32_t task_start = millis();
    
    // LCD updates (every 500ms = 2 Hz)
    lcdInterfaceUpdate();  // Update display
    
    // Update stats
    uint32_t task_time = millis() - task_start;
    task_stats[8].run_count++;
    task_stats[8].total_time_ms += task_time;
    task_stats[8].last_run_time_ms = task_time;
    if (task_time > task_stats[8].max_run_time_ms) {
      task_stats[8].max_run_time_ms = task_time;
    }
    
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_LCD));
  }
}

// ============================================================================
// STATISTICS & DIAGNOSTICS
// ============================================================================

void taskShowStats() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║            FREERTOS TASK STATISTICS                        ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("[TASKS] Task               Runs      Avg(ms)   Max(ms)   CPU%");
  Serial.println("[TASKS] ──────────────────────────────────────────────────────");
  
  uint32_t total_time = 0;
  for (int i = 0; i < stats_count; i++) {
    total_time += task_stats[i].total_time_ms;
  }
  
  for (int i = 0; i < stats_count; i++) {
    float avg_time = (task_stats[i].run_count > 0) ? 
      (float)task_stats[i].total_time_ms / task_stats[i].run_count : 0;
    float cpu_percent = (total_time > 0) ? 
      ((float)task_stats[i].total_time_ms / total_time) * 100.0 : 0;
    
    Serial.print("[TASKS] ");
    Serial.print(task_stats[i].name);
    Serial.print("           ");
    Serial.print(task_stats[i].run_count);
    Serial.print("       ");
    Serial.print(avg_time, 2);
    Serial.print("      ");
    Serial.print(task_stats[i].max_run_time_ms);
    Serial.print("      ");
    Serial.print(cpu_percent, 1);
    Serial.println("%");
  }
  
  Serial.println();
}

void taskShowAllTasks() {
  Serial.println("\n╔════════════════════════════════════════════════════════════╗");
  Serial.println("║            FREERTOS TASK INFORMATION                      ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝\n");
  
  for (int i = 0; i < stats_count; i++) {
    if (task_stats[i].handle == NULL) continue;
    
    UBaseType_t priority = uxTaskPriorityGet(task_stats[i].handle);
    UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(task_stats[i].handle);
    
    Serial.print("[TASKS] ");
    Serial.print(task_stats[i].name);
    Serial.print(" - Priority: ");
    Serial.print(priority);
    Serial.print(", Stack Remaining: ");
    Serial.print(stack_high_water * 4);  // Convert words to bytes
    Serial.println(" bytes");
  }
  
  Serial.println();
}

uint8_t taskGetCpuUsage() {
  uint32_t total_time = 0;
  uint32_t elapsed = millis() - boot_time_ms;
  
  if (elapsed == 0) return 0;
  
  for (int i = 0; i < stats_count; i++) {
    total_time += task_stats[i].last_run_time_ms;
  }
  
  return (uint8_t)((total_time * 100) / elapsed);
}

uint32_t taskGetUptime() {
  return (millis() - boot_time_ms) / 1000;
}
