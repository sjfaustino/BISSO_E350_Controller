/**
 * @file system_tuning.h
 * @brief System-wide tuning parameters and thresholds
 * @details Centralizes magic numbers for easy tuning and maintainability
 * @project PosiPro Controller
 *
 * MAINTAINABILITY FIX: All hardcoded thresholds and timeouts centralized here
 * Benefits:
 * - Single location for system tuning
 * - Self-documenting parameter names
 * - Easy to adjust for different hardware configurations
 * - Prevents inconsistent values across modules
 */

#ifndef SYSTEM_TUNING_H
#define SYSTEM_TUNING_H

// ============================================================================
// MOTION CONTROL PARAMETERS
// ============================================================================

// Mutex timeout and escalation
#define MOTION_MUTEX_TIMEOUT_BASE_MS 100        // Initial mutex timeout (ms)
#define MOTION_MUTEX_TIMEOUT_ESCALATION 20      // Consecutive failures before E-STOP
#define MOTION_POSITION_TOLERANCE_COUNTS 1      // Position tolerance (encoder counts)

// Motion loop timing
#define MOTION_UPDATE_INTERVAL_MS 10            // Motion update period (100Hz)
#define MOTION_STALL_CHECK_INTERVAL_MS 500      // How often to check for stalls

// Position limits
#define MOTION_MAX_FEEDRATE_MM_MIN 3000         // Maximum feedrate (mm/min)
#define MOTION_MIN_FEEDRATE_MM_MIN 1            // Minimum feedrate (mm/min)

// ============================================================================
// SAFETY SYSTEM PARAMETERS
// ============================================================================

// Quality score thresholds (0-100 scale)
#define SAFETY_QUALITY_THRESHOLD_CRITICAL 25    // <25 = Critical alarm
#define SAFETY_QUALITY_THRESHOLD_WARNING 50     // <50 = Warning
#define SAFETY_QUALITY_THRESHOLD_GOOD 75        // >75 = Good

// Timing requirements
#define SAFETY_ESTOP_LATENCY_TARGET_MS 50       // Target E-STOP response (ISO 13849)
#define SAFETY_ESTOP_LATENCY_MAX_MS 100         // Maximum acceptable latency (SIL2)
#define SAFETY_STALL_CHECK_INTERVAL_MS 500      // Stall detection period

// Velocity thresholds
#define SAFETY_MIN_VELOCITY_THRESHOLD_MM_S 0.1  // Below this = stalled

// ============================================================================
// FAULT LOGGING PARAMETERS
// ============================================================================

// NVS write cooldown for flash wear protection
#define FAULT_NVS_WRITE_COOLDOWN_NORMAL_MS 1000     // Normal operation (1 second)
#define FAULT_NVS_WRITE_COOLDOWN_STORM_MS 10000     // During fault storm (10 seconds)
#define FAULT_STORM_THRESHOLD_PER_SEC 5             // >5 faults/sec = storm
#define FAULT_RATE_WINDOW_SIZE 10                   // Sliding window size for rate calc

// Ring buffer configuration
#define FAULT_RING_BUFFER_SIZE 64               // In-memory fault ring buffer entries
#define FAULT_NVS_BUFFER_SIZE 32                // NVS persistent fault entries

// ============================================================================
// I2C BUS PARAMETERS
// ============================================================================

// Health check timing
#define I2C_HEALTH_CHECK_INTERVAL_MS 1000       // Check PLC I2C every second
#define I2C_RECOVERY_RETRY_COUNT 3              // Retry recovery this many times
#define I2C_RECOVERY_BACKOFF_BASE_MS 50         // Base backoff time (50ms)

// Mutex timeout escalation
#define I2C_MUTEX_TIMEOUT_THRESHOLD 10          // Escalate after 10 timeouts

// LCD Degraded Mode
#define LCD_MAX_CONSECUTIVE_ERRORS 10           // Permanently switch to Serial after 10 fails

// ============================================================================
// MOTION SAFETY PARAMETERS
// ============================================================================

// ============================================================================
// WATCHDOG PARAMETERS
// ============================================================================

// Watchdog timing (defined in watchdog_manager.h, referenced here for docs)
#define WDT_TASK_TIMEOUT_SEC 10                 // Task watchdog timeout
#define WDT_INTERRUPT_TIMEOUT_MS 5000           // Interrupt watchdog timeout
#define WDT_TEST_STALL_DURATION_MS 10000        // Test stall duration

