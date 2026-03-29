/**
 * @file motion_state_machine.cpp
 * @brief Implementation of formal state machine for motion control
 * @project BISSO E350 Controller
 */

#include "motion_state_machine.h"
#include "motion.h"
#include "axis.h"
#include "config_unified.h"
#include "config_keys.h"
#include "hardware_config.h" // For machineCal axis calibration
#include "system_constants.h" // For MOTION_POSITION_SCALE_FACTOR
#include "plc_iface.h"      // ELBO PLC I2C interface (replaces elbo_q73.h and elbo_i73.h)
#include "encoder_wj66.h"
#include "system_tuning.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "board_inputs.h"
#include "system_events.h" // Event-driven architecture
#include <Arduino.h>

// External spinlock for thread-safe state access
extern portMUX_TYPE motionSpinlock;

// ============================================================================
// STATE TABLE - Defines all states and their handlers
// ============================================================================

static const state_definition_t state_table[] = {
    // State                        Name             Entry              Handler                          Exit
    {MOTION_IDLE,                   "IDLE",          state_idle_entry,  state_idle_handler,              NULL},
    {MOTION_WAIT_CONSENSO,          "WAIT_CONSENSO", NULL,              state_wait_consenso_handler,     NULL},
    {MOTION_EXECUTING,              "EXECUTING",     state_executing_entry, state_executing_handler,     NULL},
    {MOTION_STOPPING,               "STOPPING",      state_stopping_entry,  state_stopping_handler,      NULL},
    {MOTION_PAUSED,                 "PAUSED",        NULL,              state_paused_handler,            NULL},
    {MOTION_ERROR,                  "ERROR",         state_error_entry, state_error_handler,             NULL},
    {MOTION_HOMING_APPROACH_FAST,   "HOMING_FAST",   NULL,              state_homing_approach_fast_handler, NULL},
    {MOTION_HOMING_BACKOFF,         "HOMING_BACKOFF",NULL,              state_homing_backoff_handler,    NULL},
    {MOTION_HOMING_APPROACH_FINE,   "HOMING_FINE",   NULL,              state_homing_approach_fine_handler, NULL},
    {MOTION_HOMING_SETTLE,          "HOMING_SETTLE", NULL,              state_homing_settle_handler,     NULL},
    {MOTION_DWELL,                  "DWELL",         NULL,              state_dwell_handler,             NULL},
    {MOTION_WAIT_PIN,               "WAIT_PIN",      NULL,              state_wait_pin_handler,          NULL}
};

static const size_t state_table_size = sizeof(state_table) / sizeof(state_table[0]);

// ============================================================================
// STATE MACHINE METHODS
// ============================================================================

void MotionStateMachine::init() {
    logInfo("[FSM] Motion state machine initialized with %d states", state_table_size);
}

const state_definition_t* MotionStateMachine::getStateDefinition(motion_state_t state) {
    for (size_t i = 0; i < state_table_size; i++) {
        if (state_table[i].state == state) {
            return &state_table[i];
        }
    }
    return NULL;
}

void MotionStateMachine::executeEntryAction(Axis* axis, motion_state_t state) {
    const state_definition_t* def = getStateDefinition(state);
    if (def && def->on_entry) {
        def->on_entry(axis);
    }
}

void MotionStateMachine::executeExitAction(Axis* axis, motion_state_t state) {
    const state_definition_t* def = getStateDefinition(state);
    if (def && def->on_exit) {
        def->on_exit(axis);
    }
}

// --- TRANSITION TABLE ---
// Defines ALL valid motion state changes. 
// Any transition not directly listed here is blocked.
struct MotionTransition {
    motion_state_t from;
    motion_state_t to;
};

