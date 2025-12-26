/**
 * @file task_performance_monitor.cpp
 * @brief Task Performance Monitoring Implementation (PHASE 5.1)
 */

#include "task_performance_monitor.h"
#include "serial_logger.h"
#include "memory_monitor.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// Maximum tasks to track
#define MAX_TRACKED_TASKS 16

// Tracking enablement flags (PHASE 5.2: Selective tracking)
// Skip low-priority tasks by default to reduce memory footprint
static bool task_tracking_enabled[MAX_TRACKED_TASKS] = {
    true,   // PERF_TASK_ID_SAFETY (critical)
    true,   // PERF_TASK_ID_MOTION (critical)
    true,   // PERF_TASK_ID_ENCODER (critical)
    true,   // PERF_TASK_ID_PLC_COMM (important)
    true,   // PERF_TASK_ID_I2C_MGR (important)
    false,  // PERF_TASK_ID_CLI (low priority)
    true,   // PERF_TASK_ID_FAULT_LOG (important)
    false,  // PERF_TASK_ID_MONITOR (system task)
    false   // PERF_TASK_ID_LCD (UI task)
};

// Per-task performance metrics
static task_performance_t task_metrics[MAX_TRACKED_TASKS];
static int active_tasks = 0;

// Task execution timing tracking (temporary during execution)
static struct {
    uint32_t start_time_us;
    bool in_progress;
} task_timing[MAX_TRACKED_TASKS];

// System-wide metrics cache
static system_performance_t system_metrics;
static uint32_t last_metrics_update = 0;

// Task name lookup table
static const char* task_names[] = {
    "Safety",
    "Motion",
    "Encoder",
    "PLC_Comm",
    "I2C_Manager",
    "CLI",
    "Fault_Log",
    "Monitor",
    "LCD"
};

// Helper: Get or create metrics entry for task (PHASE 5.2: Lazy-load with selective tracking)
static task_performance_t* getOrCreateMetrics(uint32_t task_id) {
    // Check if this task is enabled for tracking
    if (task_id >= MAX_TRACKED_TASKS || !task_tracking_enabled[task_id]) {
        return NULL;  // Skip tracking for disabled tasks
    }

    // Search for existing
    for (int i = 0; i < active_tasks; i++) {
        if (task_metrics[i].task_id == task_id) {
            return &task_metrics[i];
        }
    }

    // Create new if space available (lazy-load on first use)
    if (active_tasks < MAX_TRACKED_TASKS) {
        task_performance_t* metrics = &task_metrics[active_tasks];
        memset(metrics, 0, sizeof(*metrics));
        metrics->task_id = task_id;
        metrics->task_name = (task_id < 9) ? task_names[task_id] : "Unknown";
        metrics->min_runtime_us = 0xFFFFFFFF;
        active_tasks++;
        return metrics;
    }

    return NULL;
}

// Get microsecond timestamp
static uint32_t getMicroseconds() {
    return micros();
}

void perfMonitorInit() {
    memset(task_metrics, 0, sizeof(task_metrics));
    memset(task_timing, 0, sizeof(task_timing));
    memset(&system_metrics, 0, sizeof(system_metrics));
    active_tasks = 0;

    logInfo("[PERF_MONITOR] Initialized (lazy-load, selective tracking enabled)");
}

void perfMonitorSetTaskTracking(uint32_t task_id, bool enable) {
    if (task_id >= MAX_TRACKED_TASKS) return;
    task_tracking_enabled[task_id] = enable;
}

bool perfMonitorIsTaskTracked(uint32_t task_id) {
    if (task_id >= MAX_TRACKED_TASKS) return false;
    return task_tracking_enabled[task_id];
}

void perfMonitorTaskStart(uint32_t task_id) {
    if (task_id >= MAX_TRACKED_TASKS) return;

    task_timing[task_id].start_time_us = getMicroseconds();
    task_timing[task_id].in_progress = true;
}

