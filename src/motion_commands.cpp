/**
 * @file motion_commands.cpp
 * @brief High-level Trajectory Generation (G-Code -> Targets)
 * @project Gemini v2.1.0
 */

#include "motion.h"
#include "motion_buffer.h" // For buffer access if needed, though usually handled by parser
#include "system_constants.h"
#include "plc_iface.h"
#include "encoder_motion_integration.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "task_manager.h"
#include "safety.h"
#include "encoder_calibration.h" 
#include "encoder_wj66.h"        
#include "config_unified.h"
#include "config_keys.h" 
#include <math.h> 
#include <string.h>
#include <stdlib.h> 
#include <stdio.h>

extern motion_axis_t axes[MOTION_AXES];
extern uint8_t active_axis;
extern int32_t active_start_position;
extern bool global_enabled;

// ============================================================================
// MOVEMENT COMMANDS
// ============================================================================

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
  if (!global_enabled) {
    logError("[MOTION] System disabled");
    return;
  }
  
  if (!taskLockMutex(taskGetMotionMutex(), 100)) {
    logError("[MOTION] Mutex timeout");
    return;
  }

  // 1. Encoder Health Check
  for (int i = 0; i < MOTION_AXES; i++) {
      if (wj66IsStale(i)) {
          logError("[MOTION] Move rejected. Encoder %d stale.", i);
          faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, i, wj66GetAxisAge(i), "Encoder stale");
          taskUnlockMutex(taskGetMotionMutex());
          return;
      }
  }
  
  // 2. Determine Target Axis
  // (Standard Single-Axis Logic)
  uint8_t target_axis = 255;
  int32_t target_pos = 0;
  float targets_mm[] = {x, y, z, a};

  float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_y = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_z = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_a = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG; 
  float scales[] = {scale_x, scale_y, scale_z, scale_a};

  int active_axes_count = 0;
  for (int i = 0; i < MOTION_AXES; i++) {
    int32_t target_counts = (int32_t)(targets_mm[i] * scales[i]);
    // Check deviation > 1 count
    if (abs(target_counts - motionGetPosition(i)) > 1) { 
      active_axes_count++;
      target_axis = i;
      target_pos = target_counts;
    }
  }

  if (active_axes_count == 0) {
    // Already there
    taskUnlockMutex(taskGetMotionMutex());
    return;
  }

  if (active_axes_count > 1) {
    // This should be caught by GCodeParser splitting, but safe to keep as hard limit
    logError("[MOTION] Multi-axis move rejected (Safety Interlock).");
    taskUnlockMutex(taskGetMotionMutex());
    return;
  }
  
  if (active_axis != 255) {
      logError("[MOTION] Axis %d busy.", active_axis);
      taskUnlockMutex(taskGetMotionMutex());
      return;
  }
  
  // 3. Speed Profile Selection
  float effective_speed = (speed_mm_s <= 0.1f) ? 90.0f : speed_mm_s;
  
  // 4. Soft Limit Check
  if (axes[target_axis].soft_limit_enabled) {
    if (target_pos < axes[target_axis].soft_limit_min || target_pos > axes[target_axis].soft_limit_max) {
      logError("[MOTION] Soft limit violation axis %d", target_axis);
      faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, target_axis, target_pos, "Target limit violation");
      taskUnlockMutex(taskGetMotionMutex());
      return;
    }
  }
  
  // 5. Execute
  axes[target_axis].target_position = target_pos;
  axes[target_axis].position_at_stop = motionGetPosition(target_axis);

  speed_profile_t profile = motionMapSpeedToProfile(target_axis, effective_speed);
  axes[target_axis].saved_speed_profile = profile; 

  motionSetPLCSpeedProfile(profile); 

  bool is_forward = (target_pos > motionGetPosition(target_axis));
  motionSetPLCAxisDirection(255, false, false); // Reset others
  motionSetPLCAxisDirection(target_axis, true, is_forward); 

  active_axis = target_axis;
  active_start_position = motionGetPosition(target_axis);
  
  axes[target_axis].state = MOTION_WAIT_CONSENSO; 
  axes[target_axis].state_entry_ms = millis();
  
  logInfo("[MOTION] Moving Axis %d -> %d", target_axis, target_pos);
  
  taskUnlockMutex(taskGetMotionMutex());
  
  // Signal task to wake immediately for fast response
  taskSignalMotionUpdate();
}

void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s) {
  // Helper: Converts relative MM to absolute MM and calls MoveAbsolute
  // This is safe because MoveAbsolute calculates current position freshly
  float cur_x = motionGetPositionMM(0);
  float cur_y = motionGetPositionMM(1);
  float cur_z = motionGetPositionMM(2);
  float cur_a = motionGetPositionMM(3);

  motionMoveAbsolute(cur_x + dx, cur_y + dy, cur_z + dz, cur_a + da, speed_mm_s);
}