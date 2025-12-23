/**
 * @file load_manager.cpp
 * @brief Load Manager Implementation (PHASE 5.3)
 */

#include "load_manager.h"
#include "task_manager.h"
#include "serial_logger.h"
#include "safety.h"
#include "motion.h"  // PHASE 5.10: For motionEmergencyStop()
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// Load thresholds (CPU percentage)
#define LOAD_THRESHOLD_NORMAL       75
#define LOAD_THRESHOLD_ELEVATED     85
#define LOAD_THRESHOLD_HIGH         95
#define LOAD_THRESHOLD_CRITICAL     98

// State persistence
#define CRITICAL_LOAD_TIMEOUT_MS    30000  // E-STOP after 30 seconds at critical load

// Load state tracking
static struct {
    load_state_t current_state;
    load_state_t previous_state;
    uint8_t current_cpu_percent;
    uint32_t state_entry_time_ms;
    bool state_changed;
    bool emergency_triggered;
    uint32_t critical_load_start_ms;
} load_state = {
    .current_state = LOAD_STATE_NORMAL,
    .previous_state = LOAD_STATE_NORMAL,
    .current_cpu_percent = 0,
    .state_entry_time_ms = 0,
    .state_changed = false,
    .emergency_triggered = false,
    .critical_load_start_ms = 0
};

