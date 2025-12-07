/**
 * @file motion_control.cpp
 * @brief Real-Time Hardware Execution Layer (Gemini v3.5.12)
 * @details Fixed Thread Safety: Added timeout and starvation logging to motion loop.
 * @author Sergio Faustino
 */

#include "motion.h"
#include "motion_planner.h"
#include "motion_state.h" // Implements these interfaces
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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// STATE OWNERSHIP
// ============================================================================

Axis axes[MOTION_AXES]; 

// Protected Global State
static struct {
    uint8_t active_axis;
    int32_t active_start_position;
    bool global_enabled;
    bool encoder_feedback_enabled;
} m_state = { 255, 0, true, false };

static portMUX_TYPE motionSpinlock = portMUX_INITIALIZER_UNLOCKED;

const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, ELBO_I73_AXIS_A}; 
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_I73_CONSENSO_X, ELBO_I73_CONSENSO_Y, ELBO_I73_CONSENSO_Z, ELBO_I73_CONSENSO_A};

// Forward Declarations
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus);
void motionSetPLCSpeedProfile(speed_profile_t profile);

// ============================================================================
// AXIS CLASS IMPLEMENTATION
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
    if (state >= MOTION_HOMING_APPROACH_FAST) return false;

    if (position < soft_limit_min || position > soft_limit_max) {
        if (strict_mode) {
            if (!_error_logged) {
                faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, id, position, "Strict Limit Hit");
                logError("[AXIS %d] Strict Limit Violation: %ld", id, (long)position);
                _error_logged = true;
            }
            return true; 
        }
    } else {
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
            if ((m_state.active_start_position < target_position && position >= target_position) ||
                (m_state.active_start_position > target_position && position <= target_position)) {
                
                state = MOTION_STOPPING;
                state_entry_ms = millis();
                position_at_stop = position; 
                motionSetPLCAxisDirection(255, false, false); 
            }
            break;

        case MOTION_STOPPING:
            if (abs(position - target_position) < configGetInt(KEY_MOTION_DEADBAND, 10)) {
                state = MOTION_IDLE;
                m_state.active_axis = 255; 
            }
            else if (millis() - state_entry_ms > 5000) {
                logWarning("[AXIS %d] Stop Settlement Timeout", id);
                state = MOTION_IDLE;
                m_state.active_axis = 255;
            }
            break;

        case MOTION_HOMING_APPROACH_FAST:
        case MOTION_HOMING_APPROACH_FINE:
            {
                bool hit = elboI73GetInput(AXIS_TO_I73_BIT[id]);
                if (millis() - state_entry_ms > 45000) { 
                    state = MOTION_ERROR; logError("[HOME] Timeout"); 
                }

                if (hit) {
                    motionSetPLCAxisDirection(255, false, false); 
                    if (state == MOTION_HOMING_APPROACH_FAST) {
                        int slow_prof = configGetInt(KEY_HOME_PROFILE_SLOW, 0); 
                        motionSetPLCSpeedProfile((speed_profile_t)slow_prof);
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
            if (!elboI73GetInput(AXIS_TO_I73_BIT[id])) { 
                if (millis() - state_entry_ms > 1000) {
                    motionSetPLCAxisDirection(255, false, false);
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
                m_state.active_axis = 255;
                logInfo("[HOME] Axis %d Zeroed.", id);
            }
            break;
            
        default: break;
    }
}

// ============================================================================
// MAIN CONTROL LOOP
// ============================================================================

void motionInit() {
    logInfo("[MOTION] Init v3.5.12...");
    for(int i=0; i<MOTION_AXES; i++) {
        axes[i].init(i);
        axes[i].soft_limit_min = -500000;
        axes[i].soft_limit_max = 500000;
    }
    motionPlanner.init();
    motionSetPLCAxisDirection(255, false, false);
}

void motionUpdate() {
    if (!m_state.global_enabled) return;
    
    // FIX: Starvation Watchdog
    static uint32_t consecutive_skips = 0;

    // Try to acquire lock with 5ms timeout (Half a cycle)
    if (!taskLockMutex(taskGetMotionMutex(), 5)) {
        consecutive_skips++;
        if (consecutive_skips >= 5) { // 50ms blockage
            logWarning("[MOTION] Starvation: Loop skipped %lu times (Mutex busy)", (unsigned long)consecutive_skips);
        }
        return; // Skip this cycle, but track it
    }
    
    // Reset counter on success
    consecutive_skips = 0;

    int strict_limits = configGetInt(KEY_MOTION_STRICT_LIMITS, 1);
    
    for (int i=0; i<MOTION_AXES; i++) {
        int32_t pos = wj66GetPosition(i);
        axes[i].position = pos;
        
        if (axes[i].checkSoftLimits(strict_limits)) {
            motionEmergencyStop();
            taskUnlockMutex(taskGetMotionMutex());
            return;
        }
    }

    motionPlanner.update(axes, m_state.active_axis, m_state.active_start_position);

    if (m_state.active_axis != 255) {
        axes[m_state.active_axis].updateState(axes[m_state.active_axis].position, axes[m_state.active_axis].target_position);
    }

    taskUnlockMutex(taskGetMotionMutex());
}

// ============================================================================
// PUBLIC ACCESSORS
// ============================================================================

int32_t motionGetPosition(uint8_t axis) {
    return (axis < MOTION_AXES) ? axes[axis].position : 0;
}

int32_t motionGetTarget(uint8_t axis) {
    return (axis < MOTION_AXES) ? axes[axis].target_position : 0;
}

motion_state_t motionGetState(uint8_t axis) {
    return (axis < MOTION_AXES) ? axes[axis].state : MOTION_ERROR;
}

float motionGetPositionMM(uint8_t axis) {
    if (axis >= MOTION_AXES) return 0.0f;
    int32_t counts = axes[axis].position;
    float scale = 1.0f;
    
    if (axis == 0) scale = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 1) scale = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 2) scale = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 3) scale = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG;
    
    return (float)counts / scale;
}

