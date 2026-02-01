#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// ============================================================================
// TASK PRIORITY LEVELS
// ============================================================================

#define TASK_PRIORITY_SAFETY 24
#define TASK_PRIORITY_MOTION 22
#define TASK_PRIORITY_ENCODER 20
#define TASK_PRIORITY_PLC_COMM 18
#define TASK_PRIORITY_I2C_MANAGER 17
#define TASK_PRIORITY_CLI 15
#define TASK_PRIORITY_FAULT_LOG 14
#define TASK_PRIORITY_MONITOR 12
#define TASK_PRIORITY_TELEMETRY 11 // PHASE 5.4: Background telemetry on Core 0
#define TASK_PRIORITY_LCD_FORMAT                                               \
  10                        // PHASE 5.4: LCD string formatting on Core 0
#define TASK_PRIORITY_LCD 9 // PHASE 5.4: Display only, reduced priority
#define TASK_PRIORITY_IDLE 1

// ============================================================================
// TASK STACK SIZES
// ============================================================================
// NOTE: Stack sizes increased to prevent Guru Meditation Errors from:
//       - Heavy snprintf() operations (stack-intensive string formatting)
//       - JSON serialization (ArduinoJson allocates on stack for small docs)
//       - Deep call chains in complex state machines

#define TASK_STACK_SAFETY                                                      \
  4096 // INCREASED: 3KB->4KB (Fix low stack warning on S3)
#define TASK_STACK_MOTION                                                      \
  4096 // Keep 4K for motion planner
#define TASK_STACK_ENCODER                                                     \
  3072 // REDUCED: 4KB->3KB
#define TASK_STACK_PLC_COMM 2048
#define TASK_STACK_I2C_MANAGER                                                 \
  3072
#define TASK_STACK_CLI                                                         \
  4096 // TUNED: 8KB->4KB (HWM 5.5KB unused)
#define TASK_STACK_FAULT_LOG 3072
#define TASK_STACK_MONITOR 4096 // TUNED: 6KB->4KB (HWM 732 bytes was too low at 3KB)
#define TASK_STACK_TELEMETRY                                                   \
  4096 // TUNED: 6KB->4KB (HWM 3.4KB unused)
#define TASK_STACK_LCD_FORMAT                                                  \
  4096 // INCREASED: 3KB->4KB (Fix low stack warning)
#define TASK_STACK_LCD                                                         \
  4096 // KEPT: 4KB (HWM 932 bytes was too low at 3KB)
#define TASK_STACK_BOOT 2048

// WARNING: AsyncWebServer handlers create JsonDocument on stack!
// If web API returns become complex, increase CONFIG_ASYNC_TCP_TASK_STACK_SIZE
// in platformio.ini or switch to heap-allocated JsonDocuments in web_server.cpp

// ============================================================================
// TASK CORE AFFINITY
// ============================================================================

#define CORE_0 0
#define CORE_1 1
#define CORE_BOTH -1

// ============================================================================
// TASK PERIOD/FREQUENCY (in milliseconds)
// ============================================================================

#define TASK_PERIOD_SAFETY 5
#define TASK_PERIOD_MOTION 10
#define TASK_PERIOD_ENCODER 20
#define TASK_PERIOD_PLC_COMM 50
#define TASK_PERIOD_I2C_MANAGER 50
#define TASK_PERIOD_CLI 100
#define TASK_PERIOD_FAULT_LOG 500
#define TASK_PERIOD_MONITOR 1000
#define TASK_PERIOD_TELEMETRY                                                  \
  50 // PHASE 5.4: High-frequency base (20Hz) to ensure stable 10Hz DRO updates
#define TASK_PERIOD_LCD_FORMAT                                                 \
  200 // PHASE 5.4: Format strings same rate as display
// PHASE 3.1: Increased from 100ms to 20ms (50Hz) to match encoder update
// frequency Reduces position display staleness from ±100ms to ±20ms
// AUDIT: Reduced to 200ms (5Hz) to prevent I2C bus contention with Motion task
#define TASK_PERIOD_LCD 200

// ============================================================================
// ADAPTIVE I2C TIMEOUT CONFIGURATION
// ============================================================================

// PHASE 5.4: Optimized I2C timeout for dual-core performance
// At low CPU: 50ms (system idle, I2C operations should complete quickly)
// At high CPU: 100ms (reduced from 500ms to prevent Safety task blocking)
// Formula: timeout_ms = base_ms + (cpu_usage_percent * scale_factor)
// JUSTIFICATION: Safety task should never wait >100ms for I2C (5ms cycle = 20
// cycles max) Under load, I2C ops complete quickly due to 100kHz bus speed. If
// timeout needed, indicates I2C bus/device failure → better to fail fast than
// hang system.
#define I2C_TIMEOUT_BASE_MS 50
#define I2C_TIMEOUT_MAX_MS 150
#define I2C_TIMEOUT_SCALE 0.5f

// ============================================================================
// MESSAGE QUEUE DEFINITIONS
// ============================================================================

