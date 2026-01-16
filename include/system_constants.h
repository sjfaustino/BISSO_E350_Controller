/**
 * @file system_constants.h
 * @brief System-wide constants and pin definitions
 * @project PosiPro
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

#ifndef SYSTEM_CONSTANTS_H
#define SYSTEM_CONSTANTS_H


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
#ifndef ENCODER_TIMEOUT_MS
#define ENCODER_TIMEOUT_MS 1000                   
#endif
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
#ifndef SAFETY_STALL_CHECK_INTERVAL_MS
#define SAFETY_STALL_CHECK_INTERVAL_MS 100        
#endif
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

#endif // SYSTEM_CONSTANTS_H
