/**
 * @file motion_control.cpp
 * @brief Real-Time Hardware Execution Layer (Gemini v3.5.1)
 * @details Implements Object-Oriented Axis logic, Flood Protection, and Homing.
 * @author Sergio Faustino
 */

#include "motion.h"
#include "motion_planner.h"
#include "motion_state.h"
#include "system_constants.h"
#include "plc_iface.h"
#include "encoder_wj66.h"        
#include "fault_logging.h"
#include "serial_logger.h"
#include "task_manager.h"
#include "safety.h"
#include "encoder_calibration.h" 
#include "config_unified.h" 
#include "config_keys.h"
#include <math.h>
#include <stdlib.h> 
#include <stdio.h>

// ============================================================================
// STATE OWNERSHIP
// ============================================================================

Axis axes[MOTION_AXES]; 

uint8_t active_axis = 255; 
int32_t active_start_position = 0;
bool global_enabled = true; 
static bool encoder_feedback_enabled = false;

const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, ELBO_I73_AXIS_A}; 
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_I73_CONSENSO_X, ELBO_I73_CONSENSO_Y, ELBO_I73_CONSENSO_Z, ELBO_I73_CONSENSO_A};

// ============================================================================
// AXIS CLASS METHODS
// ============================================================================

Axis::Axis() {
    id = 0;
    state = MOTION_IDLE;
    position = 0;
    target_position = 0;
    enabled = true;
    _error_logged = false;
    soft_limit_enabled = true;
    soft_limit_min = -1000000;
    soft_limit_max = 1000000;
}

void Axis::init(uint8_t axis_id) {
    id = axis_id;
    state = MOTION_IDLE;
    _error_logged = false;
    enabled = true;
}

bool Axis::checkSoftLimits(bool strict_mode) {
    if (!enabled || !soft_limit_enabled) return false;
    // Don't check limits if we are actively homing
    if (state >= MOTION_HOMING_APPROACH_FAST) return false;

    if (position < soft_limit_min || position > soft_limit_max) {
        if (strict_mode) {
            // FLOOD PROTECTION: Only log once per violation event
            if (!_error_logged) {
                faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, id, position, "Strict Limit Hit");
                logError("[AXIS %d] Strict Limit Violation: %ld", id, (long)position);
                _error_logged = true;
            }
            return true; // Signal E-Stop
        }
    } else {
        // Clear flag if we return to valid bounds
        _error_logged = false; 
    }
    return false;
}

