/**
 * @file motion_planner.cpp
 * @brief Implementation of Motion Planning Logic
 * @details Includes the speed mapping logic required by motion_control.
 */

#include "motion_planner.h"
#include "config_unified.h"
#include "config_keys.h"
#include "encoder_calibration.h" // Provides machineCal
#include "serial_logger.h"
#include <math.h>
#include <stdlib.h>

MotionPlanner motionPlanner;

// Forward Declaration for internal move command (Control layer)
extern void motionStartInternalMove(float x, float y, float z, float a, float speed_mm_s);

// Helper macro for min/max
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

MotionPlanner::MotionPlanner() : feed_override(1.0f) {}

void MotionPlanner::init() {
    motionBuffer.init();
    feed_override = 1.0f;
    logInfo("[PLANNER] Initialized");
}

void MotionPlanner::update(motion_axis_t* axes, uint8_t& active_axis, int32_t& active_start_pos) {
    // 1. IDLE STATE: Drain Buffer
    if (active_axis == 255) {
        checkBufferDrain(active_axis);
        return;
    }

    // 2. ACTIVE STATE: Planning Logic
    motion_axis_t* axis = &axes[active_axis];
    int32_t current_pos = axis->position; 

    if (axis->state == MOTION_EXECUTING) {
        
        // A. Feed Rate Updates
        float effective_speed = axis->commanded_speed_mm_s * feed_override;
        speed_profile_t desired_profile = motionMapSpeedToProfile(active_axis, effective_speed);
        
        if (desired_profile != axis->saved_speed_profile) {
            motionSetPLCSpeedProfile(desired_profile);
            axis->saved_speed_profile = desired_profile;
        }

        // B. Look-Ahead (Velocity Blending)
        checkLookAhead(axis, active_axis, active_start_pos);

        // C. Dynamic Approach
        applyDynamicApproach(axis, active_axis, current_pos);
    }
}

bool MotionPlanner::checkBufferDrain(uint8_t& active_axis) {
    if (motionBuffer.isEmpty()) return false;
    
    int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
    if (!buffer_enabled) return false;

    motion_cmd_t cmd;
    if (motionBuffer.pop(&cmd)) {
        motionStartInternalMove(cmd.x, cmd.y, cmd.z, cmd.a, cmd.speed_mm_s);
        return true;
    }
    return false;
}

bool MotionPlanner::checkLookAhead(motion_axis_t* axis, uint8_t active_axis, int32_t& active_start_pos) {
    int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
    if (!buffer_enabled || motionBuffer.isEmpty()) return false;

    motion_cmd_t nextCmd;
    if (motionBuffer.peek(&nextCmd)) {
        float scale = 1.0f;
        if(active_axis==0) scale = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : MOTION_POSITION_SCALE_FACTOR;
        else if(active_axis==1) scale = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : MOTION_POSITION_SCALE_FACTOR;
        else if(active_axis==2) scale = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : MOTION_POSITION_SCALE_FACTOR;
        else if(active_axis==3) scale = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : MOTION_POSITION_SCALE_FACTOR_DEG;

        float next_target_mm = 0.0f;
        if(active_axis==0) next_target_mm = nextCmd.x;
        else if(active_axis==1) next_target_mm = nextCmd.y;
        else if(active_axis==2) next_target_mm = nextCmd.z;
        else if(active_axis==3) next_target_mm = nextCmd.a;
        
        int32_t next_target_counts = (int32_t)(next_target_mm * scale);
        
        if (abs(next_target_counts - axis->target_position) < 10) return false;

        bool current_dir = (axis->target_position > active_start_pos);
        bool next_dir = (next_target_counts > axis->target_position);
        
        if (current_dir == next_dir) {
            motionBuffer.pop(NULL); 
            logInfo("[PLANNER] Blending Axis %d -> %ld", active_axis, (long)next_target_counts);
            active_start_pos = axis->target_position;
            axis->target_position = next_target_counts;
            axis->commanded_speed_mm_s = nextCmd.speed_mm_s;
            return true;
        }
    }
    return false;
}

void MotionPlanner::applyDynamicApproach(motion_axis_t* axis, uint8_t active_axis, int32_t current_pos) {
    if (active_axis != 0) return; 

    int32_t dist = abs(axis->target_position - current_pos);
    int32_t threshold = 0;
    float scale = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;

    if (configGetInt(KEY_MOTION_APPROACH_MODE, APPROACH_MODE_FIXED) == APPROACH_MODE_FIXED) {
        threshold = (int32_t)(configGetInt(KEY_X_APPROACH, 50) * scale);
    } else {
        float v = 0.0f;
        if (axis->saved_speed_profile == SPEED_PROFILE_3) v = machineCal.X.speed_fast_mm_min/60.0f;
        else if (axis->saved_speed_profile == SPEED_PROFILE_2) v = machineCal.X.speed_med_mm_min/60.0f;
        else v = machineCal.X.speed_slow_mm_min/60.0f;

        float a = configGetFloat(KEY_DEFAULT_ACCEL, 5.0f);
        if (a < 0.1f) a = 0.1f;
        threshold = (int32_t)(((v*v)/(2.0f*a)) * 1.1f * scale);
    }

    if (dist <= threshold && dist > 100 && axis->saved_speed_profile != SPEED_PROFILE_1) {
        motionSetPLCSpeedProfile(SPEED_PROFILE_1);
        axis->saved_speed_profile = SPEED_PROFILE_1;
    }
}

void MotionPlanner::setFeedOverride(float factor) {
    if (factor < 0.1f) factor = 0.1f;
    if (factor > 2.0f) factor = 2.0f;
    feed_override = factor;
    logInfo("[PLANNER] Feed Override: %.0f%%", factor * 100.0f);
}

float MotionPlanner::getFeedOverride() { return feed_override; }

// --- IMPLEMENTATION OF MISSING FUNCTION ---
speed_profile_t motionMapSpeedToProfile(uint8_t axis, float requested_speed_mm_s) {
    // Simple mapping based on X-axis calibration for now. 
    // In future, can be axis-specific.
    
    // Convert Config (mm/min) to mm/s
    float slow_limit = machineCal.X.speed_slow_mm_min / 60.0f;
    float med_limit  = machineCal.X.speed_med_mm_min / 60.0f;
    // float fast_limit = machineCal.X.speed_fast_mm_min / 60.0f; 

    // Logic: If requested speed is below medium threshold, use Slow.
    // If between Medium and Fast, use Medium.
    // Else Fast.
    
    // Safety Fallbacks if config is 0
    if (slow_limit < 0.1f) slow_limit = 5.0f;
    if (med_limit < 0.1f) med_limit = 15.0f;

    // Mapping
    if (requested_speed_mm_s <= slow_limit) return SPEED_PROFILE_1;
    if (requested_speed_mm_s <= med_limit) return SPEED_PROFILE_2;
    return SPEED_PROFILE_3;
}