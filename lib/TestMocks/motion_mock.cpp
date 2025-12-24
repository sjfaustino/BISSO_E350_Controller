/**
 * @file test/mocks/motion_mock.cpp
 * @brief Mock implementation of motion controller for testing
 */

#include "motion_mock.h"
#include <cstdio>
#include <cstring>
#include <cmath>

motion_mock_state_t motion_mock_init(void)
{
    motion_mock_state_t motion;
    std::memset(&motion, 0, sizeof(motion));

    motion.state = MOTION_IDLE;
    motion.active_axis = 255;          // No active axis
    motion.target_position_steps = 0;
    motion.current_position_steps = 0;
    motion.current_speed_hz = 0;
    motion.motion_start_time_ms = 0;

    // Soft limits: 0 to 500mm (assuming 100 PPM encoder)
    motion.soft_limit_low_steps = 0;
    motion.soft_limit_high_steps = 500 * 100;  // 50000 steps

    motion.min_safe_speed_hz = 1;      // LSP
    motion.max_safe_speed_hz = 105;    // HSP

    motion.stall_status = STALL_NONE;
    motion.motor_current_amps = 0.0f;
    motion.stall_current_threshold_amps = 8.0f;
    motion.stall_detection_time_ms = 0;

    motion.motion_quality_score = 100.0f;
    motion.velocity_deviation_percent = 0.0f;

    motion.move_attempts = 0;
    motion.move_completed = 0;
    motion.move_errors = 0;

    motion.e_stop_active = 0;

    return motion;
}

move_validation_result_t motion_mock_validate_move(
    motion_mock_state_t* motion,
    uint8_t axis,
    int32_t distance_steps,
    uint16_t speed_hz)
{
    if (!motion) return MOVE_INVALID_AXIS;

    // Check axis validity
    if (axis > 2) {
        return MOVE_INVALID_AXIS;
    }

    // Check distance validity (non-zero)
    if (distance_steps == 0) {
        return MOVE_INVALID_DISTANCE;
    }

    // Check speed validity
    if (speed_hz < motion->min_safe_speed_hz || speed_hz > motion->max_safe_speed_hz) {
        return MOVE_INVALID_SPEED;
    }

    // Check soft limits
    int32_t new_position = motion->current_position_steps + distance_steps;
    if (new_position < motion->soft_limit_low_steps ||
        new_position > motion->soft_limit_high_steps) {
        return MOVE_SOFT_LIMIT_VIOLATION;
    }

    // Check for E-stop
    if (motion->e_stop_active) {
        return MOVE_HARDWARE_ERROR;
    }

    return MOVE_VALID;
}

void motion_mock_start_move(
    motion_mock_state_t* motion,
    uint8_t axis,
    int32_t distance_steps,
    uint16_t speed_hz)
{
    if (!motion) return;

    motion->active_axis = axis;
    motion->target_position_steps = motion->current_position_steps + distance_steps;
    motion->current_speed_hz = speed_hz;
    motion->state = MOTION_MOVING;
    motion->motion_start_time_ms = 0;
    motion->motion_quality_score = 100.0f;
    motion->velocity_deviation_percent = 0.0f;
    motion->stall_status = STALL_NONE;
    motion->stall_detection_time_ms = 0;
    motion->move_attempts++;
}