bool motionIsMoving() {
    uint8_t ax = m_state.active_axis;
    if (ax >= MOTION_AXES) return false;
    motion_state_t s = axes[ax].state;
    return (s == MOTION_EXECUTING || s == MOTION_WAIT_CONSENSO || 
            s == MOTION_HOMING_APPROACH_FAST || s == MOTION_HOMING_BACKOFF || s == MOTION_HOMING_APPROACH_FINE);
}

bool motionIsStalled(uint8_t axis) { return false; } 

bool motionIsEmergencyStopped() { return !m_state.global_enabled; }

uint8_t motionGetActiveAxis() { return m_state.active_axis; }

const char* motionStateToString(motion_state_t state) {
  switch (state) {
    case MOTION_IDLE: return "IDLE";
    case MOTION_WAIT_CONSENSO: return "WAIT";
    case MOTION_EXECUTING: return "RUN";
    case MOTION_STOPPING: return "STOP";
    case MOTION_PAUSED: return "PAUSE";
    case MOTION_ERROR: return "ERR";
    case MOTION_HOMING_APPROACH_FAST: return "H:FAST";
    case MOTION_HOMING_BACKOFF: return "H:BACK";
    case MOTION_HOMING_APPROACH_FINE: return "H:FINE";
    case MOTION_HOMING_SETTLE: return "H:ZERO";
    default: return "UNK";
  }
}

// ============================================================================
// CONTROL API
// ============================================================================

