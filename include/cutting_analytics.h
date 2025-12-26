/**
 * @file cutting_analytics.h
 * @brief Stone Cutting Analytics Module
 * @project BISSO E350 Controller
 * @details Computes power, SCE, and blade health from motor sensors.
 */

#ifndef CUTTING_ANALYTICS_H
#define CUTTING_ANALYTICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

/**
 * @brief Cutting analytics configuration
 */
typedef struct {
    float motor_voltage_v;      // Line voltage (default 230V)
    float motor_efficiency;     // Motor efficiency (default 0.85)
    float blade_width_mm;       // Blade kerf width (default 3.0mm)
    float cut_depth_mm;         // Current cutting depth (default 20.0mm)
    float power_factor;         // Power factor (default 0.8 for induction motor)
} cutting_config_t;

// ============================================================================
// SESSION STATE
// ============================================================================

/**
 * @brief Per-cut session statistics
 */
typedef struct {
    uint32_t start_time_ms;     // Session start timestamp
    uint32_t end_time_ms;       // Session end timestamp (0 if active)
    float total_energy_joules;  // Cumulative energy consumed
    float total_material_mm3;   // Cumulative material removed
    float avg_sce;              // Average SCE for session
    float peak_current_amps;    // Peak current during session
    float peak_power_watts;     // Peak power during session
    uint32_t sample_count;      // Number of samples collected
} cutting_session_t;

// ============================================================================
// REAL-TIME STATE
// ============================================================================

/**
 * @brief Real-time cutting analytics state
 */
typedef struct {
    // Configuration
    cutting_config_t config;
    
    // Real-time values
    float current_amps;         // Latest motor current
    float rpm;                  // Latest spindle RPM (0 if sensor disabled)
    float feed_rate_mms;        // Latest feed rate (mm/s)
    float power_watts;          // Calculated power
    float mrr_mm3s;             // Material removal rate (mm³/s)
    float sce_jmm3;             // Specific cutting energy (J/mm³)
    
    // Running statistics
    float avg_current_amps;     // Rolling average current
    float avg_power_watts;      // Rolling average power
    float avg_sce_jmm3;         // Rolling average SCE
    float peak_current_amps;    // Peak current (all time)
    float peak_power_watts;     // Peak power (all time)
    
    // Blade health
    float baseline_sce;         // SCE baseline for current material
    float sce_deviation_pct;    // % deviation from baseline (wear indicator)
    bool blade_alert;           // True if SCE deviation exceeds threshold
    
    // Session tracking
    bool session_active;        // True if actively cutting
    cutting_session_t session;  // Current/last session stats
    
    // System
    uint32_t update_count;      // Total update cycles
    uint32_t last_update_ms;    // Last update timestamp
    bool enabled;               // Analytics enabled flag
} cutting_analytics_state_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * @brief Initialize cutting analytics module
 */
void cuttingAnalyticsInit(void);

/**
 * @brief Update analytics (call from telemetry task at 10 Hz)
 */
void cuttingAnalyticsUpdate(void);

/**
 * @brief Start a new cutting session
 */
void cuttingStartSession(void);

/**
 * @brief End the current cutting session
 */
void cuttingEndSession(void);

/**
 * @brief Check if a cutting session is active
 */
bool cuttingIsSessionActive(void);

/**
 * @brief Get current analytics state
 * @return Pointer to state (read-only)
 */
const cutting_analytics_state_t* cuttingGetState(void);

/**
 * @brief Get current session data
 * @return Pointer to session (read-only)
 */
const cutting_session_t* cuttingGetSession(void);

// ============================================================================
// CONFIGURATION API
// ============================================================================

/**
 * @brief Set cutting depth for SCE calculation
 * @param depth_mm Depth of cut in mm
 */
void cuttingSetDepth(float depth_mm);

/**
 * @brief Set blade width for SCE calculation
 * @param width_mm Blade kerf width in mm
 */
void cuttingSetBladeWidth(float width_mm);

/**
 * @brief Set motor parameters
 * @param voltage_v Line voltage
 * @param efficiency Motor efficiency (0.0-1.0)
 * @param power_factor Power factor (0.0-1.0)
 */
void cuttingSetMotorParams(float voltage_v, float efficiency, float power_factor);

/**
 * @brief Set SCE baseline for blade health monitoring
 * @param baseline_sce SCE value to use as reference (J/mm³)
 */
void cuttingSetSCEBaseline(float baseline_sce);

/**
 * @brief Enable/disable analytics
 * @param enable True to enable
 */
void cuttingSetEnabled(bool enable);

// ============================================================================
// DIAGNOSTICS
// ============================================================================

/**
 * @brief Reset all statistics and session data
 */
void cuttingResetStats(void);

/**
 * @brief Print diagnostics to serial console
 */
void cuttingPrintDiagnostics(void);

/**
 * @brief Export analytics to JSON buffer
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Bytes written
 */
size_t cuttingExportJSON(char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // CUTTING_ANALYTICS_H
