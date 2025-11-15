#ifndef SYSTEM_CONSTANTS_H
#define SYSTEM_CONSTANTS_H

// ============================================================================
// MOTION CONTROL CONSTANTS
// ============================================================================

// Position scaling: internal units to physical units
#define MOTION_POSITION_SCALE_FACTOR 1000        // 1000 internal units = 1mm

// Motion timing
#define MOTION_UPDATE_INTERVAL_MS 10              // Update frequency: 100Hz
#define MOTION_STALL_TIMEOUT_MS 2000              // Detect stalled motion after 2 seconds
#define MOTION_MAX_SPEED 100.0f                   // Maximum speed: 100 mm/s
#define MOTION_ACCELERATION 5.0f                  // Acceleration rate: 5 mm/s²

// ============================================================================
// ENCODER COMMUNICATION CONSTANTS
// ============================================================================

#define WJ66_BAUD 9600                            // WJ66 serial baud rate
#define WJ66_READ_INTERVAL_MS 50                  // Poll interval: 20Hz
#define ENCODER_COMMAND_INTERVAL_MS 500           // Command retry interval
#define ENCODER_TIMEOUT_MS 1000                   // Data timeout: 1 second
#define ENCODER_BUFFER_SIZE 64                    // Serial buffer max size
#define ENCODER_PPR 20                            // Pulses per revolution

// ============================================================================
// PLC COMMUNICATION CONSTANTS
// ============================================================================

#define PLC_COMM_INTERVAL_MS 1                    // 1kHz communication rate
#define PLC_TIMEOUT_MS 100                        // Timeout for PLC response
#define PLC_MAX_RETRIES 3                         // Retry attempts on failure

// ============================================================================
// I2C BUS CONSTANTS
// ============================================================================

#define I2C_STANDARD_FREQUENCY 100000             // 100 kHz I2C clock
#define I2C_BUS_CHECK_INTERVAL_MS 500             // Bus health check interval
#define I2C_RECOVERY_TIMEOUT_MS 100               // Bus recovery attempt timeout
#define I2C_TRANSACTION_TIMEOUT_MS 50             // Individual transaction timeout

// ============================================================================
// SAFETY SYSTEM CONSTANTS
// ============================================================================

#define SAFETY_STALL_CHECK_INTERVAL_MS 100        // Check for stalled motion
#define SAFETY_PLC_TIMEOUT_MS 500                 // PLC communication timeout
#define SAFETY_INTERLOCK_CHECK_MS 50              // Safety interlock validation
#define EMERGENCY_STOP_TIMEOUT_MS 100             // E-stop activation timeout

// ============================================================================
// WATCHDOG CONSTANTS
// ============================================================================

#define WATCHDOG_TIMEOUT_MS 8000                  // ESP32 max watchdog timeout
#define WATCHDOG_FEED_INTERVAL_MS 4000            // Feed watchdog every 4 seconds
#define TASK_MONITOR_INTERVAL_MS 1000             // Monitor task execution time
#define TASK_EXECUTION_WARNING_MS 500              // Warning threshold for slow tasks

// Task timeout thresholds (must be less than watchdog timeout)
#define TASK_TIMEOUT_SAFETY_MS 100                // Safety task max execution
#define TASK_TIMEOUT_MOTION_MS 50                 // Motion task max execution
#define TASK_TIMEOUT_ENCODER_MS 100               // Encoder task max execution
#define TASK_TIMEOUT_PLC_COMM_MS 10               // PLC comm max execution
#define TASK_TIMEOUT_I2C_MANAGER_MS 50            // I2C task max execution
#define TASK_TIMEOUT_CLI_MS 500                   // CLI task max execution
#define TASK_TIMEOUT_FAULT_LOG_MS 50              // Fault log task max execution
#define TASK_TIMEOUT_MONITOR_MS 100               // Monitor task max execution
#define TASK_TIMEOUT_LCD_MS 50                    // LCD task max execution

// ============================================================================
// FAULT LOGGING CONSTANTS
// ============================================================================

#define FAULT_LOG_SIZE 100                        // Max fault history entries
#define FAULT_LOG_CLEANUP_INTERVAL_MS 10000       // Cleanup old faults every 10s
#define FAULT_LOG_TIMEOUT_MS 100                  // Timeout for fault logging

// ============================================================================
// SERIAL COMMUNICATION CONSTANTS
// ============================================================================

#define SERIAL_BAUD_RATE 115200                   // Main serial port baud
#define SERIAL_RX_BUFFER_SIZE 256                 // Serial receive buffer
#define SERIAL_TX_BUFFER_SIZE 256                 // Serial transmit buffer

// ============================================================================
// CONFIGURATION STORAGE CONSTANTS
// ============================================================================

#define NVS_CONFIG_SAVE_INTERVAL_MS 5000          // Auto-save config every 5s
#define CONFIG_VERSION_CURRENT 4                  // Current configuration schema
#define CONFIG_MAX_SIZE 4096                      // Maximum config size

// ============================================================================
// MEMORY MANAGEMENT CONSTANTS
// ============================================================================

#define MEMORY_WARNING_THRESHOLD_KB 64            // Warn if free < 64KB
#define MEMORY_CRITICAL_THRESHOLD_KB 32           // Critical if free < 32KB
#define MEMORY_CHECK_INTERVAL_MS 1000             // Check memory every second

// ============================================================================
// WEB SERVER CONSTANTS
// ============================================================================

#define WEB_SERVER_PORT 80                        // HTTP port
#define WEB_REQUEST_TIMEOUT_MS 5000               // Request timeout
#define WEB_BUFFER_SIZE 1024                      // Request/response buffer

// ============================================================================
// LCD DISPLAY CONSTANTS
// ============================================================================

#define LCD_REFRESH_INTERVAL_MS 500               // Refresh LCD every 500ms
#define LCD_BACKLIGHT_TIMEOUT_MS 300000           // Backlight off after 5 min

// ============================================================================
// RESULT CODE ENUMERATION
// ============================================================================

typedef enum {
  RESULT_OK = 0,                                  // Operation successful
  RESULT_ERROR = 1,                               // General error
  RESULT_TIMEOUT = 2,                             // Operation timeout
  RESULT_NACK = 3,                                // NACK received
  RESULT_BUS_ERROR = 4,                           // Bus error
  RESULT_INVALID_PARAM = 5,                       // Invalid parameter
  RESULT_NOT_READY = 6,                           // Device not ready
  RESULT_BUSY = 7,                                // Device busy
  RESULT_UNKNOWN = 8                              // Unknown error
} result_t;

const char* resultToString(result_t result);

#endif // SYSTEM_CONSTANTS_H