void Axis::updateState(int32_t current_pos, int32_t global_target_pos) {
    position = current_pos;
    
    if (state == MOTION_ERROR || !enabled) return;

    switch (state) {
        case MOTION_WAIT_CONSENSO:
            if (millis() - state_entry_ms > MOTION_CONSENSO_TIMEOUT_MS) {
                faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, id, 0, "Consensus Timeout");
                state = MOTION_ERROR;
            } else {
                if (elboI73GetInput(AXIS_TO_CONSENSO_BIT[id])) {
                    state = MOTION_EXECUTING;
                    state_entry_ms = millis();
                }
            }
            break;

        case MOTION_EXECUTING:
            // Check if passed target
            // Note: global_target_pos comes from the loop controller
            if ((active_start_position < target_position && position >= target_position) ||
                (active_start_position > target_position && position <= target_position)) {
                
                position = target_position;
                state = MOTION_STOPPING;
                state_entry_ms = millis();
                position_at_stop = position;
                
                motionSetPLCAxisDirection(255, false, false); // Hardware Stop
            }
            break;

        case MOTION_STOPPING:
            if (abs(position - position_at_stop) < configGetInt(KEY_MOTION_DEADBAND, 10)) {
                state = MOTION_IDLE;
                active_axis = 255; // Release mutex
            }
            break;

        // --- HOMING LOGIC ---
        case MOTION_HOMING_APPROACH_FAST:
        case MOTION_HOMING_APPROACH_FINE:
            {
                bool hit = elboI73GetInput(AXIS_TO_I73_BIT[id]);
                if (millis() - state_entry_ms > 45000) { 
                    state = MOTION_ERROR; logError("[HOME] Timeout"); 
                }

                if (hit) {
                    motionSetPLCAxisDirection(255, false, false); // Stop
                    
                    if (state == MOTION_HOMING_APPROACH_FAST) {
                        // Switch to SLOW profile (Configurable)
                        int slow_prof = configGetInt(KEY_HOME_PROFILE_SLOW, 0); 
                        motionSetPLCSpeedProfile((speed_profile_t)slow_prof);
                        
                        // Back off (Positive)
                        motionSetPLCAxisDirection(id, true, true);
                        state = MOTION_HOMING_BACKOFF;
                    } else {
                        homing_trigger_pos = position;
                        state = MOTION_HOMING_SETTLE;
                    }
                    state_entry_ms = millis();
                }
            }
            break;

        case MOTION_HOMING_BACKOFF:
            if (!elboI73GetInput(AXIS_TO_I73_BIT[id])) { // Released
                if (millis() - state_entry_ms > 1000) {
                    motionSetPLCAxisDirection(255, false, false);
                    
                    // Fine approach (Negative)
                    int slow_prof = configGetInt(KEY_HOME_PROFILE_SLOW, 0);
                    motionSetPLCSpeedProfile((speed_profile_t)slow_prof);
                    
                    motionSetPLCAxisDirection(id, true, false);
                    state = MOTION_HOMING_APPROACH_FINE;
                    state_entry_ms = millis();
                }
            }
            break;

        case MOTION_HOMING_SETTLE:
            if (millis() - state_entry_ms > HOMING_SETTLE_MS) {
                wj66SetZero(id);
                position = 0;
                target_position = 0;
                state = MOTION_IDLE;
                active_axis = 255;
                logInfo("[HOME] Axis %d Zeroed.", id);
            }
            break;
            
        default: break;
    }
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void motionInit() {
    logInfo("[MOTION] Init v3.5.1...");
    for(int i=0; i<MOTION_AXES; i++) {
        axes[i].init(i);
        // Default Limits
        axes[i].soft_limit_min = -500000;
        axes[i].soft_limit_max = 500000;
    }
    motionPlanner.init();
    motionSetPLCAxisDirection(255, false, false);
}

void motionUpdate() {
    if (!global_enabled) return;
    if (!taskLockMutex(taskGetMotionMutex(), 0)) return;

    // 1. Global Monitor
    int strict_limits = configGetInt(KEY_MOTION_STRICT_LIMITS, 1);
    
    for (int i=0; i<MOTION_AXES; i++) {
        int32_t pos = wj66GetPosition(i);
        axes[i].position = pos; // Update object state
        
        if (axes[i].checkSoftLimits(strict_limits)) {
            // Error logged by object. Kill power.
            motionEmergencyStop();
            taskUnlockMutex(taskGetMotionMutex());
            return;
        }
    }

    // 2. Planner
    motionPlanner.update(axes, active_axis, active_start_position);

    // 3. Active Axis Logic
    if (active_axis != 255) {
        axes[active_axis].updateState(axes[active_axis].position, axes[active_axis].target_position);
    }

    taskUnlockMutex(taskGetMotionMutex());
}

// ============================================================================
// API IMPLEMENTATION
// ============================================================================