static const MotionTransition motion_transition_table[] = {
    // FROM IDLE
    {MOTION_IDLE, MOTION_EXECUTING},
    {MOTION_IDLE, MOTION_HOMING_APPROACH_FAST},
    {MOTION_IDLE, MOTION_STOPPING},
    {MOTION_IDLE, MOTION_ERROR},

    // FROM EXECUTING
    {MOTION_EXECUTING, MOTION_IDLE},      // Natural end
    {MOTION_EXECUTING, MOTION_STOPPING},  // Operator stop
    {MOTION_EXECUTING, MOTION_ERROR},     // Fault

    // FROM HOMING SEQUENCE
    {MOTION_HOMING_APPROACH_FAST, MOTION_HOMING_BACKOFF},
    {MOTION_HOMING_APPROACH_FAST, MOTION_ERROR},
    {MOTION_HOMING_BACKOFF,       MOTION_HOMING_APPROACH_FINE},
    {MOTION_HOMING_BACKOFF,       MOTION_ERROR},
    {MOTION_HOMING_APPROACH_FINE, MOTION_HOMING_SETTLE},
    {MOTION_HOMING_APPROACH_FINE, MOTION_ERROR},
    {MOTION_HOMING_SETTLE,        MOTION_IDLE},
    {MOTION_HOMING_SETTLE,        MOTION_ERROR},

    // FROM STOPPING
    {MOTION_STOPPING, MOTION_IDLE},
    {MOTION_STOPPING, MOTION_ERROR},

    // FROM ERROR
    {MOTION_ERROR, MOTION_IDLE}           // Recovery
};

static const size_t MOTION_TRANSITION_COUNT = sizeof(motion_transition_table) / sizeof(motion_transition_table[0]);

bool MotionStateMachine::isValidTransition(motion_state_t from_state, motion_state_t to_state) {
    if (from_state == to_state) return true;

    // Direct transition to ERROR is always allowed
    if (to_state == MOTION_ERROR) return true;

    for (size_t i = 0; i < MOTION_TRANSITION_COUNT; i++) {
        if (motion_transition_table[i].from == from_state && 
            motion_transition_table[i].to == to_state) {
            return true;
        }
    }

    logWarning("[MOTION] Blocked invalid state transition: %s -> %s", 
               motionStateToString(from_state), motionStateToString(to_state));
    return false;
}

bool MotionStateMachine::transitionTo(Axis* axis, motion_state_t new_state) {
    if (!axis) {
        return false;
    }

    portENTER_CRITICAL(&motionSpinlock);
    motion_state_t old_state = axis->state;
    portEXIT_CRITICAL(&motionSpinlock);

    // Check if transition is valid
    if (!isValidTransition(old_state, new_state)) {
        logWarning("[FSM] Invalid transition from %s to %s on axis %d",
                   motionStateToString(old_state),
                   motionStateToString(new_state),
                   axis->id);
        return false;
    }

    // Execute exit action for old state
    executeExitAction(axis, old_state);

    // Perform thread-safe state transition
    portENTER_CRITICAL(&motionSpinlock);
    axis->state = new_state;
    axis->state_entry_ms = millis();
    portEXIT_CRITICAL(&motionSpinlock);

    // Execute entry action for new state
    executeEntryAction(axis, new_state);

    logDebug("[FSM] Axis %d: %s -> %s", axis->id,
             motionStateToString(old_state),
             motionStateToString(new_state));

    // Signal event group for state changes
    // This allows tasks to wake up immediately instead of polling
    systemEventsMotionSet(EVENT_MOTION_STATE_CHANGE);

    // Signal specific events based on new state
    switch (new_state) {
        case MOTION_IDLE:
            systemEventsMotionSet(EVENT_MOTION_IDLE);
            if (old_state == MOTION_EXECUTING || old_state == MOTION_STOPPING) {
                systemEventsMotionSet(EVENT_MOTION_COMPLETED);
            }
            break;

        case MOTION_EXECUTING:
            systemEventsMotionSet(EVENT_MOTION_STARTED);
            break;

        case MOTION_STOPPING:
            systemEventsMotionSet(EVENT_MOTION_STOPPED);
            break;

        case MOTION_ERROR:
            systemEventsMotionSet(EVENT_MOTION_ERROR);
            break;

        case MOTION_HOMING_APPROACH_FAST:
            systemEventsMotionSet(EVENT_MOTION_HOMING_START);
            break;

        case MOTION_HOMING_SETTLE:
            // Clear homing start, will set complete after settle
            systemEventsMotionClear(EVENT_MOTION_HOMING_START);
            break;

        default:
            break;
    }

    return true;
}

