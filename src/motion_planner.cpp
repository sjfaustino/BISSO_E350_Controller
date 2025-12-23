/**
 * @file motion_planner.cpp
 * @brief Implementation of Motion Planning Logic v3.5.18
 */

#include "motion_planner.h"
#include "config_keys.h"
#include "config_unified.h"
#include "encoder_calibration.h"
#include "serial_logger.h"
#include "system_constants.h"
#include "task_manager.h" // For taskGetMotionMutex()
#include <math.h>
#include <stdlib.h>


MotionPlanner motionPlanner;

// CRITICAL FIX: Signature matched to motion_control.cpp
extern bool motionStartInternalMove(float x, float y, float z, float a,
                                    float speed_mm_s);

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

MotionPlanner::MotionPlanner() : feed_override(1.0f) {}

void MotionPlanner::init() {
  // CRITICAL: Buffer manages its own mutex via taskGetBufferMutex()
  motionBuffer.init();
  feed_override = 1.0f;
  logInfo("[PLANNER] Initialized");
}

void MotionPlanner::update(Axis *axes, uint8_t &active_axis,
                           int32_t &active_start_pos) {
  if (active_axis == 255) {
    checkBufferDrain(active_axis);
    return;
  }

  Axis *axis = &axes[active_axis];
  int32_t current_pos = axis->position;

  if (axis->state == MOTION_EXECUTING) {
    float effective_speed = axis->commanded_speed_mm_s * feed_override;
    speed_profile_t desired_profile =
        motionMapSpeedToProfile(active_axis, effective_speed);

    if (desired_profile != axis->saved_speed_profile) {
      motionSetPLCSpeedProfile(desired_profile);
      axis->saved_speed_profile = desired_profile;
    }

    checkLookAhead(axis, active_axis, active_start_pos);
    applyDynamicApproach(axis, active_axis, current_pos);
  }
}

bool MotionPlanner::checkBufferDrain(uint8_t &active_axis) {
  if (motionBuffer.isEmpty())
    return false;

  int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
  if (!buffer_enabled)
    return false;

  motion_cmd_t cmd;
  // Check if next move is valid before popping (Peek logic could be added here)
  if (motionBuffer.pop(&cmd)) {
    // CRITICAL FIX: Convert counts back to MM only when calling motion control
    // Motion control API still uses MM, but buffer stores counts to prevent
    // drift
    float x_scale = (machineCal.X.pulses_per_mm > 0)
                        ? machineCal.X.pulses_per_mm
                        : MOTION_POSITION_SCALE_FACTOR;
    float y_scale = (machineCal.Y.pulses_per_mm > 0)
                        ? machineCal.Y.pulses_per_mm
                        : MOTION_POSITION_SCALE_FACTOR;
    float z_scale = (machineCal.Z.pulses_per_mm > 0)
                        ? machineCal.Z.pulses_per_mm
                        : MOTION_POSITION_SCALE_FACTOR;
    float a_scale = (machineCal.A.pulses_per_degree > 0)
                        ? machineCal.A.pulses_per_degree
                        : MOTION_POSITION_SCALE_FACTOR_DEG;

    float x_mm = (float)cmd.x_counts / x_scale;
    float y_mm = (float)cmd.y_counts / y_scale;
    float z_mm = (float)cmd.z_counts / z_scale;
    float a_mm = (float)cmd.a_counts / a_scale;

    if (!motionStartInternalMove(x_mm, y_mm, z_mm, a_mm, cmd.speed_mm_s)) {
      logError("[PLANNER] Buffered move failed to start!");
      // Retry logic or Halt could go here
    }
    return true;
  }
  return false;
}

bool MotionPlanner::checkLookAhead(Axis *axis, uint8_t active_axis,
                                   int32_t &active_start_pos) {
  int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
  if (!buffer_enabled || motionBuffer.isEmpty())
    return false;

  motion_cmd_t nextCmd;
  if (motionBuffer.peek(&nextCmd)) {
    // CRITICAL FIX: Commands now stored as int32_t counts - no float
    // conversion! This eliminates cumulative rounding errors over long jobs
    int32_t next_target_counts = 0;
    if (active_axis == 0)
      next_target_counts = nextCmd.x_counts;
    else if (active_axis == 1)
      next_target_counts = nextCmd.y_counts;
    else if (active_axis == 2)
      next_target_counts = nextCmd.z_counts;
    else if (active_axis == 3)
      next_target_counts = nextCmd.a_counts;

    if (abs(next_target_counts - axis->target_position) < 10)
      return false;

    bool current_dir = (axis->target_position > active_start_pos);
    bool next_dir = (next_target_counts > axis->target_position);

    if (current_dir == next_dir) {
      motionBuffer.pop(NULL);
      logInfo("[PLANNER] Blending Axis %d -> %ld", active_axis,
              (long)next_target_counts);
      active_start_pos = axis->target_position;
      axis->target_position = next_target_counts;
      axis->commanded_speed_mm_s = nextCmd.speed_mm_s;
      return true;
    }
  }
  return false;
}

void MotionPlanner::applyDynamicApproach(Axis *axis, uint8_t active_axis,
                                         int32_t current_pos) {
  int32_t dist = abs(axis->target_position - current_pos);
  int32_t approach_mm = configGetInt(KEY_X_APPROACH, 50);

  // PHASE 5.10: CRITICAL FIX - Use correct axis scaling instead of hardcoded X-axis
  // Bug: Y/Z/A axes were using X-axis scaling, causing incorrect threshold calculations
  float scale = MOTION_POSITION_SCALE_FACTOR;
  switch (active_axis) {
    case 0: // X-axis
      scale = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : MOTION_POSITION_SCALE_FACTOR;
      break;
    case 1: // Y-axis
      scale = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : MOTION_POSITION_SCALE_FACTOR;
      break;
    case 2: // Z-axis
      scale = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : MOTION_POSITION_SCALE_FACTOR;
      break;
    case 3: // A-axis (rotary, degrees)
      scale = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : MOTION_POSITION_SCALE_FACTOR_DEG;
      break;
  }
  int32_t threshold_counts = (int32_t)(approach_mm * scale);

  if (dist < threshold_counts && axis->saved_speed_profile != SPEED_PROFILE_1) {
    motionSetPLCSpeedProfile(SPEED_PROFILE_1);
    axis->saved_speed_profile = SPEED_PROFILE_1;
  }
}

void MotionPlanner::setFeedOverride(float factor) {
  if (factor < 0.1f)
    factor = 0.1f;
  if (factor > 2.0f)
    factor = 2.0f;
  feed_override = factor;
}

float MotionPlanner::getFeedOverride() { return feed_override; }