// ============================================================================
// STACK MONITORING PARAMETERS
// ============================================================================

// Stack high water mark thresholds (in words, not bytes)
// ESP32-S3: 1 word = 4 bytes
#define STACK_CRITICAL_THRESHOLD_WORDS 128      // <128 words (512 bytes) = CRITICAL
#define STACK_WARNING_THRESHOLD_WORDS 256       // <256 words (1024 bytes) = WARNING
#define STACK_SAFE_MARGIN_WORDS 512             // >512 words (2048 bytes) = SAFE

// Stack sizes (in bytes) - defined in task_manager.h
#define TASK_STACK_DEFAULT 2048                 // Default task stack
#define TASK_STACK_LARGE 4096                   // Large task stack
#define TASK_STACK_CRITICAL 6144                // Critical path tasks

// ============================================================================
// SPINLOCK PERFORMANCE PARAMETERS
// ============================================================================

// Critical section duration thresholds
#define SPINLOCK_MAX_DURATION_US 10             // >10μs should use mutex instead
#define SPINLOCK_WARNING_DURATION_US 5          // >5μs warning threshold
#define SPINLOCK_IDEAL_DURATION_US 1            // <1μs ideal for spinlock

// ============================================================================
// WEB SERVER PARAMETERS
// ============================================================================

// Request size limits
#define WEB_MAX_REQUEST_BODY_SIZE 8192          // 8KB max POST body
#define WEB_MAX_CONFIG_KEY_LENGTH 64            // Max config key length
#define WEB_MAX_CONFIG_VALUE_LENGTH 256         // Max config value length

// Rate limiting
#define WEB_RATE_LIMIT_WINDOW_SEC 60            // 1 minute window
#define WEB_RATE_LIMIT_MAX_REQUESTS 60          // 60 requests/minute

// ============================================================================
// ENCODER PARAMETERS
// ============================================================================

// Communication timing
#define ENCODER_READ_INTERVAL_MS 100            // Read encoder every 100ms
#define ENCODER_TIMEOUT_MS 500                  // Encoder read timeout
#define ENCODER_BAUD_RATE_DEFAULT 9600          // Default baud rate

// Deviation thresholds
#define ENCODER_MAX_DEVIATION_COUNTS 100        // Maximum acceptable deviation

// ============================================================================
// VFD PARAMETERS
// ============================================================================

// Spindle control
#define VFD_MIN_SPEED_HZ 5                      // Minimum spindle frequency
#define VFD_MAX_SPEED_HZ 400                    // Maximum spindle frequency
#define VFD_ACC_TIME_DEFAULT_MS 1000            // Default acceleration time
#define VFD_DEC_TIME_DEFAULT_MS 1000            // Default deceleration time

// Current monitoring
#define VFD_OVERCURRENT_THRESHOLD_AMPS 30       // Overcurrent threshold
#define VFD_POLL_INTERVAL_MS 1000               // Poll spindle current every second

// ============================================================================
// SPINDLE MONITOR PARAMETERS (JXK-10)
// ============================================================================

#define SPINDLE_MONITOR_POLL_DEFAULT_MS 1000    // Default poll interval
#define SPINDLE_MONITOR_RATE_LIMIT_MS 100       // Minimum time between updates
#define SPINDLE_OVERCURRENT_DEFAULT_AMPS 30.0f  // Default overcurrent threshold
#define SPINDLE_STALL_DEFAULT_AMPS 25.0f        // Default stall threshold
#define SPINDLE_DROP_DEFAULT_AMPS 5.0f          // Default breakage drop threshold
#define SPINDLE_STALL_TIMEOUT_DEFAULT_MS 2000   // Default stall timeout

// ============================================================================
// TELEMETRY PARAMETERS
// ============================================================================

// Update intervals
#define TELEMETRY_UPDATE_INTERVAL_MS 1000       // Update telemetry every second
#define TELEMETRY_HISTORY_SIZE 60               // Keep 60 seconds of history

// ============================================================================
// DEBUGGING AND DIAGNOSTICS
// ============================================================================

// Log throttling
#define LOG_THROTTLE_INTERVAL_MS 1000           // Throttle repeated logs to 1/sec
#define LOG_STORM_DETECTION_THRESHOLD 10        // >10 logs/sec = storm

// Performance profiling
#define PROFILE_SAMPLE_COUNT 100                // Number of samples for profiling
#define PROFILE_REPORT_INTERVAL_SEC 60          // Report every 60 seconds

#endif // SYSTEM_TUNING_H