void perfMonitorTaskEnd(uint32_t task_id) {
    if (task_id >= MAX_TRACKED_TASKS || !task_timing[task_id].in_progress) return;

    uint32_t end_time_us = getMicroseconds();
    uint32_t duration_us = (uint32_t)(end_time_us - task_timing[task_id].start_time_us);

    task_performance_t* metrics = getOrCreateMetrics(task_id);
    if (!metrics) return;

    // Update timing statistics
    metrics->run_count++;
    metrics->total_runtime_us += duration_us;
    metrics->avg_runtime_us = metrics->total_runtime_us / metrics->run_count;

    if (duration_us < metrics->min_runtime_us) {
        metrics->min_runtime_us = duration_us;
    }
    if (duration_us > metrics->max_runtime_us) {
        metrics->max_runtime_us = duration_us;
    }

    // Update predicted max (moving average of max)
    if (metrics->predicted_max_runtime_us == 0) {
        metrics->predicted_max_runtime_us = duration_us;
    } else {
        // Exponential moving average: new = 0.7 * old + 0.3 * current
        metrics->predicted_max_runtime_us =
            (uint32_t)((0.7f * metrics->predicted_max_runtime_us) + (0.3f * duration_us));
    }

    metrics->last_execution_timestamp = millis();
    task_timing[task_id].in_progress = false;
}

void perfMonitorQueueWait(uint32_t task_id, uint32_t wait_duration_us) {
    task_performance_t* metrics = getOrCreateMetrics(task_id);
    if (!metrics) return;

    metrics->queue_wait_total_us += wait_duration_us;
    if (wait_duration_us > metrics->queue_max_wait_us) {
        metrics->queue_max_wait_us = wait_duration_us;
    }
}

const task_performance_t* perfMonitorGetTaskMetrics(uint32_t task_id) {
    for (int i = 0; i < active_tasks; i++) {
        if (task_metrics[i].task_id == task_id) {
            return &task_metrics[i];
        }
    }
    return NULL;
}

const task_performance_t* perfMonitorGetAllMetrics(int* count) {
    if (count) *count = active_tasks;
    return task_metrics;
}

system_performance_t perfMonitorGetSystemMetrics() {
    // Update system metrics if not recently cached
    uint32_t now = millis();
    if ((uint32_t)(now - last_metrics_update) > 100) {  // Update every 100ms
        // Calculate total CPU from all tasks
        uint32_t total_runtime = 0;
        for (int i = 0; i < active_tasks; i++) {
            total_runtime += task_metrics[i].total_runtime_us;
        }

        system_metrics.total_runtime_us = total_runtime;
        system_metrics.uptime_seconds = now / 1000;
        system_metrics.free_heap_bytes = memoryMonitorGetFreeHeap();
        system_metrics.min_free_heap_bytes = memoryMonitorGetMinFreeHeap();

        // Estimate CPU usage (total task time / total elapsed time)
        uint32_t elapsed_us = system_metrics.uptime_seconds * 1000000UL;
        if (elapsed_us > 0) {
            system_metrics.total_cpu_percent = (uint8_t)((total_runtime * 100) / elapsed_us);
            if (system_metrics.total_cpu_percent > 100) {
                system_metrics.total_cpu_percent = 100;
            }
        }

        last_metrics_update = now;
    }

    return system_metrics;
}

void perfMonitorPrintSummary() {
    serialLoggerLock();
    Serial.println("\n[PERF] === Performance Summary ===");
    Serial.println("Task            | Runs    | Avg(us)  | Max(us)  | CPU%");
    Serial.println("-----------|---------|----------|----------|------");

    system_performance_t sys = perfMonitorGetSystemMetrics();
    uint32_t total_time = 0;
    for (int i = 0; i < active_tasks; i++) {
        total_time += task_metrics[i].total_runtime_us;
    }

    for (int i = 0; i < active_tasks; i++) {
        float cpu_percent = (total_time > 0) ?
            ((float)task_metrics[i].total_runtime_us / total_time) * 100.0f : 0.0f;

        Serial.printf("%-15s | %-7lu | %-8lu | %-8lu | %.1f%%\n",
            task_metrics[i].task_name,
            (unsigned long)task_metrics[i].run_count,
            (unsigned long)task_metrics[i].avg_runtime_us,
            (unsigned long)task_metrics[i].max_runtime_us,
            cpu_percent);
    }

    Serial.printf("\nSystem CPU: %u%%, Heap: %lu bytes free\n",
        sys.total_cpu_percent,
        (unsigned long)sys.free_heap_bytes);
    Serial.println();
    serialLoggerUnlock();
}