const char* loadManagerGetStateString(load_state_t state) {
    switch (state) {
        case LOAD_STATE_NORMAL: return "NORMAL";
        case LOAD_STATE_ELEVATED: return "ELEVATED";
        case LOAD_STATE_HIGH: return "HIGH";
        case LOAD_STATE_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

void loadManagerInit() {
    load_state.current_state = LOAD_STATE_NORMAL;
    load_state.previous_state = LOAD_STATE_NORMAL;
    load_state.state_entry_time_ms = millis();
    load_state.state_changed = false;
    load_state.emergency_triggered = false;
    load_state.critical_load_start_ms = 0;

    logInfo("[LOAD_MGR] Initialized with thresholds: %u%%/%u%%/%u%%",
           LOAD_THRESHOLD_ELEVATED, LOAD_THRESHOLD_HIGH, LOAD_THRESHOLD_CRITICAL);
}

static void transitionToState(load_state_t new_state) {
    if (new_state == load_state.current_state) {
        return;  // No transition needed
    }

    load_state.previous_state = load_state.current_state;
    load_state.current_state = new_state;
    load_state.state_entry_time_ms = millis();
    load_state.state_changed = true;

    logWarning("[LOAD_MGR] State transition: %s → %s (CPU: %u%%)",
              loadManagerGetStateString(load_state.previous_state),
              loadManagerGetStateString(new_state),
              load_state.current_cpu_percent);

    // Handle state-specific actions
    switch (new_state) {
        case LOAD_STATE_NORMAL:
            load_state.emergency_triggered = false;
            load_state.critical_load_start_ms = 0;
            logInfo("[LOAD_MGR] System recovered to NORMAL state");
            break;

        case LOAD_STATE_ELEVATED:
            Serial.println("[LOAD_MGR] [WARN] System entering ELEVATED load state");
            Serial.println("  - Reducing refresh rates");
            Serial.println("  - Increasing telemetry intervals");
            break;

        case LOAD_STATE_HIGH:
            Serial.println("[LOAD_MGR] [WARN] System entering HIGH load state");
            Serial.println("  - Suspending non-critical tasks");
            Serial.println("  - Reducing motion poll rate");
            logWarning("[LOAD_MGR] HIGH load detected - suspending telemetry");
            break;

        case LOAD_STATE_CRITICAL:
            Serial.println("[LOAD_MGR] [CRITICAL] System CRITICAL load state");
            Serial.println("  - All non-essential services suspended");
            Serial.println("  - E-STOP will trigger if condition persists 30 seconds");
            logWarning("[LOAD_MGR] CRITICAL load - emergency shutdown in 30 seconds if not resolved");
            load_state.critical_load_start_ms = millis();
            break;
    }
}

void loadManagerUpdate() {
    uint8_t cpu_usage = taskGetCpuUsage();
    load_state.current_cpu_percent = cpu_usage;
    load_state.state_changed = false;

    // State machine: determine new state based on CPU usage
    load_state_t new_state = load_state.current_state;

    if (cpu_usage < LOAD_THRESHOLD_NORMAL) {
        // Normal operation
        new_state = LOAD_STATE_NORMAL;
    } else if (cpu_usage < LOAD_THRESHOLD_ELEVATED) {
        // Elevated load
        new_state = LOAD_STATE_ELEVATED;
    } else if (cpu_usage < LOAD_THRESHOLD_HIGH) {
        // High load
        new_state = LOAD_STATE_HIGH;
    } else {
        // Critical load
        new_state = LOAD_STATE_CRITICAL;
    }

    // Transition if state changed
    if (new_state != load_state.current_state) {
        transitionToState(new_state);
    }

    // Handle critical load timeout - E-STOP after 30 seconds
    if (load_state.current_state == LOAD_STATE_CRITICAL) {
        uint32_t time_in_critical = millis() - load_state.critical_load_start_ms;

        if (time_in_critical > CRITICAL_LOAD_TIMEOUT_MS && !load_state.emergency_triggered) {
            load_state.emergency_triggered = true;
            logError("[LOAD_MGR] CRITICAL: Emergency E-STOP triggered after 30 seconds at max load");
            Serial.println("[LOAD_MGR] [CRITICAL] INITIATING EMERGENCY E-STOP DUE TO SYSTEM OVERLOAD");

            // PHASE 5.10: CRITICAL FIX - Actually trigger E-STOP instead of just logging
            motionEmergencyStop();

            logError("[LOAD_MGR] System overload detected - manual intervention required");
        }
    }

    // Hysteresis: require significant load drop to leave critical state
    if (load_state.current_state == LOAD_STATE_CRITICAL && cpu_usage < LOAD_THRESHOLD_HIGH - 10) {
        // Only leave critical state if CPU drops significantly
        transitionToState(LOAD_STATE_HIGH);
    }
}

load_status_t loadManagerGetStatus() {
    load_status_t status;
    status.current_state = load_state.current_state;
    status.previous_state = load_state.previous_state;
    status.current_cpu_percent = load_state.current_cpu_percent;
    status.state_entry_time_ms = load_state.state_entry_time_ms;
    status.time_in_state_ms = millis() - load_state.state_entry_time_ms;
    status.state_changed = load_state.state_changed;
    status.emergency_estop_initiated = load_state.emergency_triggered;
    return status;
}

bool loadManagerIsSubsystemActive(uint8_t subsystem_id) {
    // Determine which subsystems to disable at each load level
    switch (load_state.current_state) {
        case LOAD_STATE_NORMAL:
            return true;  // All active

        case LOAD_STATE_ELEVATED:
            // Keep critical systems, reduce others
            return true;  // All still active, just rate-limited

        case LOAD_STATE_HIGH:
            // Disable non-critical systems
            if (subsystem_id & (LOAD_SUBSYS_LCD | LOAD_SUBSYS_TELEMETRY | LOAD_SUBSYS_MONITOR)) {
                return false;
            }
            return true;

        case LOAD_STATE_CRITICAL:
            // Only keep safety-critical systems
            // Disable everything except motion/safety
            if (subsystem_id & (LOAD_SUBSYS_LCD | LOAD_SUBSYS_TELEMETRY |
                               LOAD_SUBSYS_MONITOR | LOAD_SUBSYS_API | LOAD_SUBSYS_LOGGING)) {
                return false;
            }
            return true;

        default:
            return false;
    }
}

uint32_t loadManagerGetAdjustedRefreshRate(uint32_t base_refresh_ms, uint8_t subsystem_id) {
    switch (load_state.current_state) {
        case LOAD_STATE_NORMAL:
            return base_refresh_ms;  // No adjustment

        case LOAD_STATE_ELEVATED:
            // Increase refresh rates by 50%
            return base_refresh_ms * 150 / 100;

        case LOAD_STATE_HIGH:
            // Double the refresh rates
            return base_refresh_ms * 200 / 100;

        case LOAD_STATE_CRITICAL:
            // Subsystems disabled in critical state
            return base_refresh_ms * 500 / 100;

        default:
            return base_refresh_ms;
    }
}

bool loadManagerIsUnderLoad() {
    return load_state.current_state != LOAD_STATE_NORMAL;
}

void loadManagerForceState(load_state_t state) {
    logInfo("[LOAD_MGR] Forcing state to %s (testing)", loadManagerGetStateString(state));
    transitionToState(state);
}

void loadManagerPrintStatus() {
    load_status_t status = loadManagerGetStatus();

    Serial.println("\n[LOAD_MGR] === System Load Status ===");
    Serial.printf("Current State: %s\n", loadManagerGetStateString(status.current_state));
    Serial.printf("CPU Usage: %u%%\n", status.current_cpu_percent);
    Serial.printf("Time in State: %lu ms\n", (unsigned long)status.time_in_state_ms);

    Serial.println("\nThresholds:");
    Serial.printf("  Normal    < %u%% CPU\n", LOAD_THRESHOLD_NORMAL);
    Serial.printf("  Elevated  %u-%u%% CPU (reduce refresh rates 50%%)\n",
                 LOAD_THRESHOLD_NORMAL, LOAD_THRESHOLD_ELEVATED);
    Serial.printf("  High      %u-%u%% CPU (suspend non-essential tasks)\n",
                 LOAD_THRESHOLD_ELEVATED, LOAD_THRESHOLD_HIGH);
    Serial.printf("  Critical  > %u%% CPU (emergency shutdown in 30s)\n", LOAD_THRESHOLD_HIGH);

    Serial.println("\nActions by State:");
    Serial.println("  NORMAL:    All subsystems active, normal refresh rates");
    Serial.println("  ELEVATED:  Reduce LCD refresh (100ms→200ms), increase telemetry (500ms→750ms)");
    Serial.println("  HIGH:      Suspend LCD/Monitor/Telemetry, reduce motion poll (10ms→20ms)");
    Serial.println("  CRITICAL:  Only safety-critical systems active, E-STOP after 30s");

    if (status.emergency_estop_initiated) {
        Serial.println("\n[!] EMERGENCY E-STOP HAS BEEN TRIGGERED");
    }

    Serial.println();
}
