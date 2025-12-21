/**
 * @file motion_control.cpp
 * @brief Real-Time Hardware Execution Layer (Gemini v3.5.19)
 * @details Final Polish: Encapsulation, Configurable Timeouts, Stall Logic.
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
#include "encoder_motion_integration.h"
#include "auto_report.h"  // PHASE 4.0: M154 auto-report support
#include "lcd_sleep.h"    // PHASE 4.0: M255 LCD sleep support
#include "board_inputs.h"  // PHASE 4.0: M226 board input reading
#include "spindle_current_monitor.h"  // PHASE 5.0: Spindle current monitoring
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// STATE OWNERSHIP
// ============================================================================

// FIX: Made static to enforce encapsulation
static Axis axes[MOTION_AXES]; 

static struct {
    uint8_t active_axis;
    int32_t active_start_position;
    bool global_enabled;
    int strict_limits;
} m_state = { 255, 0, true, 1 };

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
    dwell_end_ms = 0;
    wait_pin_id = 0;
    wait_pin_type = 0;
    wait_pin_state = 0;
    wait_pin_timeout_ms = 0;
    current_velocity_mm_s = 0.0f;
    prev_position = 0;
    prev_update_ms = 0;
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
    // Calculate velocity (differentiate position over time)
    uint32_t current_time_ms = millis();
    if (prev_update_ms > 0) {
        uint32_t dt_ms = current_time_ms - prev_update_ms;
        if (dt_ms > 0) {
            int32_t delta_pos = current_pos - prev_position;
            // Convert counts/ms to mm/s
            // velocity = (delta_counts / dt_ms) * (1000 ms/s) * (1 mm / ppm counts)
            float ppm = encoderCalibrationGetPPM(id);  // pulses per mm
            if (ppm > 0.0f) {
                current_velocity_mm_s = ((float)delta_pos / (float)dt_ms) * 1000.0f / ppm;
            } else {
                current_velocity_mm_s = 0.0f;
            }
        }
    }

    // Update tracking variables
    prev_position = current_pos;
    prev_update_ms = current_time_ms;
    position = current_pos;

    // CRITICAL FIX: Use spinlock to protect state reads/writes
    // This ensures atomic state transitions and prevents race conditions
    // when multiple tasks access state (motion task vs CLI/status reporting)
    portENTER_CRITICAL(&motionSpinlock);

    motion_state_t current_state = state;

    portEXIT_CRITICAL(&motionSpinlock);

    if (current_state == MOTION_ERROR || !enabled) return;

    switch (current_state) {
        case MOTION_WAIT_CONSENSO:
            // PHASE 5.1: Wraparound-safe timeout comparison
            if ((uint32_t)(millis() - state_entry_ms) > MOTION_CONSENSO_TIMEOUT_MS) {
                faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, id, 0, "Consensus Timeout");
                portENTER_CRITICAL(&motionSpinlock);
                state = MOTION_ERROR;
                portEXIT_CRITICAL(&motionSpinlock);
            } else {
                if (elboI73GetInput(AXIS_TO_CONSENSO_BIT[id])) {
                    portENTER_CRITICAL(&motionSpinlock);
                    state = MOTION_EXECUTING;
                    state_entry_ms = millis();
                    portEXIT_CRITICAL(&motionSpinlock);
                }
            }
            break;

        case MOTION_EXECUTING:
            if ((m_state.active_start_position < target_position && position >= target_position) ||
                (m_state.active_start_position > target_position && position <= target_position)) {

                portENTER_CRITICAL(&motionSpinlock);
                state = MOTION_STOPPING;
                state_entry_ms = millis();
                portEXIT_CRITICAL(&motionSpinlock);

                position_at_stop = position;
                motionSetPLCAxisDirection(255, false, false);
            }
            break;

        case MOTION_STOPPING:
            if (abs(position - target_position) < configGetInt(KEY_MOTION_DEADBAND, 10)) {
                portENTER_CRITICAL(&motionSpinlock);
                state = MOTION_IDLE;
                m_state.active_axis = 255;
                portEXIT_CRITICAL(&motionSpinlock);
            }
            else {
                // FIX: Configurable Timeout (PHASE 5.1: Wraparound-safe)
                uint32_t timeout = configGetInt(KEY_STOP_TIMEOUT, 5000);
                if ((uint32_t)(millis() - state_entry_ms) > timeout) {
                    logWarning("[AXIS %d] Stop Settlement Timeout", id);
                    portENTER_CRITICAL(&motionSpinlock);
                    state = MOTION_IDLE;
                    m_state.active_axis = 255;
                    portEXIT_CRITICAL(&motionSpinlock);
                }
            }
            break;

        case MOTION_HOMING_APPROACH_FAST:
        case MOTION_HOMING_APPROACH_FINE:
            {
                bool hit = elboI73GetInput(AXIS_TO_I73_BIT[id]);
                // PHASE 5.1: Wraparound-safe timeout comparison
                if ((uint32_t)(millis() - state_entry_ms) > 45000) {
                    portENTER_CRITICAL(&motionSpinlock);
                    state = MOTION_ERROR;
                    portEXIT_CRITICAL(&motionSpinlock);
                    logError("[HOME] Timeout");
                }

                if (hit) {
                    motionSetPLCAxisDirection(255, false, false);
                    if (current_state == MOTION_HOMING_APPROACH_FAST) {
                        int slow_prof = configGetInt(KEY_HOME_PROFILE_SLOW, 0);
                        motionSetPLCSpeedProfile((speed_profile_t)slow_prof);
                        motionSetPLCAxisDirection(id, true, true);
                        portENTER_CRITICAL(&motionSpinlock);
                        state = MOTION_HOMING_BACKOFF;
                        state_entry_ms = millis();
                        portEXIT_CRITICAL(&motionSpinlock);
                    } else {
                        homing_trigger_pos = position;
                        portENTER_CRITICAL(&motionSpinlock);
                        state = MOTION_HOMING_SETTLE;
                        state_entry_ms = millis();
                        portEXIT_CRITICAL(&motionSpinlock);
                    }
                }
            }
            break;

        case MOTION_HOMING_BACKOFF:
            if (!elboI73GetInput(AXIS_TO_I73_BIT[id])) {
                // PHASE 5.1: Wraparound-safe timeout comparison
                if ((uint32_t)(millis() - state_entry_ms) > 1000) {
                    motionSetPLCAxisDirection(255, false, false);
                    int slow_prof = configGetInt(KEY_HOME_PROFILE_SLOW, 0);
                    motionSetPLCSpeedProfile((speed_profile_t)slow_prof);
                    motionSetPLCAxisDirection(id, true, false);
                    portENTER_CRITICAL(&motionSpinlock);
                    state = MOTION_HOMING_APPROACH_FINE;
                    state_entry_ms = millis();
                    portEXIT_CRITICAL(&motionSpinlock);
                }
            }
            break;

        case MOTION_HOMING_SETTLE:
            // PHASE 5.1: Wraparound-safe timeout comparison
            if ((uint32_t)(millis() - state_entry_ms) > HOMING_SETTLE_MS) {
                wj66SetZero(id);
                position = 0;
                target_position = 0;
                portENTER_CRITICAL(&motionSpinlock);
                state = MOTION_IDLE;
                m_state.active_axis = 255;
                portEXIT_CRITICAL(&motionSpinlock);
                logInfo("[HOME] Axis %d Zeroed.", id);
            }
            break;

        case MOTION_DWELL:
            // Non-blocking dwell - just wait for timer to expire
            // PHASE 5.1: Wraparound-safe timeout comparison
            if ((int32_t)(millis() - dwell_end_ms) >= 0) {
                portENTER_CRITICAL(&motionSpinlock);
                state = MOTION_IDLE;
                m_state.active_axis = 255;
                portEXIT_CRITICAL(&motionSpinlock);
                logInfo("[MOTION] Dwell complete");
            }
            break;

        case MOTION_WAIT_PIN: {
            // Non-blocking pin state wait with optional timeout
            bool pin_state = false;
            bool pin_ready = false;

            // Read pin state based on type
            if (wait_pin_type == 0) {
                // I73 input (ELBO PLC)
                pin_state = elboI73GetInput(wait_pin_id);
                pin_ready = true;
            } else if (wait_pin_type == 1) {
                // Board input (KC868-A16)
                button_state_t buttons = boardInputsUpdate();
                // Map button state based on pin ID
                if (wait_pin_id == 0) pin_state = buttons.estop_active;
                else if (wait_pin_id == 1) pin_state = buttons.pause_pressed;
                else if (wait_pin_id == 2) pin_state = buttons.resume_pressed;
                else pin_state = false;
                pin_ready = buttons.connection_ok;
            } else if (wait_pin_type == 2) {
                // Direct ESP32 GPIO
                pinMode(wait_pin_id, INPUT);
                pin_state = (digitalRead(wait_pin_id) == HIGH);
                pin_ready = true;
            }

            // Check if pin state matches what we're waiting for
            if (pin_ready && pin_state == wait_pin_state) {
                portENTER_CRITICAL(&motionSpinlock);
                state = MOTION_IDLE;
                m_state.active_axis = 255;
                portEXIT_CRITICAL(&motionSpinlock);
                logInfo("[MOTION] Pin %d state %d detected", wait_pin_id, wait_pin_state);
                break;
            }

            // Check for timeout (PHASE 5.1: Wraparound-safe)
            if (wait_pin_timeout_ms > 0 && (uint32_t)(millis() - state_entry_ms) >= wait_pin_timeout_ms) {
                portENTER_CRITICAL(&motionSpinlock);
                state = MOTION_ERROR;
                portEXIT_CRITICAL(&motionSpinlock);
                faultLogEntry(FAULT_WARNING, FAULT_MOTION_TIMEOUT, id, 0, "Pin wait timeout");
                logWarning("[MOTION] Pin %d wait timeout", wait_pin_id);
            }
            break;
        }

        default: break;
    }
}

// ============================================================================
// MAIN CONTROL LOOP
// ============================================================================

void motionInit() {
    logInfo("[MOTION] Init v3.5.19...");
    m_state.strict_limits = configGetInt(KEY_MOTION_STRICT_LIMITS, 1);

    for(int i=0; i<MOTION_AXES; i++) {
        axes[i].init(i);
        axes[i].soft_limit_min = -500000;
        axes[i].soft_limit_max = 500000;
    }
    motionPlanner.init();
    autoReportInit();  // PHASE 4.0: Initialize auto-report system
    lcdSleepInit();    // PHASE 4.0: Initialize LCD sleep system

    // PHASE 5.0: Initialize spindle current monitoring
    uint8_t spindle_addr = configGetInt(KEY_SPINDLE_ADDRESS, 1);
    float spindle_threshold = (float)configGetInt(KEY_SPINDLE_THRESHOLD, 30);
    if (!spindleMonitorInit(spindle_addr, spindle_threshold)) {
        logWarning("[MOTION] Failed to initialize spindle current monitor");
    }

    motionSetPLCAxisDirection(255, false, false);
}

void motionUpdate() {
    if (!m_state.global_enabled) return;

    // PHASE 5.1: Exponential backoff with safety escalation
    static uint32_t consecutive_skips = 0;
    static uint32_t last_timeout_warning_ms = 0;
    static uint8_t backoff_level = 0;  // 0=100ms, 1=200ms, 2=400ms, 3=escalate

    // Calculate timeout with exponential backoff
    uint32_t timeout_ms = 100;  // Base: 100ms (was 5ms - too aggressive)
    if (backoff_level > 0) {
        timeout_ms = 100 << backoff_level;  // 100, 200, 400ms
        if (timeout_ms > 400) timeout_ms = 400;  // Cap at 400ms
    }

    if (!taskLockMutex(taskGetMotionMutex(), timeout_ms)) {
        consecutive_skips++;

        // Increase backoff level after repeated failures
        if (consecutive_skips >= 3) {
            backoff_level = 1;
        }
        if (consecutive_skips >= 10) {
            backoff_level = 2;
        }

        // Log warning after threshold
        uint32_t now = millis();
        if ((uint32_t)(now - last_timeout_warning_ms) >= 5000) {  // Log once per 5 seconds
            logWarning("[MOTION] Mutex timeout (%lums): Skipped %lu times, backoff level %u",
                      (unsigned long)timeout_ms, (unsigned long)consecutive_skips, backoff_level);

            // Format fault message with actual values (faultLogWarning doesn't support printf-style formatting)
            char fault_msg[128];
            snprintf(fault_msg, sizeof(fault_msg),
                    "Motion mutex timeout: %lu consecutive failures, backoff level %u",
                    (unsigned long)consecutive_skips, backoff_level);
            faultLogWarning(FAULT_MOTION_TIMEOUT, fault_msg);
            last_timeout_warning_ms = now;
        }

        // Escalate to emergency stop if critical timeout threshold reached
        if (consecutive_skips >= 20) {
            logError("[MOTION] CRITICAL: Motion mutex timeout escalation!");
            faultLogCritical(FAULT_MOTION_TIMEOUT, "Motion mutex critical failure - escalating to emergency stop");
            motionEmergencyStop();
            consecutive_skips = 0;
            backoff_level = 0;
        }

        return;
    }

    // Successfully acquired mutex - reset backoff
    if (consecutive_skips > 0) {
        consecutive_skips = 0;
        backoff_level = 0;
    }

    int strict_mode = m_state.strict_limits;

    for (int i=0; i<MOTION_AXES; i++) {
        int32_t pos = wj66GetPosition(i);
        axes[i].position = pos;

        if (axes[i].checkSoftLimits(strict_mode)) {
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

    // PHASE 4.0: Check if auto-report interval elapsed (non-blocking)
    autoReportUpdate();

    // PHASE 4.0: Check if LCD sleep timeout elapsed (non-blocking)
    lcdSleepUpdate();

    // PHASE 5.0: Update spindle current monitoring (non-blocking, includes safety shutdown)
    spindleMonitorUpdate();
}

// ============================================================================
// PUBLIC ACCESSORS
// ============================================================================

// FIX: Read-only Accessor Implementation
const Axis* motionGetAxis(uint8_t axis) {
    return (axis < MOTION_AXES) ? &axes[axis] : nullptr;
}

int32_t motionGetPosition(uint8_t axis) {
    return (axis < MOTION_AXES) ? axes[axis].position : 0;
}

int32_t motionGetTarget(uint8_t axis) {
    return (axis < MOTION_AXES) ? axes[axis].target_position : 0;
}

motion_state_t motionGetState(uint8_t axis) {
    if (axis >= MOTION_AXES) return MOTION_ERROR;

    // CRITICAL FIX: Use spinlock to protect state read
    portENTER_CRITICAL(&motionSpinlock);
    motion_state_t s = axes[axis].state;
    portEXIT_CRITICAL(&motionSpinlock);

    return s;
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

float motionGetVelocity(uint8_t axis) {
    if (axis >= MOTION_AXES) return 0.0f;

    // Return current velocity in mm/s (calculated in updateState)
    return axes[axis].current_velocity_mm_s;
}

bool motionIsMoving() {
    // CRITICAL FIX: Use spinlock to protect state read
    portENTER_CRITICAL(&motionSpinlock);
    uint8_t ax = m_state.active_axis;
    motion_state_t s = (ax < MOTION_AXES) ? axes[ax].state : MOTION_ERROR;
    portEXIT_CRITICAL(&motionSpinlock);

    if (ax >= MOTION_AXES) return false;
    return (s == MOTION_EXECUTING || s == MOTION_WAIT_CONSENSO ||
            s == MOTION_HOMING_APPROACH_FAST || s == MOTION_HOMING_BACKOFF || s == MOTION_HOMING_APPROACH_FINE);
}

// FIX: Delegating to encoder integration instead of hardcoded false
bool motionIsStalled(uint8_t axis) { 
    return encoderMotionHasError(axis); 
}

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

bool motionHome(uint8_t axis) {
    if (axis >= MOTION_AXES) return false;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return false;
    if (m_state.active_axis != 255) { 
        taskUnlockMutex(taskGetMotionMutex()); 
        return false; 
    }

    logInfo("[HOME] Axis %d Start", axis);
    m_state.active_axis = axis;
    axes[axis].state = MOTION_HOMING_APPROACH_FAST;
    axes[axis].state_entry_ms = millis();
    
    int fast_prof = configGetInt(KEY_HOME_PROFILE_FAST, 2); 
    motionSetPLCSpeedProfile((speed_profile_t)fast_prof);
    motionSetPLCAxisDirection(axis, true, false); 
    
    taskUnlockMutex(taskGetMotionMutex());
    return true;
}

bool motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
    if (!m_state.global_enabled) { 
        logError("[MOTION] Disabled"); 
        return false; 
    }
    if (!taskLockMutex(taskGetMotionMutex(), 100)) { 
        logError("[MOTION] Busy (Mutex)"); 
        return false; 
    }

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
        taskUnlockMutex(taskGetMotionMutex()); 
        return false; 
    }

    axes[target_axis].commanded_speed_mm_s = speed_mm_s;
    
    if (axes[target_axis].soft_limit_enabled) {
        if(target_pos < axes[target_axis].soft_limit_min || target_pos > axes[target_axis].soft_limit_max) {
            logError("[MOTION] Target Limit Violation");
            taskUnlockMutex(taskGetMotionMutex()); 
            return false;
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
    return true;
}

// ============================================================================
// WRAPPERS AND HELPERS
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

bool motionStartInternalMove(float x, float y, float z, float a, float speed_mm_s) { 
    return motionMoveAbsolute(x, y, z, a, speed_mm_s); 
}

bool motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s) {
    float cur_x = motionGetPositionMM(0);
    float cur_y = motionGetPositionMM(1);
    float cur_z = motionGetPositionMM(2);
    float cur_a = motionGetPositionMM(3);
    return motionMoveAbsolute(cur_x + dx, cur_y + dy, cur_z + dz, cur_a + da, speed_mm_s);
}

bool motionSetPosition(float x, float y, float z, float a) {
    // G92 - Set current position without moving
    // This is used for calibration and work offset setting
    // IMPORTANT: Only call when axes are idle

    if (!taskLockMutex(taskGetMotionMutex(), 100)) {
        logError("[MOTION] Cannot set position - mutex locked");
        return false;
    }

    // Check if any axis is currently moving
    if (m_state.active_axis != 255) {
        taskUnlockMutex(taskGetMotionMutex());
        logError("[MOTION] Cannot set position - axis %d is active", m_state.active_axis);
        return false;
    }

    // Convert mm to counts for each axis
    float positions[] = {x, y, z, a};
    float scales[] = {
        (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
        (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
        (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
        (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG
    };

    // Set positions for all axes
    for (uint8_t i = 0; i < MOTION_AXES; i++) {
        int32_t new_pos = (int32_t)(positions[i] * scales[i]);
        axes[i].position = new_pos;
        axes[i].target_position = new_pos;
        logInfo("[MOTION] Axis %d position set to %.3f mm (%ld counts)",
                i, positions[i], (long)new_pos);
    }

    taskUnlockMutex(taskGetMotionMutex());
    return true;
}

void motionSetFeedOverride(float factor) { motionPlanner.setFeedOverride(factor); }
float motionGetFeedOverride() { return motionPlanner.getFeedOverride(); }

void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos) {
    if (axis < MOTION_AXES) {
        axes[axis].soft_limit_min = min_pos;
        axes[axis].soft_limit_max = max_pos;
    }
}

void motionSetStrictLimits(bool enable) {
    m_state.strict_limits = enable ? 1 : 0;
    configSetInt(KEY_MOTION_STRICT_LIMITS, m_state.strict_limits);
    logInfo("[MOTION] Strict Limits: %s", enable ? "ON" : "OFF");
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

void motionEnableEncoderFeedback(bool enable) { 
    encoderMotionEnableFeedback(enable); 
}

bool motionIsEncoderFeedbackEnabled() { 
    return encoderMotionIsFeedbackActive(); 
}

bool motionStop() {
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return false;
    if (m_state.active_axis != 255) {
        motionSetPLCAxisDirection(255, false, false);
        axes[m_state.active_axis].state = MOTION_STOPPING;
        axes[m_state.active_axis].target_position = axes[m_state.active_axis].position;
        axes[m_state.active_axis].position_at_stop = axes[m_state.active_axis].position;
    }
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
    return true;
}

bool motionPause() {
    if (!m_state.global_enabled) return false;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return false;
    if (m_state.active_axis != 255 && (axes[m_state.active_axis].state == MOTION_EXECUTING || axes[m_state.active_axis].state == MOTION_WAIT_CONSENSO)) {
        motionSetPLCAxisDirection(255, false, false);
        axes[m_state.active_axis].state = MOTION_PAUSED;
        logInfo("[MOTION] Paused axis %d", m_state.active_axis);
    }
    taskUnlockMutex(taskGetMotionMutex());
    taskSignalMotionUpdate();
    return true;
}

bool motionResume() {
    if (!m_state.global_enabled) return false;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return false;
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
    return true;
}

bool motionDwell(uint32_t ms) {
    // Non-blocking dwell command for G4 gcode
    // Uses one axis (axis 0) as the dwell controller
    if (!m_state.global_enabled) return false;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return false;

    // Only execute dwell if no motion is active
    if (m_state.active_axis == 255 && axes[0].state == MOTION_IDLE) {
        axes[0].state = MOTION_DWELL;
        axes[0].dwell_end_ms = millis() + ms;
        axes[0].state_entry_ms = millis();
        m_state.active_axis = 0;  // Mark axis 0 as "active" during dwell

        logInfo("[MOTION] Dwell: %lu ms (end at %lu)", (unsigned long)ms, (unsigned long)axes[0].dwell_end_ms);
        taskUnlockMutex(taskGetMotionMutex());
        taskSignalMotionUpdate();
        return true;
    }

    taskUnlockMutex(taskGetMotionMutex());
    return false;  // Cannot dwell while motion is active
}

bool motionWaitPin(uint8_t pin_id, uint8_t pin_type, uint8_t state, uint32_t timeout_sec) {
    // Non-blocking pin state wait command for M226 gcode
    // Uses axis 0 as the wait controller
    if (!m_state.global_enabled) return false;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return false;

    // Only execute pin wait if no motion is active
    if (m_state.active_axis == 255 && axes[0].state == MOTION_IDLE) {
        axes[0].state = MOTION_WAIT_PIN;
        axes[0].wait_pin_id = pin_id;
        axes[0].wait_pin_type = pin_type;
        axes[0].wait_pin_state = state;
        axes[0].wait_pin_timeout_ms = (timeout_sec > 0) ? (timeout_sec * 1000) : 0;
        axes[0].state_entry_ms = millis();
        m_state.active_axis = 0;  // Mark axis 0 as "active" during wait

        logInfo("[MOTION] Wait for pin: id=%d type=%d state=%d timeout=%lu sec",
                pin_id, pin_type, state, (unsigned long)timeout_sec);
        taskUnlockMutex(taskGetMotionMutex());
        taskSignalMotionUpdate();
        return true;
    }

    taskUnlockMutex(taskGetMotionMutex());
    return false;  // Cannot wait while motion is active
}

void motionEmergencyStop() {
    // PHASE 5.7: E-Stop Latency Monitoring (Gemini Recommendation)
    // Track E-Stop response time to detect priority inversion issues
    // Target: <50ms (ISO 13849 PLd), Warn if >50ms
    uint32_t estop_start_us = micros();

    // CRITICAL: Deadlock Prevention (Gemini Audit)
    // Use 10ms timeout to prevent deadlock if Motion task holds mutex while blocked on I2C
    // If timeout occurs, E-stop still succeeds via hardware PLC I/O (independent of mutex)
    // See: docs/GEMINI_FINAL_AUDIT.md for complete deadlock analysis
    bool got_mutex = taskLockMutex(taskGetMotionMutex(), 10);

    // PRIMARY SAFETY: Disable all axes at hardware level (PLC I/O)
    // This does NOT require motion_mutex - uses taskGetI2cPlcMutex() instead
    // Ensures axes stop even if mutex unavailable
    motionSetPLCAxisDirection(255, false, false);

    portENTER_CRITICAL(&motionSpinlock);
    m_state.global_enabled = false;
    for (int i = 0; i < MOTION_AXES; i++) axes[i].state = MOTION_ERROR;
    m_state.active_axis = 255;
    portEXIT_CRITICAL(&motionSpinlock);

    motionBuffer.clear();
    autoReportDisable();  // PHASE 4.0: Disable auto-report on E-Stop
    lcdSleepWakeup();    // PHASE 4.0: Wake display on E-Stop for visibility
    if (got_mutex) taskUnlockMutex(taskGetMotionMutex());

    // PHASE 5.7: E-Stop Latency Monitoring (Gemini Recommendation)
    // Measure and log E-Stop response time
    // Safety limits: IEC 61508 SIL2 (<100ms), ISO 13849 PLd (<50ms)
    uint32_t estop_latency_us = micros() - estop_start_us;
    if (estop_latency_us > 50000) {  // >50ms (50,000 microseconds)
        logWarning("[MOTION] [SAFETY] E-Stop latency high: %lu us (%.1f ms) - Target: <50ms",
                   (unsigned long)estop_latency_us, estop_latency_us / 1000.0f);
    }

    logError("[MOTION] [CRITICAL] EMERGENCY STOP ACTIVATED (Latency: %.1f ms)",
             estop_latency_us / 1000.0f);
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