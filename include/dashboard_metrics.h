/**
 * @file dashboard_metrics.h
 * @brief Dashboard Metrics Aggregator (PHASE 5.3)
 * @details Real-time data streaming for web UI dashboard
 * @project BISSO E350 Controller
 */

#ifndef DASHBOARD_METRICS_H
#define DASHBOARD_METRICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Real-time dashboard metrics snapshot
 */
typedef struct {
    // System status
    uint32_t uptime_ms;
    uint8_t cpu_percent;
    uint32_t free_heap_bytes;

    // Motion
    float x_pos, y_pos, z_pos, a_pos;
    bool motion_moving;
    bool motion_enabled;

    // Safety
    bool estop_active;
    bool alarm_active;
    uint32_t fault_count;

    // Spindle
    float spindle_current_amps;
    bool spindle_overcurrent;

    // Network
    bool wifi_connected;
    uint8_t wifi_signal;

    // Performance
    uint8_t slowest_task_id;
    uint32_t slowest_task_us;

    // Encoder health (simple status)
    uint8_t encoder_health[4];  // 0=optimal, 1=normal, 2=degraded, 3=critical

    // Load state
    uint8_t load_state;  // 0=normal, 1=elevated, 2=high, 3=critical

    // Timestamp
    uint64_t timestamp_ms;
} dashboard_metrics_t;

/**
 * Initialize dashboard metrics
 */
void dashboardMetricsInit();

/**
 * Update dashboard metrics from all subsystems
 * Call periodically from monitor task
 */
void dashboardMetricsUpdate();

/**
 * Get current metrics snapshot
 * @return Current dashboard metrics
 */
dashboard_metrics_t dashboardMetricsGetSnapshot();

/**
 * Export metrics as compact JSON for WebSocket streaming
 * @param buffer Output buffer
 * @param buffer_size Max size
 * @return Bytes written
 */
size_t dashboardMetricsExportJSON(char* buffer, size_t buffer_size);

/**
 * Export metrics as extended JSON with all details
 * @param buffer Output buffer
 * @param buffer_size Max size
 * @return Bytes written
 */
size_t dashboardMetricsExportExtendedJSON(char* buffer, size_t buffer_size);

/**
 * Print dashboard metrics to serial
 */
void dashboardMetricsPrint();

#ifdef __cplusplus
}
#endif

#endif // DASHBOARD_METRICS_H
