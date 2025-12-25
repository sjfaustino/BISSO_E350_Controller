/**
 * @file spindle_current_monitor.h
 * @brief Spindle Motor Current Monitoring System
 * @project BISSO E350 Controller - Phase 5.0
 * @details Non-blocking spindle motor current monitoring with safety shutdown,
 *          LCD display integration, and telemetry logging for web UI.
 */

#ifndef SPINDLE_CURRENT_MONITOR_H
#define SPINDLE_CURRENT_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Spindle current monitor state
typedef struct {
    bool enabled;                       // Monitoring enabled/disabled
    uint32_t poll_interval_ms;          // Poll interval (default 1000ms)
    uint32_t last_poll_time_ms;         // Last poll timestamp
    float overcurrent_threshold_amps;   // Threshold for safety shutdown (default 30A)

    // Current readings
    float current_amps;                 // Latest current reading
    float current_peak_amps;            // Peak current since startup
    float current_average_amps;         // Running average
    float current_previous_amps;        // Previous reading for delta detection

    // Alarm thresholds
    float tool_breakage_drop_amps;      // Min drop to detect tool breakage (default 5A)
    float stall_threshold_amps;         // Current threshold for stall (default 25A)
    uint32_t stall_timeout_ms;          // Duration before stall triggers (default 2000ms)
    
    // Alarm states
    bool alarm_tool_breakage;           // Tool breakage detected (sudden drop)
    bool alarm_stall;                   // Stall detected (prolonged overload)
    bool alarm_overload;                // Overcurrent condition active
    uint32_t overload_start_time_ms;    // When overload condition started

    // Statistics
    uint32_t read_count;                // Total successful reads
    uint32_t error_count;               // Total read errors
    uint32_t overload_count;            // Times overload condition detected
    uint32_t shutdown_count;            // Times safety shutdown triggered
    uint32_t tool_breakage_count;       // Times tool breakage detected
    uint32_t stall_count;               // Times stall detected

    // Fault history
    uint32_t last_shutdown_time_ms;     // Timestamp of last shutdown
    float last_shutdown_current_amps;   // Current when last shutdown occurred

    // Device state
    uint8_t jxk10_slave_address;        // JXK-10 Modbus slave ID
    uint32_t jxk10_baud_rate;           // JXK-10 baud rate
} spindle_monitor_state_t;

/**
 * @brief Initialize spindle current monitoring system
 * @param jxk10_address JXK-10 Modbus slave address (1-247, default 1)
 * @param threshold_amps Overcurrent threshold in amperes (default 30A)
 * @return true if successful, false on error
 */
bool spindleMonitorInit(uint8_t jxk10_address, float threshold_amps);

/**
 * @brief Enable/disable spindle current monitoring
 * @details When disabled, system runs normally without current monitoring
 * @param enable true to enable, false to disable
 */
void spindleMonitorSetEnabled(bool enable);

/**
 * @brief Check if monitoring is enabled
 * @return true if enabled, false if disabled
 */
bool spindleMonitorIsEnabled(void);

/**
 * @brief Set overcurrent threshold
 * @param threshold_amps Threshold in amperes (0.0-50.0 for standard sensor)
 */
void spindleMonitorSetThreshold(float threshold_amps);

/**
 * @brief Get current overcurrent threshold
 * @return Threshold in amperes
 */
float spindleMonitorGetThreshold(void);

/**
 * @brief Set polling interval
 * @param interval_ms Interval in milliseconds (default 1000ms)
 */
void spindleMonitorSetPollInterval(uint32_t interval_ms);

/**
 * @brief Get current polling interval
 * @return Interval in milliseconds
 */
uint32_t spindleMonitorGetPollInterval(void);

/**
 * @brief Update spindle current monitor (non-blocking)
 * @details Call frequently from main loop or task (e.g., motion control loop)
 *          Handles polling, safety shutdown detection, and telemetry
 * @return true if poll was executed, false if waiting for interval
 */
bool spindleMonitorUpdate(void);

/**
 * @brief Get latest current reading
 * @return Current in amperes
 */
float spindleMonitorGetCurrent(void);

/**
 * @brief Get peak current reading since startup
 * @return Peak current in amperes
 */
float spindleMonitorGetPeakCurrent(void);

/**
 * @brief Check if spindle is in overcurrent condition
 * @return true if current exceeds threshold, false otherwise
 */
bool spindleMonitorIsOvercurrent(void);

/**
 * @brief Check if JXK-10 reports overload status
 * @return true if device overload flag is set
 */
bool spindleMonitorIsOverload(void);

/**
 * @brief Check if JXK-10 reports fault status
 * @return true if device fault flag is set
 */
bool spindleMonitorIsFault(void);

/**
 * @brief Trigger safety shutdown due to overcurrent
 * @details This is called internally but can be called externally for testing
 *          Logs to fault history and initiates motion emergency stop
 */
void spindleMonitorTriggerShutdown(void);

/**
 * @brief Get monitor state/statistics
 * @return Pointer to spindle monitor state structure
 */
const spindle_monitor_state_t* spindleMonitorGetState(void);

/**
 * @brief Reset statistics and error counters
 */
void spindleMonitorResetStats(void);

/**
 * @brief Print spindle monitor diagnostics to console
 */
void spindleMonitorPrintDiagnostics(void);

/**
 * @brief Check for tool breakage alarm
 * @return true if tool breakage detected (sudden current drop)
 */
bool spindleMonitorIsToolBreakage(void);

/**
 * @brief Check for stall alarm
 * @return true if stall detected (prolonged overload)
 */
bool spindleMonitorIsStall(void);

/**
 * @brief Clear all alarms
 */
void spindleMonitorClearAlarms(void);

/**
 * @brief Set tool breakage detection threshold
 * @param drop_amps Minimum current drop to trigger alarm (default 5A)
 */
void spindleMonitorSetToolBreakageThreshold(float drop_amps);

/**
 * @brief Set stall detection parameters
 * @param threshold_amps Current threshold for stall (default 25A)  
 * @param timeout_ms Duration before stall alarm triggers (default 2000ms)
 */
void spindleMonitorSetStallParams(float threshold_amps, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // SPINDLE_CURRENT_MONITOR_H
