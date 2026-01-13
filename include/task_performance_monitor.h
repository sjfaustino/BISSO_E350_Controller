/**
 * @file task_performance_monitor.h
 * @brief Task Performance Monitoring and Real-time Metrics (PHASE 5.1)
 * @details Tracks execution time, CPU usage, queue metrics, and predictive diagnostics
 * @project BISSO E350 Controller
 */

#ifndef TASK_PERFORMANCE_MONITOR_H
#define TASK_PERFORMANCE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Extended task metrics structure
 */
typedef struct {
    uint32_t task_id;                  // Unique task identifier
    const char* task_name;             // Task name for display

    // Execution timing metrics
    uint32_t run_count;                // Total number of task iterations
    uint32_t total_runtime_us;         // Total execution time in microseconds
    uint32_t avg_runtime_us;           // Average execution time
    uint32_t min_runtime_us;           // Minimum iteration time
    uint32_t max_runtime_us;           // Maximum iteration time

    // Queue metrics
    uint32_t queue_wait_total_us;      // Total time waiting on queue receive
    uint32_t queue_max_wait_us;        // Maximum single queue wait
    uint32_t queue_underflows;         // Times queue was empty (no message)

    // Health indicators
    uint32_t stack_watermark_bytes;    // Minimum free stack during task
    uint8_t cpu_percent;               // Estimated CPU usage percentage
    uint32_t last_execution_timestamp; // Timestamp of last task run (ms)
    bool is_stalled;                   // True if task hasn't run recently

    // Predictive metrics
    uint32_t predicted_max_runtime_us;  // Predicted max based on trend
    float cpu_trend;                    // CPU usage trend (positive = increasing load)
} task_performance_t;

/**
 * System-wide performance metrics
 */
typedef struct {
    uint32_t total_runtime_us;         // Total system execution time
    uint8_t total_cpu_percent;         // Total system CPU usage
    uint32_t uptime_seconds;           // System uptime
    uint32_t free_heap_bytes;          // Current free heap
    uint32_t min_free_heap_bytes;      // Minimum free heap since boot
    uint8_t core0_cpu_percent;         // Core 0 CPU usage
    uint8_t core1_cpu_percent;         // Core 1 CPU usage
} system_performance_t;

/**
 * Initialize performance monitoring
 */
void perfMonitorInit();

/**
 * PHASE 5.2: Enable or disable tracking for a specific task
 * @param task_id Task identifier
 * @param enable true to track, false to skip
 */
void perfMonitorSetTaskTracking(uint32_t task_id, bool enable);

/**
 * PHASE 5.2: Check if task tracking is enabled
 * @param task_id Task identifier
 * @return true if tracking enabled for this task
 */
bool perfMonitorIsTaskTracked(uint32_t task_id);

/**
 * Record task execution start (call at beginning of task iteration)
 * @param task_id Unique identifier for the task
 */
void perfMonitorTaskStart(uint32_t task_id);

/**
 * Record task execution end (call at end of task iteration)
 * @param task_id Unique identifier for the task
 */
void perfMonitorTaskEnd(uint32_t task_id);

/**
 * Record queue wait event
 * @param task_id Task that's waiting
 * @param wait_duration_us Duration task waited on queue
 */
void perfMonitorQueueWait(uint32_t task_id, uint32_t wait_duration_us);

/**
 * Get performance metrics for a specific task
 * @param task_id Task identifier
 * @return Pointer to performance structure, or NULL if not found
 */
const task_performance_t* perfMonitorGetTaskMetrics(uint32_t task_id);

/**
 * Get all task metrics
 * @return Array of performance structures
 */
const task_performance_t* perfMonitorGetAllMetrics(int* count);

/**
 * Get system-wide performance metrics
 * @return System performance structure
 */
system_performance_t perfMonitorGetSystemMetrics();

/**
 * Print detailed performance diagnostics to serial
 */
void perfMonitorPrintDiagnostics();

/**
 * Print lightweight performance summary
 */
void perfMonitorPrintSummary();

/**
 * Reset all performance metrics (useful for benchmarking)
 */
void perfMonitorReset();

/**
 * Get JSON representation of all metrics for web API
 * @param buffer Output buffer
 * @param buffer_size Maximum buffer size
 * @return Number of bytes written, 0 on error
 */
size_t perfMonitorExportJSON(char* buffer, size_t buffer_size);

/**
 * Task IDs for performance monitoring
 * Must match task creation order in task_manager.cpp
 */
#define PERF_TASK_ID_SAFETY     0
#define PERF_TASK_ID_MOTION     1
#define PERF_TASK_ID_ENCODER    2
#define PERF_TASK_ID_PLC_COMM   3
#define PERF_TASK_ID_I2C_MGR    4
#define PERF_TASK_ID_CLI        5
#define PERF_TASK_ID_FAULT_LOG  6
#define PERF_TASK_ID_MONITOR    7
#define PERF_TASK_ID_LCD        8
#define PERF_TASK_ID_TELEMETRY  9

/**
 * Convenience macros for task performance tracking
 * Usage: PERF_TASK_START(); ... task code ... PERF_TASK_END();
 */
#define PERF_TASK_START(task_id) \
    uint32_t _perf_task_id = (task_id); \
    perfMonitorTaskStart(_perf_task_id);

#define PERF_TASK_END() \
    perfMonitorTaskEnd(_perf_task_id);

#ifdef __cplusplus
}
#endif

#endif // TASK_PERFORMANCE_MONITOR_H