void motionHome(uint8_t axis) {
    if (axis >= MOTION_AXES || !taskLockMutex(taskGetMotionMutex(), 100)) return;
    if (m_state.active_axis != 255) { taskUnlockMutex(taskGetMotionMutex()); return; }

    logInfo("[HOME] Axis %d Start", axis);
    m_state.active_axis = axis;
    axes[axis].state = MOTION_HOMING_APPROACH_FAST;
    axes[axis].state_entry_ms = millis();
    
    int fast_prof = configGetInt(KEY_HOME_PROFILE_FAST, 2); 
    motionSetPLCSpeedProfile((speed_profile_t)fast_prof);
    motionSetPLCAxisDirection(axis, true, false); 
    
    taskUnlockMutex(taskGetMotionMutex());
}

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
    if (!m_state.global_enabled) { logError("[MOTION] Disabled"); return; }
    if (!taskLockMutex(taskGetMotionMutex(), 100)) { logError("[MOTION] Busy"); return; }

    float targets[] = {x, y, z, a};
    float scales[] = {
      (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG
    };

    uint8_t target_axis = 255;
    int32_t target_pos = 0;
    int cnt = 0;

    for(int i=0; i<MOTION_AXES; i++) {
        int32_t t = (int32_t)(targets[i] * scales[i]);
        if(abs(t - wj66GetPosition(i)) > 1) { 
            cnt++; target_axis=i; target_pos=t; 
        }
    }

    if (cnt > 1 || cnt == 0 || m_state.active_axis != 255) { 
        taskUnlockMutex(taskGetMotionMutex()); return; 
    }

    axes[target_axis].commanded_speed_mm_s = speed_mm_s;
    
    if (axes[target_axis].soft_limit_enabled) {
        if(target_pos < axes[target_axis].soft_limit_min || target_pos > axes[target_axis].soft_limit_max) {
            logError("[MOTION] Target Limit Violation");
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
    
    m_state.active_axis = target_axis;
    m_state.active_start_position = axes[target_axis].position;
    axes[target_axis].state = MOTION_WAIT_CONSENSO;
    axes[target_axis].state_entry_ms = millis();
    
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate(); 
}

// ============================================================================
// HELPERS & WRAPPERS
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

speed_profile_t motionMapSpeedToProfile(uint8_t axis, float speed) {
    if (speed < 10.0f) return SPEED_PROFILE_1;
    if (speed < 30.0f) return SPEED_PROFILE_2;
    return SPEED_PROFILE_3;
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

void motionSetFeedOverride(float factor) { motionPlanner.setFeedOverride(factor); }
float motionGetFeedOverride() { return motionPlanner.getFeedOverride(); }

void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos) {
    if (axis < MOTION_AXES) {
        axes[axis].soft_limit_min = min_pos;
        axes[axis].soft_limit_max = max_pos;
    }
}

void motionEnableSoftLimits(uint8_t axis, bool enable) {
    if (axis < MOTION_AXES) {
        if (axes[axis].state != MOTION_IDLE) {
            logError("[MOTION] Reject Limit Config: Axis %d Busy", axis);
            return;
        }
        if (m_state.global_enabled) {
            logError("[MOTION] Reject Limit Config: System must be Disabled (E-Stop)");
            return;
        }
        axes[axis].soft_limit_enabled = enable;
        logInfo("[MOTION] Soft Limits Axis %d: %s", axis, enable ? "ON" : "OFF");
    }
}

bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos) {
    if (axis >= MOTION_AXES) return false;
    *min_pos = axes[axis].soft_limit_min;
    *max_pos = axes[axis].soft_limit_max;
    return axes[axis].soft_limit_enabled;
}

void motionEnableEncoderFeedback(bool enable) { m_state.encoder_feedback_enabled=enable; }
bool motionIsEncoderFeedbackEnabled() { return m_state.encoder_feedback_enabled; }
bool motionIsValidStateTransition(uint8_t axis, motion_state_t new_state) { return true; }
bool motionSetState(uint8_t axis, motion_state_t new_state) { if(axis>=4) return false; axes[axis].state=new_state; return true; }

void motionStop() {
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return;
    if (m_state.active_axis != 255) {
        motionSetPLCAxisDirection(255, false, false);
        axes[m_state.active_axis].state = MOTION_STOPPING;
        axes[m_state.active_axis].target_position = axes[m_state.active_axis].position;
        axes[m_state.active_axis].position_at_stop = axes[m_state.active_axis].position;
    }
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
}

void motionPause() {
    if (!m_state.global_enabled) return;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return;
    if (m_state.active_axis != 255 && (axes[m_state.active_axis].state == MOTION_EXECUTING || axes[m_state.active_axis].state == MOTION_WAIT_CONSENSO)) {
        motionSetPLCAxisDirection(255, false, false);
        axes[m_state.active_axis].state = MOTION_PAUSED;
        logInfo("[MOTION] Paused axis %d", m_state.active_axis);
    }
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
}

void motionResume() {
    if (!m_state.global_enabled) return;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return;
    if (m_state.active_axis != 255 && axes[m_state.active_axis].state == MOTION_PAUSED) {
        float effective_speed = axes[m_state.active_axis].commanded_speed_mm_s * motionPlanner.getFeedOverride();
        speed_profile_t prof = motionMapSpeedToProfile(m_state.active_axis, effective_speed);
        motionSetPLCSpeedProfile(prof);
        
        bool is_fwd = (axes[m_state.active_axis].target_position > axes[m_state.active_axis].position);
        motionSetPLCAxisDirection(m_state.active_axis, true, is_fwd);
        
        axes[m_state.active_axis].state = MOTION_WAIT_CONSENSO;
        axes[m_state.active_axis].state_entry_ms = millis();
    }
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
}

void motionEmergencyStop() {
    bool got_mutex = taskLockMutex(taskGetMotionMutex(), 10); 
    motionSetPLCAxisDirection(255, false, false);
    
    portENTER_CRITICAL(&motionSpinlock);
    m_state.global_enabled = false;
    for (int i = 0; i < MOTION_AXES; i++) axes[i].state = MOTION_ERROR;
    m_state.active_axis = 255;
    portEXIT_CRITICAL(&motionSpinlock);
    
    motionBuffer.clear();
    if (got_mutex) taskUnlockMutex(taskGetMotionMutex());
    
    logError("[MOTION] [CRITICAL] EMERGENCY STOP ACTIVATED");
    faultLogError(FAULT_EMERGENCY_HALT, "E-Stop Activated");
    taskSignalMotionUpdate();
}

bool motionClearEmergencyStop() {
    if (m_state.global_enabled) { 
        Serial.println("[MOTION] E-Stop already cleared"); 
        return true; 
    }
    if (safetyIsAlarmed()) { 
        Serial.println("[MOTION] Cannot clear - Alarm Active"); 
        return false; 
    }
    
    portENTER_CRITICAL(&motionSpinlock);
    m_state.global_enabled = true;
    for (int i = 0; i < MOTION_AXES; i++) {
        if (axes[i].state == MOTION_ERROR) axes[i].state = MOTION_IDLE;
    }
    m_state.active_axis = 255;
    portEXIT_CRITICAL(&motionSpinlock);
    
    emergencyStopSetActive(false); 
    Serial.println("[MOTION] [OK] Emergency stop cleared");
    taskSignalMotionUpdate();
    return true;
}

void motionDiagnostics() {
    Serial.printf("\n[MOTION] State: %s | Active: %d\n", m_state.global_enabled ? "ON" : "ESTOP", m_state.active_axis);
    for (int i = 0; i < MOTION_AXES; i++) {
        Serial.printf("  Axis %d: Pos=%ld | Tgt=%ld | State=%s\n", 
            i, (long)axes[i].position, (long)axes[i].target_position, motionStateToString(axes[i].state));
    }
}