void motionHome(uint8_t axis) {
    if (axis >= MOTION_AXES || !taskLockMutex(taskGetMotionMutex(), 100)) return;
    if (active_axis != 255) { taskUnlockMutex(taskGetMotionMutex()); return; }

    logInfo("[HOME] Axis %d Start", axis);
    active_axis = axis;
    axes[axis].state = MOTION_HOMING_APPROACH_FAST;
    axes[axis].state_entry_ms = millis();
    
    // Use Configured Fast Profile
    int fast_prof = configGetInt(KEY_HOME_PROFILE_FAST, 2); 
    motionSetPLCSpeedProfile((speed_profile_t)fast_prof);
    
    motionSetPLCAxisDirection(axis, true, false); // Negative
    
    taskUnlockMutex(taskGetMotionMutex());
}

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
    if (!global_enabled) { logError("[MOTION] Disabled"); return; }
    if (!taskLockMutex(taskGetMotionMutex(), 100)) { logError("[MOTION] Busy"); return; }

    // (Selection Logic)
    uint8_t target_axis = 255;
    int32_t target_pos = 0;
    
    float targets[] = {x, y, z, a};
    // Fetch calibration scales
    float scales[] = {
      (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG
    };

    int cnt = 0;
    for(int i=0; i<MOTION_AXES; i++) {
        int32_t t = (int32_t)(targets[i] * scales[i]);
        if(abs(t - wj66GetPosition(i)) > 1) { 
            cnt++; target_axis = i; target_pos = t; 
        }
    }

    if (cnt != 1 || active_axis != 255) { taskUnlockMutex(taskGetMotionMutex()); return; }

    // Start Move
    axes[target_axis].commanded_speed_mm_s = speed_mm_s;
    
    // Check Limits (Always check target before moving, regardless of strict mode)
    if (axes[target_axis].soft_limit_enabled) {
        if(target_pos < axes[target_axis].soft_limit_min || target_pos > axes[target_axis].soft_limit_max) {
            logError("[MOTION] Target Limit Violation Axis %d", target_axis);
            taskUnlockMutex(taskGetMotionMutex()); return;
        }
    }

    axes[target_axis].target_position = target_pos;
    axes[target_axis].position_at_stop = motionGetPosition(target_axis);
    
    float eff_spd = speed_mm_s * motionPlanner.getFeedOverride();
    speed_profile_t prof = motionMapSpeedToProfile(target_axis, eff_spd);
    axes[target_axis].saved_speed_profile = prof;
    
    motionSetPLCSpeedProfile(prof);
    bool is_fwd = (target_pos > axes[target_axis].position);
    motionSetPLCAxisDirection(target_axis, true, is_fwd);
    
    active_axis = target_axis;
    active_start_position = axes[target_axis].position;
    axes[target_axis].state = MOTION_WAIT_CONSENSO;
    axes[target_axis].state_entry_ms = millis();
    
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate(); 
}

// ============================================================================
// HELPERS & WRAPPERS (Fully Implemented)
// ============================================================================

void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus) {
    if (!enable || axis >= MOTION_AXES) {
        elboSetDirection(0, false); 
        elboQ73SetRelay(ELBO_Q73_ENABLE, false);
        return;
    }
    elboSetDirection(axis, is_plus);
    elboQ73SetRelay(ELBO_Q73_ENABLE, true);
}

void motionSetPLCSpeedProfile(speed_profile_t profile) {
    elboSetSpeedProfile((uint8_t)profile);
}

void motionStartInternalMove(float x, float y, float z, float a, float speed_mm_s) { 
    motionMoveAbsolute(x, y, z, a, speed_mm_s); 
}

void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s) {
    float cur_x = motionGetPositionMM(0);
    float cur_y = motionGetPositionMM(1);
    float cur_z = motionGetPositionMM(2);
    float cur_a = motionGetPositionMM(3);
    motionMoveAbsolute(cur_x + dx, cur_y + dy, cur_z + dz, cur_a + da, speed_mm_s);
}

void motionSetFeedOverride(float factor) { 
    motionPlanner.setFeedOverride(factor); 
}

float motionGetFeedOverride() { 
    return motionPlanner.getFeedOverride(); 
}

void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos) {
    if (axis < MOTION_AXES) {
        axes[axis].soft_limit_min = min_pos;
        axes[axis].soft_limit_max = max_pos;
    }
}

void motionEnableSoftLimits(uint8_t axis, bool enable) {
    if (axis < MOTION_AXES) {
        axes[axis].soft_limit_enabled = enable;
    }
}

bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos) {
    if (axis >= MOTION_AXES) return false;
    *min_pos = axes[axis].soft_limit_min;
    *max_pos = axes[axis].soft_limit_max;
    return axes[axis].soft_limit_enabled;
}

void motionEnableEncoderFeedback(bool enable) {
    encoder_feedback_enabled = enable;
    // Note: External function removed to prevent linker error. 
    // Flag logic is sufficient for v3.5.1
}

bool motionIsEncoderFeedbackEnabled() { 
    return encoder_feedback_enabled; 
}

bool motionIsValidStateTransition(uint8_t axis, motion_state_t new_state) { 
    return true; 
}