void MotionStateMachine::update(Axis* axis, int32_t current_pos,
                                int32_t global_target, bool consensus_active) {
    if (!axis) {
        return;
    }

    // Get current state (thread-safe read)
    portENTER_CRITICAL(&motionSpinlock);
    motion_state_t current_state = axis->state;
    portEXIT_CRITICAL(&motionSpinlock);

    // Don't process if axis is disabled
    if (!axis->enabled) {
        return;
    }

    // Look up state handler
    const state_definition_t* def = getStateDefinition(current_state);
    if (def && def->handler) {
        // Execute state handler
        def->handler(axis, current_pos, global_target, consensus_active);
    } else {
        logWarning("[FSM] No handler for state %d on axis %d", current_state, axis->id);
    }
}

// ============================================================================
// STATE HANDLERS - Implementation of state-specific logic
// ============================================================================

void state_idle_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // IDLE state - no action needed, waiting for commands
}

void state_wait_consenso_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // Check for hardware presence - if missing, mock consensus to allow simulation
    if (!plcIsHardwarePresent()) {
        MotionStateMachine::transitionTo(axis, MOTION_EXECUTING);
        return;
    }

    // Check for timeout
    uint32_t now = millis();
    uint32_t elapsed = (now >= axis->state_entry_ms) ? (now - axis->state_entry_ms) : (UINT32_MAX - axis->state_entry_ms + now + 1);
    if (elapsed > MOTION_CONSENSO_TIMEOUT_MS) {
        faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, axis->id, 0, "Consensus Timeout");
        MotionStateMachine::transitionTo(axis, MOTION_ERROR);
        return;
    }

    // Check if consensus is active
    if (consensus) {
        MotionStateMachine::transitionTo(axis, MOTION_EXECUTING);
    }
}

void state_executing_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // Get configurable target margin (in mm, convert to counts)
    float margin_mm = configGetFloat(KEY_TARGET_MARGIN, 0.1f);
    
    // Get scale factor for this axis (centralized helper)
    float scale = motionGetAxisScale(axis->id);
    int32_t margin_counts = (int32_t)(margin_mm * scale);
    if (margin_counts < 1) margin_counts = 1; // Minimum 1 count margin
    
    // Check if target reached (within margin OR crossed target)
    bool target_reached = false;
    int32_t dist_to_target = abs(pos - target);
    
    // Within margin = on target
    if (dist_to_target <= margin_counts) {
        target_reached = true;
    } else {
        // Also check for overshoot (crossed target)
        int32_t start_pos = axis->active_start_position;
        if (start_pos < target && pos >= target) {
            target_reached = true;
        } else if (start_pos > target && pos <= target) {
            target_reached = true;
        }
    }

    if (target_reached) {
        axis->position_at_stop = pos;
        motionSetPLCAxisDirection(255, false, false); // Stop all axes
        MotionStateMachine::transitionTo(axis, MOTION_STOPPING);
    }
}

