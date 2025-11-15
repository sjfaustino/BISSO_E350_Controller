#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ============================================================================
// TASK PRIORITY LEVELS (Higher number = higher priority)
// ============================================================================
// FreeRTOS on ESP32: 0 (idle) to 25 (max), configMAX_PRIORITIES typically 25

#define TASK_PRIORITY_SAFETY      24  // Critical - must run immediately
#define TASK_PRIORITY_MOTION      22  // High - real-time motion control
#define TASK_PRIORITY_ENCODER     20  // High - encoder feedback processing
#define TASK_PRIORITY_PLC_COMM    18  // Medium-high - PLC interface
#define TASK_PRIORITY_I2C_MANAGER 17  // Medium-high - I2C bus management
#define TASK_PRIORITY_CLI         15  // Medium - user interaction
#define TASK_PRIORITY_FAULT_LOG   14  // Medium - fault logging
#define TASK_PRIORITY_MONITOR     12  // Medium-low - diagnostics
#define TASK_PRIORITY_LCD         10  // Low - display updates
#define TASK_PRIORITY_IDLE        1   // Idle task (system)

// ============================================================================
// TASK STACK SIZES (in words, ESP32 = 4 bytes/word)
// ============================================================================
// Available SRAM: ~327 KB total
// Recommendation: Tasks get 2-4 KB (512-1024 words) each

#define TASK_STACK_SAFETY        2048   // Safety-critical, needs space for exceptions
#define TASK_STACK_MOTION        2048   // Motion calculations
#define TASK_STACK_ENCODER       1024   // Encoder reading
#define TASK_STACK_PLC_COMM      1024   // I2C communication
#define TASK_STACK_I2C_MANAGER   1024   // I2C management
#define TASK_STACK_CLI           2048   // CLI parsing and commands
#define TASK_STACK_FAULT_LOG     1024   // Fault logging
#define TASK_STACK_MONITOR       1024   // Diagnostics
#define TASK_STACK_LCD           1024   // LCD updates
#define TASK_STACK_BOOT          2048   // Boot sequence

// ============================================================================
// TASK CORE AFFINITY
// ============================================================================
// ESP32-S3 has 2 cores: Core 0 (WiFi/BLE), Core 1 (Application)
// Most tasks pinned to Core 1 for consistency

#define CORE_0                   0      // WiFi/BLE core (avoid)
#define CORE_1                   1      // Application core (use this)
#define CORE_BOTH               -1      // Both cores (FreeRTOS decides)

// ============================================================================
// TASK PERIOD/FREQUENCY
// ============================================================================
// How often each task should run (in milliseconds)

#define TASK_PERIOD_SAFETY       5      // 200 Hz - safety checks every 5ms
#define TASK_PERIOD_MOTION       10     // 100 Hz - motion control every 10ms
#define TASK_PERIOD_ENCODER      20     // 50 Hz - encoder every 20ms
#define TASK_PERIOD_PLC_COMM     50     // 20 Hz - PLC communication every 50ms
#define TASK_PERIOD_I2C_MANAGER  50     // 20 Hz - I2C management every 50ms
#define TASK_PERIOD_CLI          100    // 10 Hz - CLI updates every 100ms
#define TASK_PERIOD_FAULT_LOG    500    // 2 Hz - fault logging every 500ms
#define TASK_PERIOD_MONITOR      1000   // 1 Hz - diagnostics every 1 second
#define TASK_PERIOD_LCD          500    // 2 Hz - display updates every 500ms

// ============================================================================
// MESSAGE QUEUE DEFINITIONS
// ============================================================================

// Queue item size (bytes)
#define QUEUE_ITEM_SIZE          64     // Max message size

// Queue lengths (number of items)
#define QUEUE_LEN_MOTION         10     // Motion commands
#define QUEUE_LEN_SAFETY         20     // Safety events
#define QUEUE_LEN_ENCODER        10     // Encoder data
#define QUEUE_LEN_PLC            10     // PLC messages
#define QUEUE_LEN_FAULT          50     // Fault messages
#define QUEUE_LEN_DISPLAY        10     // Display updates

// ============================================================================
// INTER-TASK COMMUNICATION TYPES
// ============================================================================

