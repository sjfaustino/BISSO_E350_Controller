/**
 * @file axis.cpp
 * @brief Axis class implementation
 */

#include "axis.h"
#include "motion.h"
#include "motion_state_machine.h"
#include "axis_utilities.h"
#include "encoder_calibration.h"
#include "encoder_wj66.h"
#include "serial_logger.h"
#include "fault_logging.h"
#include "config_unified.h"
#include <Arduino.h>

// Extern declarations for symbols in motion_control.cpp
extern portMUX_TYPE motionSpinlock;

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
    last_actual_position = 0;
    last_actual_update_ms = 0;
    predicted_position = 0;
    velocity_counts_ms = 0.0f;
    active_start_position = 0;
}

void Axis::init(uint8_t axis_id) {
    id = axis_id;
    state = MOTION_IDLE;
    _error_logged = false;
    enabled = true;
}

bool Axis::checkSoftLimits(bool strict_mode) {
    if (!enabled || !soft_limit_enabled)
        return false;
    if (state >= MOTION_HOMING_APPROACH_FAST)
        return false;

    if (position < soft_limit_min || position > soft_limit_max) {
        if (strict_mode) {
            if (!_error_logged) {
                faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, id, position,
                              "Strict Limit Hit");
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

// These will be implemented in motion_control.cpp to update internal state
extern void motionUpdateExecutionMetrics(float progress, float remaining_seconds);

void Axis::updateState(int32_t current_pos, int32_t global_target_pos, bool consensus_active) {
    uint32_t current_time_ms = millis();
    if (prev_update_ms > 0) {
        uint32_t dt_ms = (current_time_ms >= prev_update_ms) 
            ? (current_time_ms - prev_update_ms) 
            : (UINT32_MAX - prev_update_ms + current_time_ms + 1);
        
        if (dt_ms > 1000) dt_ms = 1000;

        if (dt_ms > 0) {
            int32_t delta_pos = current_pos - prev_position;
            float scale = motionGetAxisScale(id);
            if (scale > 0.0f) {
                current_velocity_mm_s = ((float)delta_pos / (float)dt_ms) * 1000.0f / scale;
                accumulated_distance_units += (double)abs(delta_pos) / (double)scale;
            } else {
                current_velocity_mm_s = 0.0f;
            }
        }
    }

    if (current_pos != last_actual_position) {
        if (last_actual_update_ms > 0) {
            uint32_t dt_actual = (current_time_ms >= last_actual_update_ms)
                ? (current_time_ms - last_actual_update_ms)
                : (UINT32_MAX - last_actual_update_ms + current_time_ms + 1);
            
            if (dt_actual > 1000) dt_actual = 1000;
            if (dt_actual > 0) {
                velocity_counts_ms = (float)(current_pos - last_actual_position) / (float)dt_actual;
            }
        }
        last_actual_position = current_pos;
        last_actual_update_ms = current_time_ms;
    }

    prev_position = current_pos;
    prev_update_ms = current_time_ms;
    position = current_pos;

    // Calculate Progress & ETA
    bool is_active = (this->id == motionGetActiveAxis());
    int32_t start_pos = this->active_start_position;

    if (is_active) {
        int32_t total_dist_counts = abs(global_target_pos - start_pos);
        if (total_dist_counts > 0) {
            int32_t current_dist_counts = abs(current_pos - start_pos);
            float prog = ((float)current_dist_counts / (float)total_dist_counts) * 100.0f;
            if (prog > 100.0f) prog = 100.0f;
            
            float remaining = 0.0f;
            float abs_velocity = fabsf(current_velocity_mm_s);
            if (abs_velocity > 0.05f) {
                float ppm = encoderCalibrationGetPPM(id);
                if (ppm > 0.1f) {
                    float rem_dist_mm = (float)(total_dist_counts - current_dist_counts) / ppm;
                    remaining = (abs_velocity > 0.01f) ? (rem_dist_mm / abs_velocity) : 0.0f;
                }
            }
            motionUpdateExecutionMetrics(prog, remaining);
        }
    }

    MotionStateMachine::update(this, current_pos, global_target_pos, consensus_active);
}
