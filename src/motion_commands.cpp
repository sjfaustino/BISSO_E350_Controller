#include "motion.h"
#include "system_constants.h"
#include "plc_iface.h"
#include "encoder_motion_integration.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "task_manager.h"
#include "safety.h"
#include "encoder_calibration.h" 
#include "encoder_wj66.h"        
#include <math.h> 
#include <string.h>
#include <stdlib.h> 
#include <stdio.h>

#define MOTION_MED_SPEED_DEFAULT 90.0f 

extern motion_axis_t axes[MOTION_AXES];
extern uint8_t active_axis;
extern int32_t active_start_position;
extern bool global_enabled;

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
  if (!global_enabled) {
    logError("[MOTION] System disabled");
    return;
  }
  
  if (!taskLockMutex(taskGetMotionMutex(), 100)) {
    logError("[MOTION] Mutex timeout");
    return;
  }

  for (int i = 0; i < MOTION_AXES; i++) {
      if (wj66IsStale(i)) {
          logError("[MOTION] Move rejected. Encoder %d stale.", i);
          faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, i, wj66GetAxisAge(i), "Encoder stale");
          taskUnlockMutex(taskGetMotionMutex());
          return;
      }
  }
  
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
    if (abs(target_counts - motionGetPosition(i)) > 1) { 
      active_axes_count++;
      target_axis = i;
      target_pos = target_counts;
    }
  }

  if (active_axes_count == 0) {
    logInfo("[MOTION] Already at target.");
    taskUnlockMutex(taskGetMotionMutex());
    return;
  }

  if (active_axes_count > 1) {
    logError("[MOTION] Multi-axis move rejected.");
    taskUnlockMutex(taskGetMotionMutex());
    return;
  }
  
  if (active_axis != 255) {
      logError("[MOTION] Axis %d busy.", active_axis);
      taskUnlockMutex(taskGetMotionMutex());
      return;
  }
  
  float effective_speed = (speed_mm_s == 0.0f) ? MOTION_MED_SPEED_DEFAULT : speed_mm_s;
  
  if (axes[target_axis].soft_limit_enabled) {
    if (target_pos < axes[target_axis].soft_limit_min || target_pos > axes[target_axis].soft_limit_max) {
      logError("[MOTION] Soft limit violation axis %d", target_axis);
      faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, target_axis, target_pos, "Target limit violation");
      taskUnlockMutex(taskGetMotionMutex());
      return;
    }
  }
  
  axes[target_axis].target_position = target_pos;
  axes[target_axis].position_at_stop = motionGetPosition(target_axis);

  speed_profile_t profile = motionMapSpeedToProfile(target_axis, effective_speed);
  axes[target_axis].saved_speed_profile = profile; 

  motionSetPLCSpeedProfile(profile); 

  bool is_forward = (target_pos > motionGetPosition(target_axis));
  motionSetPLCAxisDirection(255, false, false); 
  motionSetPLCAxisDirection(target_axis, true, is_forward); 

  active_axis = target_axis;
  active_start_position = motionGetPosition(target_axis);
  
  axes[target_axis].state = MOTION_WAIT_CONSENSO; 
  axes[target_axis].state_entry_ms = millis();
  
  logInfo("[MOTION] Moving Axis %d -> %d (Wait Consensus)", target_axis, target_pos);
  taskUnlockMutex(taskGetMotionMutex());
}

void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s) {
  float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_y = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_z = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_a = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG;

  float current_x = motionGetPosition(0) / scale_x;
  float current_y = motionGetPosition(1) / scale_y;
  float current_z = motionGetPosition(2) / scale_z;
  float current_a = motionGetPosition(3) / scale_a;

  motionMoveAbsolute(current_x + dx, current_y + dy, current_z + dz, current_a + da, speed_mm_s);
}

void motionStop() {
  if (!taskLockMutex(taskGetMotionMutex(), 100)) {
    logError("[MOTION] Mutex timeout (Stop)");
    return;
  }
  
  if (active_axis != 255) {
    motionSetPLCAxisDirection(255, false, false); 
    axes[active_axis].position_at_stop = motionGetPosition(active_axis); 
    axes[active_axis].state = MOTION_STOPPING;
    axes[active_axis].state_entry_ms = millis();
    logInfo("[MOTION] Stop axis %d", active_axis);
  } else {
    logInfo("[MOTION] Idle stop");
  }
  taskUnlockMutex(taskGetMotionMutex());
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
}

void motionResume() {
  if (!global_enabled) return;
  if (!taskLockMutex(taskGetMotionMutex(), 100)) return;

  if (active_axis != 255 && axes[active_axis].state == MOTION_PAUSED) {
      logInfo("[MOTION] Resuming axis %d", active_axis);
      motionSetPLCSpeedProfile(axes[active_axis].saved_speed_profile);

      int32_t current_pos = motionGetPosition(active_axis);
      int32_t target_pos = axes[active_axis].target_position;
      bool is_forward = (target_pos > current_pos); 

      motionSetPLCAxisDirection(255, false, false); 
      motionSetPLCAxisDirection(active_axis, true, is_forward);

      axes[active_axis].state = MOTION_WAIT_CONSENSO;
      axes[active_axis].state_entry_ms = millis();
  }
  taskUnlockMutex(taskGetMotionMutex());
}

void motionEmergencyStop() {
  bool got_mutex = taskLockMutex(taskGetMotionMutex(), 10); 
  motionSetPLCAxisDirection(255, false, false);
  global_enabled = false;
  
  for (int i = 0; i < MOTION_AXES; i++) axes[i].state = MOTION_ERROR;
  active_axis = 255;
  
  if (got_mutex) taskUnlockMutex(taskGetMotionMutex());
  else logError("[MOTION] E-Stop forced (Mutex timeout)");
  
  logError("[MOTION] [CRITICAL] EMERGENCY STOP ACTIVATED");
  faultLogError(FAULT_EMERGENCY_HALT, "E-Stop Activated");
}

bool motionClearEmergencyStop() {
  if (global_enabled == true) {
    Serial.println("[MOTION] [INFO] E-Stop already cleared");
    return true;
  }
  
  if (safetyIsAlarmed()) {
    Serial.println("[MOTION] [ERR] Cannot clear - Safety Alarm Active");
    return false;
  }
  
  global_enabled = true;
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].state == MOTION_ERROR) axes[i].state = MOTION_IDLE;
  }
  active_axis = 255;
  
  emergencyStopSetActive(false); 
  Serial.println("[MOTION] [OK] Emergency stop cleared");
  return true;
}