// PHASE 5.10: Renamed to QUEUE_DATA_SIZE to clarify it's the payload size, not full message
#define QUEUE_DATA_SIZE 96
#define QUEUE_LEN_MOTION 10
#define QUEUE_LEN_SAFETY 20
#define QUEUE_LEN_ENCODER 10
#define QUEUE_LEN_PLC 10
// PHASE 2 FIX: Increased from 50 to 150 to prevent loss of critical logs
// Rationale: Under fault conditions, system can generate 20+ faults/sec.
// With 50 items, queue fills in 2.5s and critical logs are dropped.
// With 150 items, provides 7.5s buffer for fault processing.
#define QUEUE_LEN_FAULT 150
#define QUEUE_LEN_DISPLAY 10

// ============================================================================
// INTER-TASK COMMUNICATION TYPES
// ============================================================================

typedef enum {
  MSG_SAFETY_ESTOP_REQUESTED,
  MSG_SAFETY_ESTOP_CLEAR,
  MSG_SAFETY_ALARM_TRIGGERED,
  MSG_SAFETY_ALARM_CLEARED,
  MSG_MOTION_START,
  MSG_MOTION_STOP,
  MSG_MOTION_EMERGENCY_HALT,
  MSG_ENCODER_DATA_READY,
  MSG_ENCODER_ERROR,
  MSG_ENCODER_CALIBRATION_DONE,
  MSG_PLC_COMMAND_RECEIVED,
  MSG_PLC_STATUS_UPDATE,
  MSG_PLC_ERROR,
  MSG_FAULT_LOGGED,
  MSG_FAULT_CRITICAL,
  MSG_DISPLAY_UPDATE,
} message_type_t;

typedef struct {
  message_type_t type;
  uint32_t param1;
  uint32_t param2;
  uint8_t data[QUEUE_DATA_SIZE];
  uint32_t timestamp;
} queue_message_t;

typedef struct {
  TaskHandle_t handle;
  const char *name;
  UBaseType_t priority;
  uint32_t run_count;
  uint32_t total_time_ms;
  uint32_t last_run_time_ms;
  uint32_t max_run_time_ms;
  uint16_t stack_high_water;
} task_stats_t;

int taskGetStatsCount();
task_stats_t *taskGetStatsArray();

void taskManagerInit();
void taskManagerStart();

void taskSafetyFunction(void *parameter);
void taskMotionFunction(void *parameter);
void taskEncoderFunction(void *parameter);
void taskPlcCommFunction(void *parameter);
void taskI2cManagerFunction(void *parameter);
void taskCliFunction(void *parameter);
void taskFaultLogFunction(void *parameter);
void taskMonitorFunction(void *parameter);
void taskTelemetryFunction(
    void *parameter); // PHASE 5.4: Background telemetry collection
void taskLcdFormatterFunction(
    void *parameter); // PHASE 5.4: LCD string formatting
void taskLcdFunction(void *parameter);

void taskSafetyCreate();
void taskMotionCreate();
void taskEncoderCreate();
void taskPlcCommCreate();
void taskI2cManagerCreate();
void taskCliCreate();
void taskFaultLogCreate();
void taskMonitorCreate();
void taskTelemetryCreate();    // PHASE 5.4: Background telemetry collection
void taskLcdFormatterCreate(); // PHASE 5.4: LCD string formatting
void taskLcdCreate();

QueueHandle_t taskGetMotionQueue();
QueueHandle_t taskGetSafetyQueue();
QueueHandle_t taskGetEncoderQueue();
QueueHandle_t taskGetPlcQueue();
QueueHandle_t taskGetFaultQueue();
QueueHandle_t taskGetDisplayQueue();

bool taskSendMessage(QueueHandle_t queue, const queue_message_t *msg);
bool taskReceiveMessage(QueueHandle_t queue, queue_message_t *msg,
                        uint32_t timeout_ms);

// NEW: Direct Task Notification for high-speed signaling
void taskSignalMotionUpdate();

SemaphoreHandle_t
taskGetI2cMutex(); // DEPRECATED: Use separate board/PLC mutexes
SemaphoreHandle_t
taskGetI2cBoardMutex(); // PHASE 5.4: Board inputs (buttons, etc.)
SemaphoreHandle_t
taskGetI2cPlcMutex(); // PHASE 5.4: PLC interface (speed, CONSENSO)
SemaphoreHandle_t taskGetLcdMutex(); // LCD display (0x27)
SemaphoreHandle_t taskGetMotionMutex();
SemaphoreHandle_t taskGetBufferMutex(); // NEW: Separate buffer mutex
bool taskLockMutex(SemaphoreHandle_t mutex, uint32_t timeout_ms);
void taskUnlockMutex(SemaphoreHandle_t mutex);

void taskShowStats();
void taskShowAllTasks();
uint8_t taskGetCpuUsage();
uint32_t taskGetUptime();

// PHASE 2.5: Adaptive I2C timeout based on CPU load
// Returns timeout in milliseconds, scaled from base to max based on current CPU
// usage
uint32_t taskGetAdaptiveI2cTimeout();

// Memory Tuning
void taskUpdateStackUsage();

#endif
