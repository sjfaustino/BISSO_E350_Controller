/**
 * @file system_telemetry.h
 * @brief Comprehensive System Telemetry and Health Metrics (PHASE 5.1)
 * @details Aggregates all system metrics into unified telemetry dashboard
 * @project BISSO E350 Controller
 */

#ifndef SYSTEM_TELEMETRY_H
#define SYSTEM_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * System health status
 */
typedef enum {
    HEALTH_UNKNOWN = 0,
    HEALTH_CRITICAL = 1,    // System is in fault state
    HEALTH_WARNING = 2,     // High resource usage or slow tasks
    HEALTH_NORMAL = 3,      // All systems operating normally
    HEALTH_OPTIMAL = 4      // All systems within optimal ranges
} system_health_t;

/**
 * Comprehensive telemetry snapshot
 */
typedef struct {
    // System Status
    system_health_t health_status;
    uint32_t uptime_seconds;
    uint32_t boot_failures;  // Number of boot attempts before success

    // CPU & Memory
    uint8_t cpu_usage_percent;
    uint32_t free_heap_bytes;
    uint32_t heap_fragmentation_percent;
    uint32_t stack_used_bytes;

    // Motion System
    bool motion_enabled;
    bool motion_moving;
    float axis_x_mm;
    float axis_y_mm;
    float axis_z_mm;
    float axis_a_mm;
    uint32_t steps_executed;
    uint32_t motion_errors;

    // Spindle
    bool spindle_enabled;
    float spindle_current_amps;
    float spindle_current_peak_amps;
    uint32_t spindle_errors;
    bool spindle_overcurrent;
    bool spindle_fault;

    // RPM Sensor (YH-TC05)
    bool rpm_sensor_enabled;
    uint16_t spindle_rpm;
    bool rpm_stall_detected;

    // Safety System
    bool estop_active;
    bool alarm_active;
    uint32_t safety_events;
    uint32_t faults_logged;
    uint32_t critical_faults;

    // Task Metrics
    uint8_t slowest_task_id;      // Task with longest execution time
    uint32_t slowest_task_time_us;
    uint32_t total_task_underruns; // Times tasks missed deadline

    // Network
    bool wifi_connected;
    uint8_t wifi_signal_strength;  // 0-100 RSSI percentage
    uint32_t http_requests_served;
    uint32_t http_errors;

    // Configuration
    uint32_t config_version;
    bool config_is_default;
    uint32_t config_changes_count;

    // Diagnostics
    const char* primary_fault_message;
    uint32_t loop_cycle_count;
    uint32_t watchdog_resets;
} system_telemetry_t;

/**
 * Binary telemetry packet (Packed for 90% space reduction)
 */
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;           // 0xB155 (BISSO)
    uint8_t version;          // Protocol version: 1
    uint8_t health;           // system_health_t

    uint32_t uptime;          // seconds
    uint8_t cpu_usage;        // percentage
    uint32_t free_heap;       // bytes
    
    uint8_t flags;            // [0]:motion_en, [1]:moving, [2]:spindle_en, [3]:overcurrent, [4]:fault, [5]:wifi_conn, [6]:estop, [7]:alarm

    float axis_x;             // mm
    float axis_y;             // mm
    float axis_z;             // mm
    float axis_a;             // mm

    float spindle_amps;       // Current
    float spindle_peak;       // Peak Current

    uint32_t faults_logged;
    uint32_t critical_faults;
    
    uint8_t slowest_id;
    uint32_t slowest_us;
    
    uint8_t wifi_signal;      // percentage
} telemetry_packet_t;
#pragma pack(pop)

/**
 * Initialize telemetry collection
 */
void telemetryInit();

/**
 * Update telemetry snapshot with current system state
 */
void telemetryUpdate();

/**
 * Get current telemetry snapshot
 * @return Telemetry structure with latest data
 */
system_telemetry_t telemetryGetSnapshot();

/**
 * Get system health status
 * @return Health enum value
 */
system_health_t telemetryGetHealthStatus();

/**
 * Export telemetry as JSON for web API
 * @param buffer Output buffer
 * @param buffer_size Maximum buffer size
 * @return Number of bytes written, 0 on error
 */
size_t telemetryExportJSON(char* buffer, size_t buffer_size);

/**
 * Export compact telemetry (subset of fields) for lightweight clients
 * @param buffer Output buffer
 * @param buffer_size Maximum buffer size
 * @return Number of bytes written
 */
size_t telemetryExportCompactJSON(char* buffer, size_t buffer_size);

/**
 * Export telemetry as binary packet (High performance)
 * @param packet Output packet buffer
 * @return Number of bytes written
 */
size_t telemetryExportBinary(telemetry_packet_t* packet);

/**
 * Export delta telemetry - only includes fields changed since last call
 * @param buffer Output buffer
 * @param buffer_size Maximum buffer size
 * @param full_update If true, return all fields (reset baseline)
 * @return Number of bytes written
 */
size_t telemetryExportDeltaJSON(char* buffer, size_t buffer_size, bool full_update);

/**
 * Print human-readable telemetry summary
 */
void telemetryPrintSummary();

/**
 * Print detailed telemetry with all fields
 */
void telemetryPrintDetailed();

/**
 * Get health status as human-readable string
 * @param status Health enum
 * @return String description
 */
const char* telemetryGetHealthStatusString(system_health_t status);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_TELEMETRY_H