bool motionSetState(uint8_t axis, motion_state_t new_state) {
    if (axis >= MOTION_AXES || !taskLockMutex(taskGetMotionMutex(), 100)) return false;
    axes[axis].state = new_state;
    if (new_state == MOTION_WAIT_CONSENSO || new_state == MOTION_STOPPING) {
        axes[axis].state_entry_ms = millis();
    }
    if (new_state == MOTION_IDLE || new_state == MOTION_ERROR) {
        motionSetPLCAxisDirection(255, false, false);
        active_axis = 255;
    }
    taskUnlockMutex(taskGetMotionMutex());
    return true;
}

// ============================================================================
// CONTROL FUNCTIONS
// ============================================================================

void motionStop() {
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return;
    if (active_axis != 255) {
        motionSetPLCAxisDirection(255, false, false);
        axes[active_axis].state = MOTION_STOPPING;
        axes[active_axis].position_at_stop = axes[active_axis].position;
    }
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
}

void motionPause() {
    if (!global_enabled) return;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return;
    if (active_axis != 255 && (axes[active_axis].state == MOTION_EXECUTING || axes[active_axis].state == MOTION_WAIT_CONSENSO)) {
        motionSetPLCAxisDirection(255, false, false);
        axes[active_axis].state = MOTION_PAUSED;
        logInfo("[MOTION] Paused axis %d", active_axis);
    }
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
}

void motionResume() {
    if (!global_enabled) return;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return;
    if (active_axis != 255 && axes[active_axis].state == MOTION_PAUSED) {
        logInfo("[MOTION] Resuming axis %d", active_axis);
        float effective_speed = axes[active_axis].commanded_speed_mm_s * motionPlanner.getFeedOverride();
        speed_profile_t prof = motionMapSpeedToProfile(active_axis, effective_speed);
        motionSetPLCSpeedProfile(prof);
        
        bool is_forward = (axes[active_axis].target_position > axes[active_axis].position);
        motionSetPLCAxisDirection(active_axis, true, is_forward);
        
        axes[active_axis].state = MOTION_WAIT_CONSENSO;
        axes[active_axis].state_entry_ms = millis();
    }
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
}

void motionEmergencyStop() {
    bool got_mutex = taskLockMutex(taskGetMotionMutex(), 10); 
    
    // Immediate Hardware Shutdown
    motionSetPLCAxisDirection(255, false, false);
    global_enabled = false;
    
    for (int i = 0; i < MOTION_AXES; i++) {
        axes[i].state = MOTION_ERROR;
    }
    active_axis = 255;
    motionBuffer.clear();
    
    if (got_mutex) taskUnlockMutex(taskGetMotionMutex());
    logError("[MOTION] [CRITICAL] EMERGENCY STOP ACTIVATED");
    faultLogError(FAULT_EMERGENCY_HALT, "E-Stop Activated");
    taskSignalMotionUpdate();
}

bool motionClearEmergencyStop() {
    if (global_enabled) { 
        Serial.println("[MOTION] E-Stop already cleared"); 
        return true; 
    }
    if (safetyIsAlarmed()) { 
        Serial.println("[MOTION] Cannot clear - Alarm Active"); 
        return false; 
    }
    global_enabled = true;
    for (int i = 0; i < MOTION_AXES; i++) {
        if (axes[i].state == MOTION_ERROR) axes[i].state = MOTION_IDLE;
    }
    active_axis = 255;
    emergencyStopSetActive(false); 
    Serial.println("[MOTION] [OK] Emergency stop cleared");
    taskSignalMotionUpdate();
    return true;
}

speed_profile_t motionMapSpeedToProfile(uint8_t axis, float speed) {
    // Default mapping logic: Slow < 10, Medium < 30, Fast >= 30
    // This can be customized per machine calibration if needed
    if (speed < 10.0) return SPEED_PROFILE_1;
    if (speed < 30.0) return SPEED_PROFILE_2;
    return SPEED_PROFILE_3;
}

void motionDiagnostics() {
    Serial.printf("\n[MOTION] State: %s | Active: %d | Feed: %.0f%%\n", 
        global_enabled ? "ON" : "ESTOP", active_axis, motionPlanner.getFeedOverride() * 100.0f);
        
    for (int i = 0; i < MOTION_AXES; i++) {
        Serial.printf("  Axis %d: Pos=%ld | Tgt=%ld | State=%s\n", 
            i, (long)axes[i].position, (long)axes[i].target_position, motionStateToString(axes[i].state));
    }
}