void state_stopping_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // Get configurable target margin (in mm, convert to counts)
    float margin_mm = configGetFloat(KEY_TARGET_MARGIN, 0.1f);
    
    // Get scale factor for this axis (centralized helper)
    float scale = motionGetAxisScale(axis->id);
    int32_t margin_counts = (int32_t)(margin_mm * scale);
    if (margin_counts < 1) margin_counts = 1;

    // Check if stopped within margin (position on target)
    if (abs(pos - target) <= margin_counts) {
        MotionStateMachine::transitionTo(axis, MOTION_IDLE);
        return;
    }

    // Position Hunting Logic
    // If we've settled for at least 600ms (typical mechanical bounce time)
    // and we are still outside margin, we check if we should "hunt" back.
    // SAFETY: Restrict hunting to X (0) and Z (2) axes. 
    // Hunting is intentionally disabled for the Y-axis (Cutting) because an automatic 
    // position correction (reversing) while the blade is inside the material could 
    // cause immediate blade breakage or severe damage to the workpiece/machine.
    uint32_t now = millis();
    uint32_t settle_time = (now >= axis->state_entry_ms) ? (now - axis->state_entry_ms) : (UINT32_MAX - axis->state_entry_ms + now + 1);
    if ((axis->id == 0 || axis->id == 2) && settle_time > 600) {
        logInfo("[AXIS %d] Position hunt: error=%ld counts. Reversing...", 
                axis->id, (long)(target - pos));
        
        // Re-engage motion towards target using Slow profile (Speed Profile 1)
        bool is_fwd = (target > pos);
        plcBeginTransaction();
        motionSetPLCSpeedProfile(SPEED_PROFILE_1); // Forced slow speed for hunting
        motionSetPLCAxisDirection(axis->id, true, is_fwd);
        plcEndTransaction();
        
        // Transition back to EXECUTING
        // Note: state_executing_entry will auto-update active_start_position
        MotionStateMachine::transitionTo(axis, MOTION_EXECUTING);
        return;
    }

    // Check for timeout (Safety fallback if hunting fails or gets stuck)
    uint32_t timeout = configGetInt(KEY_STOP_TIMEOUT, 5000);
    uint32_t now_stop = millis();
    uint32_t elapsed_stop = (now_stop >= axis->state_entry_ms) ? (now_stop - axis->state_entry_ms) : (UINT32_MAX - axis->state_entry_ms + now_stop + 1);
    if (elapsed_stop > timeout) {
        logWarning("[AXIS %d] Stop settlement timeout (pos=%ld, target=%ld, margin=%ld)", 
                   axis->id, (long)pos, (long)target, (long)margin_counts);
        MotionStateMachine::transitionTo(axis, MOTION_IDLE);
    }
}

void state_paused_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // PAUSED state - waiting for resume command
    // State change handled by motionResume() API
}

void state_error_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // ERROR state - waiting for clearance
    // State change handled by motionClearEmergencyStop() or reset commands
}

void state_homing_approach_fast_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    bool hit = elboI73GetInput(AXIS_TO_I73_BIT[axis->id]);

    // Check for timeout
    uint32_t now = millis();
    uint32_t elapsed = (now >= axis->state_entry_ms) ? (now - axis->state_entry_ms) : (UINT32_MAX - axis->state_entry_ms + now + 1);
    if (elapsed > 45000) {
        logError("[HOME] Timeout on axis %d", axis->id);
        MotionStateMachine::transitionTo(axis, MOTION_ERROR);
        return;
    }

    // Check if limit switch hit
    if (hit) {
        motionSetPLCAxisDirection(255, false, false);
        int slow_prof = configGetInt(KEY_HOME_PROFILE_SLOW, 0);
        motionSetPLCSpeedProfile((speed_profile_t)slow_prof);
        motionSetPLCAxisDirection(axis->id, true, true); // Reverse direction
        MotionStateMachine::transitionTo(axis, MOTION_HOMING_BACKOFF);
    }
}

void state_homing_backoff_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // Wait until limit switch is released
    if (!elboI73GetInput(AXIS_TO_I73_BIT[axis->id])) {
        // Wait a bit after switch release
        uint32_t now = millis();
        uint32_t elapsed = (now >= axis->state_entry_ms) ? (now - axis->state_entry_ms) : (UINT32_MAX - axis->state_entry_ms + now + 1);
        if (elapsed > 1000) {
            motionSetPLCAxisDirection(255, false, false);
            int slow_prof = configGetInt(KEY_HOME_PROFILE_SLOW, 0);
            motionSetPLCSpeedProfile((speed_profile_t)slow_prof);
            motionSetPLCAxisDirection(axis->id, true, false); // Forward again
            MotionStateMachine::transitionTo(axis, MOTION_HOMING_APPROACH_FINE);
        }
    }
}

void state_homing_approach_fine_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    bool hit = elboI73GetInput(AXIS_TO_I73_BIT[axis->id]);

    // Check for timeout
    uint32_t now = millis();
    uint32_t elapsed = (now >= axis->state_entry_ms) ? (now - axis->state_entry_ms) : (UINT32_MAX - axis->state_entry_ms + now + 1);
    if (elapsed > 45000) {
        logError("[HOME] Fine approach timeout on axis %d", axis->id);
        MotionStateMachine::transitionTo(axis, MOTION_ERROR);
        return;
    }

    // Check if limit switch hit again (fine position)
    if (hit) {
        axis->homing_trigger_pos = pos;
        MotionStateMachine::transitionTo(axis, MOTION_HOMING_SETTLE);
    }
}

