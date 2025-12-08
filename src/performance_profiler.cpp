/**
 * @file performance_profiler.cpp
 * @brief Real-Time Performance Profiling Implementation
 * @author Sergio Faustino
 */

#include "performance_profiler.h"
#include "serial_logger.h"
#include <esp_heap_caps.h>
#include <string.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static profile_section_t sections[PROFILER_MAX_SECTIONS];
static int section_count = 0;
static bool profiler_initialized = false;

static system_snapshot_t last_snapshot;
static health_status_t current_health;

// Health monitoring state
static uint32_t warning_timestamps[60]; // Last 60 warnings with timestamps
static int warning_index = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void profilerInit() {
    if (profiler_initialized) return;

    memset(sections, 0, sizeof(sections));
    memset(&last_snapshot, 0, sizeof(last_snapshot));
    memset(&current_health, 0, sizeof(current_health));
    memset(warning_timestamps, 0, sizeof(warning_timestamps));

    section_count = 0;
    profiler_initialized = true;

    // Initialize health status as healthy
    current_health.motion_loop_healthy = true;
    current_health.safety_loop_healthy = true;
    current_health.memory_healthy = true;
    current_health.i2c_healthy = true;

    logInfo("[PROFILER] Initialized. Capacity: %d sections", PROFILER_MAX_SECTIONS);
}

// ============================================================================
// SECTION MANAGEMENT
// ============================================================================

int profilerRegisterSection(const char* name, uint32_t warning_threshold_us) {
    if (!profiler_initialized) profilerInit();

    if (section_count >= PROFILER_MAX_SECTIONS) {
        logError("[PROFILER] Cannot register '%s': Max sections reached", name);
        return -1;
    }

    int id = section_count++;
    profile_section_t* section = &sections[id];

    section->name = name;
    section->call_count = 0;
    section->total_time_us = 0;
    section->min_time_us = UINT32_MAX;
    section->max_time_us = 0;
    section->last_time_us = 0;
    section->warning_count = 0;
    section->warning_threshold_us = warning_threshold_us;
    section->last_warning_time = 0;
    section->active = false;
    section->start_time_us = 0;

    return id;
}

// ============================================================================
// TIMING FUNCTIONS
// ============================================================================

void profilerStart(int section_id) {
    if (section_id < 0 || section_id >= section_count) return;

    profile_section_t* section = &sections[section_id];

    if (section->active) {
        // Nested call detected - log warning but don't break
        static uint32_t last_nested_warn = 0;
        if (millis() - last_nested_warn > 5000) {
            logWarning("[PROFILER] Nested call detected: %s", section->name);
            last_nested_warn = millis();
        }
        return;
    }

    section->active = true;
    section->start_time_us = micros();
}

void profilerStop(int section_id) {
    uint32_t end_time = micros(); // Capture immediately to reduce overhead

    if (section_id < 0 || section_id >= section_count) return;

    profile_section_t* section = &sections[section_id];

    if (!section->active) {
        // Stop without start - log once and ignore
        static uint32_t last_mismatch_warn = 0;
        if (millis() - last_mismatch_warn > 5000) {
            logWarning("[PROFILER] Stop without start: %s", section->name);
            last_mismatch_warn = millis();
        }
        return;
    }

    uint32_t elapsed = end_time - section->start_time_us;

    // Update statistics
    section->call_count++;
    section->total_time_us += elapsed;
    section->last_time_us = elapsed;

    if (elapsed < section->min_time_us) section->min_time_us = elapsed;
    if (elapsed > section->max_time_us) section->max_time_us = elapsed;

    // Check threshold
    if (section->warning_threshold_us > 0 && elapsed > section->warning_threshold_us) {
        section->warning_count++;

        // Log warning (throttled to once per 2 seconds per section)
        uint32_t now = millis();
        if (now - section->last_warning_time > 2000) {
            logWarning("[PROFILER] %s took %lu µs (threshold: %lu µs)",
                      section->name,
                      (unsigned long)elapsed,
                      (unsigned long)section->warning_threshold_us);
            section->last_warning_time = now;

            // Record for health monitoring
            warning_timestamps[warning_index] = now;
            warning_index = (warning_index + 1) % 60;
        }
    }

    section->active = false;
}

// ============================================================================
// QUERY FUNCTIONS
// ============================================================================

const profile_section_t* profilerGetSection(int section_id) {
    if (section_id < 0 || section_id >= section_count) return nullptr;
    return &sections[section_id];
}

int profilerGetActiveWarnings() {
    int count = 0;
    for (int i = 0; i < section_count; i++) {
        if (sections[i].call_count > 0 && sections[i].warning_count > 0) {
            // Check if warning is recent (within last 10 seconds)
            if (millis() - sections[i].last_warning_time < 10000) {
                count++;
            }
        }
    }
    return count;
}

// ============================================================================
// REPORTING
// ============================================================================