typedef enum {
  // Safety events
  MSG_SAFETY_ESTOP_REQUESTED,
  MSG_SAFETY_ESTOP_CLEAR,
  MSG_SAFETY_ALARM_TRIGGERED,
  MSG_SAFETY_ALARM_CLEARED,
  
  // Motion events
  MSG_MOTION_START,
  MSG_MOTION_STOP,
  MSG_MOTION_EMERGENCY_HALT,
  
  // Encoder events
  MSG_ENCODER_DATA_READY,
  MSG_ENCODER_ERROR,
  MSG_ENCODER_CALIBRATION_DONE,
  
  // PLC events
  MSG_PLC_COMMAND_RECEIVED,
  MSG_PLC_STATUS_UPDATE,
  MSG_PLC_ERROR,
  
  // Fault events
  MSG_FAULT_LOGGED,
  MSG_FAULT_CRITICAL,
  
  // Display events
  MSG_DISPLAY_UPDATE,
} message_type_t;

typedef struct {
  message_type_t type;
  uint32_t param1;
  uint32_t param2;
  uint8_t data[QUEUE_ITEM_SIZE];
  uint32_t timestamp;
} queue_message_t;

// ============================================================================
// TASK STATISTICS
// ============================================================================

typedef struct {
  TaskHandle_t handle;
  const char* name;
  UBaseType_t priority;
  uint32_t run_count;
  uint32_t total_time_ms;
  uint32_t last_run_time_ms;
  uint32_t max_run_time_ms;
  uint16_t stack_high_water;
} task_stats_t;

// ============================================================================
// TASK MANAGER INITIALIZATION
// ============================================================================

void taskManagerInit();
void taskManagerStart();

// ============================================================================
// INDIVIDUAL TASK CREATION
// ============================================================================

// Safety task - E-stop, interlocks, alarms
void taskSafetyCreate();
void taskSafetyFunction(void* parameter);

// Motion control task - Axis movement, speed control
void taskMotionCreate();
void taskMotionFunction(void* parameter);

// Encoder task - Read position feedback
void taskEncoderCreate();
void taskEncoderFunction(void* parameter);

// PLC communication task - I2C to Siemens S5
void taskPlcCommCreate();
void taskPlcCommFunction(void* parameter);

// I2C bus manager task - Recovery, diagnostics
void taskI2cManagerCreate();
void taskI2cManagerFunction(void* parameter);

// CLI task - User input processing
void taskCliCreate();
void taskCliFunction(void* parameter);

// Fault logging task - Log to NVS
void taskFaultLogCreate();
void taskFaultLogFunction(void* parameter);

// Monitor task - Diagnostics, statistics
void taskMonitorCreate();
void taskMonitorFunction(void* parameter);

// LCD display task - Screen updates
void taskLcdCreate();
void taskLcdFunction(void* parameter);

// ============================================================================
// INTER-TASK COMMUNICATION FUNCTIONS
// ============================================================================

// Queue management
QueueHandle_t taskGetMotionQueue();
QueueHandle_t taskGetSafetyQueue();
QueueHandle_t taskGetEncoderQueue();
QueueHandle_t taskGetPlcQueue();
QueueHandle_t taskGetFaultQueue();
QueueHandle_t taskGetDisplayQueue();

// Send messages between tasks
bool taskSendMessage(QueueHandle_t queue, const queue_message_t* msg);
bool taskReceiveMessage(QueueHandle_t queue, queue_message_t* msg, uint32_t timeout_ms);

// ============================================================================
// SYNCHRONIZATION PRIMITIVES
// ============================================================================

// Mutexes for shared resources
SemaphoreHandle_t taskGetConfigMutex();        // Protect configuration
SemaphoreHandle_t taskGetI2cMutex();          // Protect I2C bus
SemaphoreHandle_t taskGetMotionMutex();       // Protect motion state

// Lock/unlock with timeout
bool taskLockMutex(SemaphoreHandle_t mutex, uint32_t timeout_ms);
void taskUnlockMutex(SemaphoreHandle_t mutex);

// ============================================================================
// TASK CONTROL
// ============================================================================

void taskSuspend(TaskHandle_t task);
void taskResume(TaskHandle_t task);
void taskDelete(TaskHandle_t task);

// ============================================================================
// TASK STATISTICS & MONITORING
// ============================================================================

void taskShowStats();
void taskShowStack();
void taskShowQueues();
void taskShowCpuUsage();

// Get stats for specific task
bool taskGetStats(const char* task_name, task_stats_t* stats);

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void taskShowAllTasks();
void taskCheckDeadlock();
void taskShowTaskTrace();

// Real-time CPU usage monitoring
uint8_t taskGetCpuUsage();                    // 0-100%
uint32_t taskGetUptime();                     // Seconds since boot

#endif
