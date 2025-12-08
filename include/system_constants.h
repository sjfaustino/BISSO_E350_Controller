/**
 * @file system_constants.h
 * @brief System-wide constants and pin definitions
 * @project Gemini v1.0.0
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

#ifndef SYSTEM_CONSTANTS_H
#define SYSTEM_CONSTANTS_H

// ============================================================================
// MOTION APPROACH MODES
// ============================================================================
#define APPROACH_MODE_FIXED   0  // Use fixed distance from config
#define APPROACH_MODE_DYNAMIC 1  // Calculate distance based on speed/accel

// ============================================================================
// HARDWARE PIN CONFIGURATION
// ============================================================================
#define PIN_I2C_SDA 4   
#define PIN_I2C_SCL 5   

// ============================================================================
// MOTION CONTROL CONSTANTS
// ============================================================================

// Speed Profile Output via PCF8574 I2C Expander (KC868-A16)
#define SPEED_PROFILE_BIT_0_PCF_PIN 0   
#define SPEED_PROFILE_BIT_1_PCF_PIN 1   

// Position scaling
#define MOTION_POSITION_SCALE_FACTOR 1000        
#define MOTION_POSITION_SCALE_FACTOR_DEG 1000    

// Motion timing
#define MOTION_UPDATE_INTERVAL_MS 10              
#define MOTION_STALL_TIMEOUT_MS 2000              
#define MOTION_MAX_SPEED 200.0f                   
#define MOTION_ACCELERATION 5.0f                  
#define MOTION_MIN_SPEED_MM_S 0.1f                
#define MOTION_MAX_SPEED_MM_S 200.0f              

// ============================================================================
// ENCODER COMMUNICATION CONSTANTS
// ============================================================================
#define WJ66_BAUD 9600                            
#define WJ66_READ_INTERVAL_MS 50                  
#define ENCODER_COMMAND_INTERVAL_MS 500           
#define ENCODER_TIMEOUT_MS 1000                   
#define ENCODER_BUFFER_SIZE 64                    
#define ENCODER_PPR 20                            

// ============================================================================
// PLC COMMUNICATION CONSTANTS
// ============================================================================
#define PLC_COMM_INTERVAL_MS 50                   
#define PLC_TIMEOUT_MS 100                        
#define PLC_MAX_RETRIES 3                         

// ============================================================================
// I2C BUS CONSTANTS
// ============================================================================
#define I2C_STANDARD_FREQUENCY 100000             
#define I2C_BUS_CHECK_INTERVAL_MS 500             
#define I2C_RECOVERY_TIMEOUT_MS 100               
#define I2C_TRANSACTION_TIMEOUT_MS 50             

// ============================================================================
// SAFETY SYSTEM CONSTANTS
// ============================================================================
#define SAFETY_STALL_CHECK_INTERVAL_MS 100        
#define SAFETY_PLC_TIMEOUT_MS 500                 
#define SAFETY_INTERLOCK_CHECK_MS 50              
#define EMERGENCY_STOP_TIMEOUT_MS 100             

// ============================================================================
// WATCHDOG CONSTANTS
// ============================================================================
#define WATCHDOG_TIMEOUT_SEC 10                   
#define WATCHDOG_FEED_INTERVAL_MS 4000            
#define TASK_MONITOR_INTERVAL_MS 1000             
#define TASK_EXECUTION_WARNING_MS 500             

// ============================================================================
// FAULT LOGGING CONSTANTS
// ============================================================================
#define FAULT_LOG_SIZE 100                        
#define FAULT_LOG_CLEANUP_INTERVAL_MS 10000       
#define FAULT_LOG_TIMEOUT_MS 100                  

// ============================================================================
// BOOT & VALIDATION CONSTANTS
// ============================================================================
#define MAX_BOOT_SUBSYSTEMS 15                    

// ============================================================================
// SERIAL COMMUNICATION CONSTANTS
// ============================================================================
#define SERIAL_BAUD_RATE 115200                   
#define SERIAL_RX_BUFFER_SIZE 256                 
#define SERIAL_TX_BUFFER_SIZE 256                 

// ============================================================================
// CONFIGURATION STORAGE CONSTANTS
// ============================================================================
#define NVS_CONFIG_SAVE_INTERVAL_MS 5000          
#define CONFIG_VERSION_CURRENT 4                  
#define CONFIG_MAX_SIZE 4096                      

// ============================================================================
// WEB/MEMORY CONSTANTS
// ============================================================================
#define WEB_MAX_JOG_DISTANCE_MM 500               
#define WEB_MAX_JOG_SPEED_MM_S 100                
#define MEMORY_WARNING_THRESHOLD_BYTES 65536      
#define MEMORY_CRITICAL_THRESHOLD_BYTES 32768     
#define MEMORY_CHECK_INTERVAL_MS 1000             
#define WEB_SERVER_PORT 80                        
#define WEB_REQUEST_TIMEOUT_MS 5000               
#define WEB_BUFFER_SIZE 1024                      
#define LCD_REFRESH_INTERVAL_MS 100               
#define LCD_BACKLIGHT_TIMEOUT_MS 300000           

typedef enum {
  RESULT_OK = 0,                                  
  RESULT_ERROR = 1,                               
  RESULT_TIMEOUT = 2,                             
  RESULT_NACK = 3,                                
  RESULT_BUS_ERROR = 4,                           
  RESULT_INVALID_PARAM = 5,                       
  RESULT_NOT_READY = 6,                           
  RESULT_BUSY = 7,                                
  RESULT_UNKNOWN = 8                              
} result_t;

const char* resultToString(result_t result);

// ============================================================================
// COMPILE-TIME SAFETY CHECKS
// ============================================================================

// Validate motion timing constraints
static_assert(MOTION_UPDATE_INTERVAL_MS >= 1, "Motion update interval too fast (min 1ms)!");
static_assert(MOTION_UPDATE_INTERVAL_MS <= 100, "Motion update interval too slow (max 100ms)!");
static_assert(MOTION_STALL_TIMEOUT_MS > MOTION_UPDATE_INTERVAL_MS,
              "Stall timeout must be greater than update interval!");
static_assert(SAFETY_STALL_CHECK_INTERVAL_MS < MOTION_STALL_TIMEOUT_MS,
              "Stall check interval must be faster than stall timeout!");

// Validate watchdog timing
static_assert(WATCHDOG_FEED_INTERVAL_MS < (WATCHDOG_TIMEOUT_SEC * 1000),
              "Watchdog feed interval must be less than watchdog timeout!");
static_assert(WATCHDOG_FEED_INTERVAL_MS >= 1000,
              "Watchdog feed interval should be at least 1 second!");

// Validate encoder timing
static_assert(WJ66_READ_INTERVAL_MS > 0, "Encoder read interval must be positive!");
static_assert(ENCODER_TIMEOUT_MS > WJ66_READ_INTERVAL_MS,
              "Encoder timeout must be greater than read interval!");

// Validate PLC communication timing
static_assert(PLC_TIMEOUT_MS > PLC_COMM_INTERVAL_MS,
              "PLC timeout must be greater than communication interval!");
static_assert(PLC_MAX_RETRIES > 0, "PLC must have at least 1 retry attempt!");
static_assert(PLC_MAX_RETRIES <= 10, "Too many PLC retries (max 10)!");

// Validate I2C timing
static_assert(I2C_TRANSACTION_TIMEOUT_MS <= I2C_RECOVERY_TIMEOUT_MS,
              "I2C transaction timeout should not exceed recovery timeout!");

// Validate buffer sizes
static_assert(ENCODER_BUFFER_SIZE >= 32, "Encoder buffer too small (min 32 bytes)!");
static_assert(ENCODER_BUFFER_SIZE <= 256, "Encoder buffer too large (max 256 bytes)!");
static_assert(SERIAL_RX_BUFFER_SIZE >= 128, "Serial RX buffer too small (min 128 bytes)!");
static_assert(SERIAL_TX_BUFFER_SIZE >= 128, "Serial TX buffer too small (min 128 bytes)!");

// Validate fault logging
static_assert(FAULT_LOG_SIZE > 0, "Fault log must have non-zero size!");
static_assert(FAULT_LOG_SIZE <= 1000, "Fault log too large (max 1000 entries)!");

// Validate configuration
static_assert(CONFIG_MAX_SIZE >= 1024, "Config storage too small (min 1KB)!");
static_assert(CONFIG_MAX_SIZE <= 65536, "Config storage too large (max 64KB)!");

// Validate memory thresholds
static_assert(MEMORY_CRITICAL_THRESHOLD_BYTES < MEMORY_WARNING_THRESHOLD_BYTES,
              "Critical memory threshold must be less than warning threshold!");

// Validate web server
static_assert(WEB_SERVER_PORT > 0 && WEB_SERVER_PORT <= 65535,
              "Web server port must be valid (1-65535)!");

#endif // SYSTEM_CONSTANTS_H