void profilerPrintReport(bool detailed) {
    Serial.println("\n╔══════════════════════════════════════════════════════════════════╗");
    Serial.println("║            PERFORMANCE PROFILING REPORT                          ║");
    Serial.println("╚══════════════════════════════════════════════════════════════════╝");

    Serial.println("\nSection                  Calls      Avg(µs)   Min(µs)   Max(µs)   Warn");
    Serial.println("─────────────────────────────────────────────────────────────────────");

    for (int i = 0; i < section_count; i++) {
        profile_section_t* s = &sections[i];

        // Skip sections with no data unless detailed report requested
        if (!detailed && s->call_count == 0) continue;

        uint32_t avg = (s->call_count > 0) ? (s->total_time_us / s->call_count) : 0;
        uint32_t min = (s->min_time_us == UINT32_MAX) ? 0 : s->min_time_us;

        // Format name with padding
        char name_padded[25];
        snprintf(name_padded, sizeof(name_padded), "%-24s", s->name);

        Serial.printf("%s %6lu  %8lu  %8lu  %8lu  %4lu",
                     name_padded,
                     (unsigned long)s->call_count,
                     (unsigned long)avg,
                     (unsigned long)min,
                     (unsigned long)s->max_time_us,
                     (unsigned long)s->warning_count);

        // Highlight if currently over threshold
        if (s->warning_threshold_us > 0 && avg > s->warning_threshold_us) {
            Serial.print(" ⚠️");
        }
        Serial.println();
    }

    // System snapshot
    profilerGetSystemSnapshot(&last_snapshot);

    Serial.println("\n─────────────────────────────────────────────────────────────────────");
    Serial.printf("Heap Free: %lu bytes | Min: %lu bytes | Max Block: %lu bytes\n",
                 (unsigned long)last_snapshot.heap_free,
                 (unsigned long)last_snapshot.heap_min,
                 (unsigned long)last_snapshot.heap_max_block);

    // Health status
    health_status_t health;
    profilerGetHealthStatus(&health);

    Serial.println("\n╔══════════════════════════════════════════════════════════════════╗");
    Serial.println("║                        HEALTH STATUS                             ║");
    Serial.println("╚══════════════════════════════════════════════════════════════════╝");
    Serial.printf("  Motion Loop:  %s\n", health.motion_loop_healthy ? "✓ OK" : "✗ SLOW");
    Serial.printf("  Safety Loop:  %s\n", health.safety_loop_healthy ? "✓ OK" : "✗ SLOW");
    Serial.printf("  Memory:       %s\n", health.memory_healthy ? "✓ OK" : "✗ LOW");
    Serial.printf("  I2C Bus:      %s\n", health.i2c_healthy ? "✓ OK" : "✗ ERRORS");
    Serial.printf("  Warnings/min: %lu\n", (unsigned long)health.warnings_last_minute);

    Serial.println("╚══════════════════════════════════════════════════════════════════╝\n");
}

void profilerReset() {
    for (int i = 0; i < section_count; i++) {
        sections[i].call_count = 0;
        sections[i].total_time_us = 0;
        sections[i].min_time_us = UINT32_MAX;
        sections[i].max_time_us = 0;
        sections[i].last_time_us = 0;
        sections[i].warning_count = 0;
    }

    memset(warning_timestamps, 0, sizeof(warning_timestamps));
    warning_index = 0;

    logInfo("[PROFILER] Statistics reset");
}

// ============================================================================
// SYSTEM MONITORING
// ============================================================================

void profilerGetSystemSnapshot(system_snapshot_t* snapshot) {
    if (!snapshot) return;

    snapshot->heap_free = ESP.getFreeHeap();
    snapshot->heap_min = ESP.getMinFreeHeap();
    snapshot->heap_max_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    snapshot->task_switches = 0; // Not easily available on ESP32

    // Estimate CPU usage (rough approximation)
    uint32_t total_time = 0;
    uint32_t sample_period_us = 1000000; // 1 second

    for (int i = 0; i < section_count; i++) {
        if (sections[i].call_count > 0) {
            total_time += sections[i].total_time_us;
        }
    }

    // This is a very rough estimate assuming all profiled code
    snapshot->cpu_usage_percent = (float)total_time / sample_period_us * 100.0f;
    if (snapshot->cpu_usage_percent > 100.0f) snapshot->cpu_usage_percent = 100.0f;
}

void profilerUpdateHealth() {
    // Count warnings in last minute
    uint32_t now = millis();
    uint32_t recent_warnings = 0;

    for (int i = 0; i < 60; i++) {
        if (warning_timestamps[i] > 0 && (now - warning_timestamps[i]) < 60000) {
            recent_warnings++;
        }
    }
    current_health.warnings_last_minute = recent_warnings;

    // Check motion loop health
    for (int i = 0; i < section_count; i++) {
        if (strstr(sections[i].name, "motion") != nullptr ||
            strstr(sections[i].name, "Motion") != nullptr) {
            if (sections[i].call_count > 0) {
                uint32_t avg = sections[i].total_time_us / sections[i].call_count;
                current_health.motion_loop_healthy = (avg < PROFILE_WARN_MOTION_LOOP_US);
            }
        }

        if (strstr(sections[i].name, "safety") != nullptr ||
            strstr(sections[i].name, "Safety") != nullptr) {
            if (sections[i].call_count > 0) {
                uint32_t avg = sections[i].total_time_us / sections[i].call_count;
                current_health.safety_loop_healthy = (avg < PROFILE_WARN_SAFETY_LOOP_US);
            }
        }
    }

    // Check memory health (critical if less than 32KB free)
    current_health.memory_healthy = (ESP.getFreeHeap() > 32768);

    // I2C health would need to be set externally by I2C module
    // For now, assume healthy unless proven otherwise
    current_health.i2c_healthy = true;
}

void profilerGetHealthStatus(health_status_t* status) {
    if (!status) return;
    profilerUpdateHealth();
    memcpy(status, &current_health, sizeof(health_status_t));
}
