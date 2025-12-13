/**
 * @file load_manager.h
 * @brief Graceful Degradation Under Load (PHASE 5.3)
 * @details Multi-level response to system overload conditions
 * @project BISSO E350 Controller
 */

#ifndef LOAD_MANAGER_H
#define LOAD_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load state machine levels
 */
typedef enum {
    LOAD_STATE_NORMAL = 0,      // CPU < 75%, normal operation
    LOAD_STATE_ELEVATED = 1,    // CPU 75-85%, reduce refresh rates
    LOAD_STATE_HIGH = 2,        // CPU 85-95%, suspend non-essential tasks
    LOAD_STATE_CRITICAL = 3     // CPU > 95%, emergency mode
} load_state_t;

/**
 * Load state information
 */
typedef struct {
    load_state_t current_state;
    load_state_t previous_state;
    uint8_t current_cpu_percent;
    uint32_t state_entry_time_ms;
    uint32_t time_in_state_ms;
    bool state_changed;
    bool emergency_estop_initiated;
} load_status_t;

/**
 * Initialize load manager
 */
void loadManagerInit();

/**
 * Update load state based on current CPU usage
 * Call from monitor task regularly
 */
void loadManagerUpdate();

/**
 * Get current load state
 * @return Current load status
 */
load_status_t loadManagerGetStatus();

/**
 * Check if specific subsystem should be active
 * @param subsystem_id Subsystem identifier (MONITOR, LCD, TELEMETRY, etc.)
 * @return true if subsystem should be active
 */
bool loadManagerIsSubsystemActive(uint8_t subsystem_id);

/**
 * Get adjusted refresh rate for a subsystem
 * @param base_refresh_ms Normal refresh rate
 * @param subsystem_id Subsystem to query
 * @return Adjusted refresh rate in ms
 */
uint32_t loadManagerGetAdjustedRefreshRate(uint32_t base_refresh_ms, uint8_t subsystem_id);

/**
 * Check if we should skip optional operations
 * @return true if system is under load and should skip non-essential work
 */
bool loadManagerIsUnderLoad();

/**
 * Force transition to specific load state (for testing)
 * @param state Desired state
 */
void loadManagerForceState(load_state_t state);

/**
 * Get human-readable state string
 * @param state Load state
 * @return State name
 */
const char* loadManagerGetStateString(load_state_t state);

/**
 * Print load manager diagnostics
 */
void loadManagerPrintStatus();

/**
 * Subsystem IDs
 */
#define LOAD_SUBSYS_MONITOR      0x01
#define LOAD_SUBSYS_LCD          0x02
#define LOAD_SUBSYS_TELEMETRY    0x04
#define LOAD_SUBSYS_LOGGING      0x08
#define LOAD_SUBSYS_ENCODER      0x10
#define LOAD_SUBSYS_API          0x20

#ifdef __cplusplus
}
#endif

#endif // LOAD_MANAGER_H