void motion_mock_update(
    motion_mock_state_t* motion,
    int32_t encoder_feedback_steps,
    float encoder_velocity_mms,
    float motor_current_amps,
    uint32_t time_ms)
{
    if (!motion || motion->state != MOTION_MOVING) return;

    motion->current_position_steps = encoder_feedback_steps;
    motion->motor_current_amps = motor_current_amps;
    motion->motion_start_time_ms += time_ms;

    // Calculate expected velocity (1 Hz = 15 mm/s baseline, 100 PPM encoder)
    float velocity_per_hz = 15.0f / 100.0f;  // 0.15 mm/s per Hz
    float expected_velocity = motion->current_speed_hz * velocity_per_hz;

    // Calculate velocity deviation
    if (expected_velocity > 0.1f) {
        motion->velocity_deviation_percent =
            std::fabs(encoder_velocity_mms - expected_velocity) / expected_velocity * 100.0f;
    }

    // Update motion quality score based on deviation
    if (motion->velocity_deviation_percent < 5.0f) {
        motion->motion_quality_score = 100.0f;
    } else if (motion->velocity_deviation_percent < 20.0f) {
        motion->motion_quality_score = 90.0f - motion->velocity_deviation_percent;
    } else {
        motion->motion_quality_score = 50.0f;
    }

    // Stall detection: high current + no movement
    if (motor_current_amps > motion->stall_current_threshold_amps) {
        motion->stall_detection_time_ms += time_ms;

        if (std::fabs(encoder_velocity_mms) < 0.1f) {
            // Motor current high but no motion = stall
            if (motion->stall_detection_time_ms > 500) {
                motion->stall_status = STALL_DETECTED;
                motion->state = MOTION_STALLED;
            } else {
                motion->stall_status = STALL_WARNING;
            }
        } else {
            // High current but moving = normal load
            motion->stall_detection_time_ms = 0;
            motion->stall_status = STALL_NONE;
        }
    } else {
        motion->stall_detection_time_ms = 0;
        motion->stall_status = STALL_NONE;
    }

    // Check if motion is complete (position reached target)
    bool completed = false;
    if (motion->target_position_steps >= motion->current_position_steps) {
        // Moving forward - reached target when current >= target (accounting for update)
        if (encoder_feedback_steps >= motion->target_position_steps) {
            completed = true;
        }
    } else {
        // Moving backward - reached target when current <= target
        if (encoder_feedback_steps <= motion->target_position_steps) {
            completed = true;
        }
    }
    
    if (completed) {
        motion->state = MOTION_IDLE;
        motion->move_completed++;
    }
}

uint8_t motion_mock_is_complete(motion_mock_state_t* motion)
{
    if (!motion) return 0;

    return (motion->state == MOTION_IDLE && motion->move_completed > 0) ? 1 : 0;
}

void motion_mock_e_stop(motion_mock_state_t* motion)
{
    if (!motion) return;

    motion->e_stop_active = 1;
    motion->state = MOTION_E_STOPPED;
    motion->current_speed_hz = 0;
}

void motion_mock_clear_e_stop(motion_mock_state_t* motion)
{
    if (!motion) return;

    motion->e_stop_active = 0;
    motion->state = MOTION_IDLE;
}

void motion_mock_set_soft_limits(
    motion_mock_state_t* motion,
    uint8_t axis __attribute__((unused)),
    int32_t low_steps,
    int32_t high_steps)
{
    if (!motion) return;

    motion->soft_limit_low_steps = low_steps;
    motion->soft_limit_high_steps = high_steps;
}

stall_status_t motion_mock_get_stall_status(motion_mock_state_t* motion)
{
    if (!motion) return STALL_NONE;

    return motion->stall_status;
}

float motion_mock_get_quality_score(motion_mock_state_t* motion)
{
    if (!motion) return 0.0f;

    return motion->motion_quality_score;
}

float motion_mock_get_velocity_deviation(motion_mock_state_t* motion)
{
    if (!motion) return 0.0f;

    return motion->velocity_deviation_percent;
}

motion_state_t motion_mock_get_state(motion_mock_state_t* motion)
{
    if (!motion) return MOTION_ERROR;

    return motion->state;
}

void motion_mock_reset(motion_mock_state_t* motion)
{
    if (!motion) return;

    motion->state = MOTION_IDLE;
    motion->active_axis = 255;
    motion->target_position_steps = 0;
    motion->current_position_steps = 0;
    motion->current_speed_hz = 0;
    motion->motion_start_time_ms = 0;
    motion->stall_status = STALL_NONE;
    motion->stall_detection_time_ms = 0;
    motion->motor_current_amps = 0.0f;
    motion->motion_quality_score = 100.0f;
    motion->velocity_deviation_percent = 0.0f;
}

void motion_mock_get_diagnostics(motion_mock_state_t* motion, char* buffer, size_t buffer_size)
{
    if (!motion || !buffer) return;

    const char* state_names[] = {"IDLE", "MOVING", "STALLED", "ERROR", "E_STOP"};
    const char* stall_names[] = {"NONE", "WARNING", "DETECTED"};
    const char* state_str = (motion->state <= MOTION_E_STOPPED)
        ? state_names[motion->state]
        : "UNKNOWN";
    const char* stall_str = (motion->stall_status <= STALL_DETECTED)
        ? stall_names[motion->stall_status]
        : "UNKNOWN";

    snprintf(buffer, buffer_size,
        "MOTION[%s] Axis:%u Stall:%s Quality:%.0f%% Vel_Dev:%.1f%% Moves:%u/%u",
        state_str,
        motion->active_axis,
        stall_str,
        motion->motion_quality_score,
        motion->velocity_deviation_percent,
        motion->move_completed,
        motion->move_attempts);
}