void state_homing_settle_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // Wait for mechanical settling
    uint32_t now = millis();
    uint32_t elapsed = (now >= axis->state_entry_ms) ? (now - axis->state_entry_ms) : (UINT32_MAX - axis->state_entry_ms + now + 1);
    if (elapsed > HOMING_SETTLE_MS) {
        // Zero the encoder
        wj66SetZero(axis->id);
        axis->position = 0;
        axis->target_position = 0;

        logInfo("[HOME] Axis %d zeroed", axis->id);

        // Signal homing completion event before transitioning
        systemEventsMotionSet(EVENT_MOTION_HOMING_COMPLETE);

        MotionStateMachine::transitionTo(axis, MOTION_IDLE);
    }
}

void state_dwell_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    // Wait for dwell timer to expire
    // dwell_end_ms is an absolute time in the future
    uint32_t now = millis();
    if ((int32_t)(now - axis->dwell_end_ms) >= 0) {
        logInfo("[MOTION] Dwell complete on axis %d", axis->id);
        MotionStateMachine::transitionTo(axis, MOTION_IDLE);
    }
}

void state_wait_pin_handler(Axis* axis, int32_t pos, int32_t target, bool consensus) {
    bool pin_state = false;
    bool pin_ready = false;

    // Read pin state based on type
    if (axis->wait_pin_type == 0) {
        // I73 input (ELBO PLC)
        pin_state = elboI73GetInput(axis->wait_pin_id);
        pin_ready = true;
    } else if (axis->wait_pin_type == 1) {
        // Board input (KC868-A16)
        button_state_t buttons = boardInputsUpdate();
        if (axis->wait_pin_id == 0)
            pin_state = buttons.estop_active;
        else if (axis->wait_pin_id == 1)
            pin_state = buttons.pause_pressed;
        else if (axis->wait_pin_id == 2)
            pin_state = buttons.resume_pressed;
        else
            pin_state = false;
        pin_ready = buttons.connection_ok;
    } else if (axis->wait_pin_type == 2) {
        // Direct ESP32 GPIO
        pinMode(axis->wait_pin_id, INPUT);
        pin_state = (digitalRead(axis->wait_pin_id) == HIGH);
        pin_ready = true;
    }

    // Check if pin state matches what we're waiting for
    if (pin_ready && pin_state == axis->wait_pin_state) {
        logInfo("[MOTION] Pin %d state %d detected on axis %d",
                axis->wait_pin_id, axis->wait_pin_state, axis->id);
        MotionStateMachine::transitionTo(axis, MOTION_IDLE);
        return;
    }

    // Check for timeout
    uint32_t now = millis();
    uint32_t elapsed = (now >= axis->state_entry_ms) ? (now - axis->state_entry_ms) : (UINT32_MAX - axis->state_entry_ms + now + 1);
    if (axis->wait_pin_timeout_ms > 0 && elapsed >= axis->wait_pin_timeout_ms) {
        faultLogEntry(FAULT_WARNING, FAULT_MOTION_TIMEOUT, axis->id, 0, "Pin wait timeout");
        logWarning("[MOTION] Pin %d wait timeout on axis %d", axis->wait_pin_id, axis->id);
        MotionStateMachine::transitionTo(axis, MOTION_ERROR);
    }
}

// ============================================================================
// ENTRY/EXIT CALLBACKS
// ============================================================================

void state_idle_entry(Axis* axis) {
    // Clear active axis when entering IDLE
    motionClearActiveAxis();
}

void state_executing_entry(Axis* axis) {
    // Record start position for target detection
    axis->active_start_position = axis->position;
}

void state_stopping_entry(Axis* axis) {
    // Record position when stopping started
    axis->position_at_stop = axis->position;
}

void state_error_entry(Axis* axis) {
    // Stop all motion when entering error state
    motionSetPLCAxisDirection(255, false, false);

    // Log error entry
    logError("[FSM] Axis %d entered ERROR state", axis->id);
}
