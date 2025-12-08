/**
 * @file performance_profiler.h
 * @brief Real-Time Performance Profiling & Instrumentation
 * @details Lightweight profiling system for measuring execution times,
 *          detecting bottlenecks, and monitoring system health in real-time.
 * @author Sergio Faustino
 */

#ifndef PERFORMANCE_PROFILER_H
#define PERFORMANCE_PROFILER_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

// Enable/disable profiling at compile time (set to 0 to remove overhead)
#ifndef PROFILING_ENABLED
#define PROFILING_ENABLED 1
#endif

// Maximum number of profiled sections
#define PROFILER_MAX_SECTIONS 32

// Warning thresholds (microseconds)
#define PROFILE_WARN_MOTION_LOOP_US 500    // 5% of 10ms budget
#define PROFILE_WARN_SAFETY_LOOP_US 250    // 5% of 5ms budget
#define PROFILE_WARN_ENCODER_LOOP_US 500   // 2.5% of 20ms budget

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct {
    const char* name;              // Section name (e.g., "Motion Update")
    uint32_t call_count;           // Number of times called
    uint32_t total_time_us;        // Total time spent (microseconds)
    uint32_t min_time_us;          // Fastest execution
    uint32_t max_time_us;          // Slowest execution
    uint32_t last_time_us;         // Most recent execution
    uint32_t warning_count;        // Times threshold was exceeded
    uint32_t warning_threshold_us; // Warning threshold (0 = no warning)
    uint32_t last_warning_time;    // Last time warning was logged
    bool active;                   // Currently timing this section
    uint32_t start_time_us;        // Start timestamp for active measurement
} profile_section_t;

typedef struct {
    uint32_t heap_free;            // Free heap in bytes
    uint32_t heap_min;             // Minimum free heap since boot
    uint32_t heap_max_block;       // Largest allocatable block
    uint32_t task_switches;        // Context switch count (if available)
    float cpu_usage_percent;       // Estimated CPU usage
} system_snapshot_t;

// ============================================================================
// PROFILER API
// ============================================================================

/**
 * @brief Initialize the profiler system
 * @note Call once during system startup
 */
void profilerInit();

/**
 * @brief Register a new profiling section
 * @param name Human-readable name for this section
 * @param warning_threshold_us Warning threshold in microseconds (0 = no warning)
 * @return Section ID, or -1 if registration failed
 */
int profilerRegisterSection(const char* name, uint32_t warning_threshold_us);

/**
 * @brief Start timing a section
 * @param section_id ID returned by profilerRegisterSection
 */
void profilerStart(int section_id);

/**
 * @brief Stop timing a section and record statistics
 * @param section_id ID returned by profilerRegisterSection
 */
void profilerStop(int section_id);

/**
 * @brief Get statistics for a specific section
 * @param section_id ID returned by profilerRegisterSection
 * @return Pointer to profile data, or NULL if invalid ID
 */
const profile_section_t* profilerGetSection(int section_id);

/**
 * @brief Print formatted profiling report to serial
 * @param detailed If true, shows all sections. If false, only active sections.
 */
void profilerPrintReport(bool detailed);

/**
 * @brief Reset all profiling statistics
 */
void profilerReset();

/**
 * @brief Take a snapshot of current system state
 * @param snapshot Output structure to fill
 */
void profilerGetSystemSnapshot(system_snapshot_t* snapshot);

/**
 * @brief Check if any section is currently over its threshold
 * @return Number of sections currently over threshold
 */
int profilerGetActiveWarnings();

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

#if PROFILING_ENABLED

/**
 * @brief Define and register a profiled section (call once globally)
 * @example PROFILE_DEFINE(motion_update, 500);  // 500µs threshold
 */
#define PROFILE_DEFINE(name, threshold_us) \
    static int _profile_id_##name = -1; \
    static void _profile_init_##name() __attribute__((constructor)); \
    static void _profile_init_##name() { \
        _profile_id_##name = profilerRegisterSection(#name, threshold_us); \
    }

/**
 * @brief Start profiling a section
 * @example PROFILE_START(motion_update);
 */
#define PROFILE_START(name) \
    do { \
        extern int _profile_id_##name; \
        if (_profile_id_##name >= 0) profilerStart(_profile_id_##name); \
    } while(0)

/**
 * @brief Stop profiling a section
 * @example PROFILE_STOP(motion_update);
 */
#define PROFILE_STOP(name) \
    do { \
        extern int _profile_id_##name; \
        if (_profile_id_##name >= 0) profilerStop(_profile_id_##name); \
    } while(0)

/**
 * @brief Profile an entire function automatically
 * @note Place at start of function
 * @example
 * void myFunction() {
 *     PROFILE_FUNCTION(500);  // Warn if exceeds 500µs
 *     // ... function code ...
 * }
 */
#define PROFILE_FUNCTION(threshold_us) \
    static int _profile_fn_id = -1; \
    if (_profile_fn_id < 0) { \
        _profile_fn_id = profilerRegisterSection(__FUNCTION__, threshold_us); \
    } \
    ProfilerScope _profile_scope(_profile_fn_id);

/**
 * @brief Profile a code block
 * @example
 * {
 *     PROFILE_SCOPE("I2C Transaction", 100);
 *     Wire.beginTransmission(...);
 *     // ...
 * }  // Automatically stops timing when scope exits
 */
#define PROFILE_SCOPE(name, threshold_us) \
    static int _profile_scope_id = -1; \
    if (_profile_scope_id < 0) { \
        _profile_scope_id = profilerRegisterSection(name, threshold_us); \
    } \
    ProfilerScope _profile_scope_obj(_profile_scope_id);

/**
 * @brief Log a timing measurement directly
 * @example PROFILE_LOG("Calculation", start_time);
 */
#define PROFILE_LOG(name, start_us) \
    do { \
        uint32_t _elapsed = micros() - (start_us); \
        Serial.printf("[PROFILE] %s: %lu µs\n", name, (unsigned long)_elapsed); \
    } while(0)

#else
// Profiling disabled - macros become no-ops
#define PROFILE_DEFINE(name, threshold_us)
#define PROFILE_START(name)
#define PROFILE_STOP(name)
#define PROFILE_FUNCTION(threshold_us)
#define PROFILE_SCOPE(name, threshold_us)
#define PROFILE_LOG(name, start_us)
#endif

// ============================================================================
// RAII HELPER (automatic start/stop via scope)
// ============================================================================

class ProfilerScope {
public:
    ProfilerScope(int section_id) : _id(section_id) {
        #if PROFILING_ENABLED
        if (_id >= 0) profilerStart(_id);
        #endif
    }

    ~ProfilerScope() {
        #if PROFILING_ENABLED
        if (_id >= 0) profilerStop(_id);
        #endif
    }

private:
    int _id;
};

// ============================================================================
// REAL-TIME MONITOR (for continuous health checks)
// ============================================================================

typedef struct {
    bool motion_loop_healthy;      // Motion loop within budget
    bool safety_loop_healthy;      // Safety loop within budget
    bool memory_healthy;           // Heap not critically low
    bool i2c_healthy;              // I2C not failing
    uint32_t warnings_last_minute; // Warning count in last 60s
} health_status_t;

/**
 * @brief Get overall system health status
 * @param status Output structure
 */
void profilerGetHealthStatus(health_status_t* status);

/**
 * @brief Update health monitoring (call periodically, e.g., 1Hz)
 */
void profilerUpdateHealth();

#endif // PERFORMANCE_PROFILER_H