void perfMonitorPrintDiagnostics() {
    serialLoggerLock();
    Serial.println("\n[PERF] === Detailed Performance Diagnostics ===");

    system_performance_t sys = perfMonitorGetSystemMetrics();
    Serial.printf("Uptime: %lu seconds | Free Heap: %lu bytes | Min Heap: %lu bytes\n",
        (unsigned long)sys.uptime_seconds,
        (unsigned long)sys.free_heap_bytes,
        (unsigned long)sys.min_free_heap_bytes);

    Serial.println("\nPer-Task Metrics:");
    Serial.println("Task            | Runs | Avg(us) | Max(us) | Min(us) | Q-Wait(us) | CPU%");
    Serial.println("-----------|------|---------|---------|---------|------------|-----");

    uint32_t total_time = 0;
    for (int i = 0; i < active_tasks; i++) {
        total_time += task_metrics[i].total_runtime_us;
    }

    for (int i = 0; i < active_tasks; i++) {
        float cpu_percent = (total_time > 0) ?
            ((float)task_metrics[i].total_runtime_us / total_time) * 100.0f : 0.0f;

        Serial.printf("%-15s | %4lu | %7lu | %7lu | %7lu | %10lu | %.1f%%\n",
            task_metrics[i].task_name,
            (unsigned long)task_metrics[i].run_count,
            (unsigned long)task_metrics[i].avg_runtime_us,
            (unsigned long)task_metrics[i].max_runtime_us,
            (unsigned long)task_metrics[i].min_runtime_us,
            (unsigned long)task_metrics[i].queue_wait_total_us,
            cpu_percent);
    }

    Serial.println();
    serialLoggerUnlock();
}

void perfMonitorReset() {
    memset(task_metrics, 0, sizeof(task_metrics));
    memset(&system_metrics, 0, sizeof(system_metrics));
    last_metrics_update = 0;
    active_tasks = 0;
    logInfo("[PERF_MONITOR] Metrics reset");
}

size_t perfMonitorExportJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 256) return 0;

    system_performance_t sys = perfMonitorGetSystemMetrics();

    // Build JSON manually to save memory
    size_t offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\"system\":{\"uptime_sec\":%lu,\"cpu_percent\":%u,\"free_heap\":%lu,\"min_heap\":%lu},\"tasks\":[",
        (unsigned long)sys.uptime_seconds,
        sys.total_cpu_percent,
        (unsigned long)sys.free_heap_bytes,
        (unsigned long)sys.min_free_heap_bytes);
    // PHASE 5.10: Check for buffer overflow after each snprintf
    if (offset >= buffer_size) return buffer_size - 1;

    uint32_t total_time = 0;
    for (int i = 0; i < active_tasks; i++) {
        total_time += task_metrics[i].total_runtime_us;
    }

    for (int i = 0; i < active_tasks; i++) {
        float cpu_percent = (total_time > 0) ?
            ((float)task_metrics[i].total_runtime_us / total_time) * 100.0f : 0.0f;

        if (i > 0) {
            offset += snprintf(buffer + offset, buffer_size - offset, ",");
            if (offset >= buffer_size) return buffer_size - 1;
        }

        offset += snprintf(buffer + offset, buffer_size - offset,
            "{\"name\":\"%s\",\"runs\":%lu,\"avg_us\":%lu,\"max_us\":%lu,\"min_us\":%lu,\"cpu_percent\":%.1f}",
            task_metrics[i].task_name,
            (unsigned long)task_metrics[i].run_count,
            (unsigned long)task_metrics[i].avg_runtime_us,
            (unsigned long)task_metrics[i].max_runtime_us,
            (unsigned long)task_metrics[i].min_runtime_us,
            cpu_percent);
        if (offset >= buffer_size) return buffer_size - 1;
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "]}");
    if (offset >= buffer_size) return buffer_size - 1;

    return offset;
}
