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

// Access global state arrays and internal helpers from motion_core.cpp
extern motion_axis_t axes[MOTION_AXES];
extern uint8_t active_axis;
extern int32_t active_start_position;
extern bool global_enabled;


// ============================================================================
// MOTION COMMAND API IMPLEMENTATIONS
// ============================================================================

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
  if (!global_enabled) {
    logError("[MOTION] ERROR: Motion system disabled");
    return;
  }
  
  // Acquire mutex
  if (!taskLockMutex(taskGetMotionMutex(), 100)) {
    logError("[MOTION] ERROR: Could not acquire motion mutex");
    return;
  }

  // --- CRITICAL SAFETY CHECK: ENCODER FRESHNESS ---
  for (int i = 0; i < MOTION_AXES; i++) {
      if (wj66IsStale(i)) {
          logError("[MOTION] ERROR: Move rejected. Encoder axis %d is stale.", i);
          faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, i, wj66GetAxisAge(i), 
                        "Move rejected: Encoder data stale (Age: %lu ms)", wj66GetAxisAge(i));
          taskUnlockMutex(taskGetMotionMutex());
          return;
      }
  }
  
  // *** TARGET CALCULATION AND VALIDATION ***
  uint8_t target_axis = 255;
  int32_t target_pos = 0;
  float targets_mm[] = {x, y, z, a};

  // --- FIX: Use Calibrated Scale Factors per Axis (Linear/Angular Fallback) ---
  float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_y = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_z = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  // Use specific degree scale factor for A-axis fallback
  float scale_a = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG; 
  float scales[] = {scale_x, scale_y, scale_z, scale_a};
  // -----------------------------------------------------------------------------

  int active_axes_count = 0;
  
  for (int i = 0; i < MOTION_AXES; i++) {
    int32_t target_counts = (int32_t)(targets_mm[i] * scales[i]);
    
    // Check if the target is significantly different from current position
    if (abs(target_counts - motionGetPosition(i)) > 1) { 
      active_axes_count++;
      target_axis = i;
      target_pos = target_counts;
    }
  }

  if (active_axes_count == 0) {
    logInfo("[MOTION] Move command received, but target is current position. Idle.");
    taskUnlockMutex(taskGetMotionMutex());
    return;
  }

  if (active_axes_count > 1) {
    logError("[MOTION] ERROR: Multi-axis move rejected (active axes: %d)", active_axes_count);
    faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, -1, active_axes_count, 
                  "Multi-axis move rejected (%d requested)", active_axes_count);
    taskUnlockMutex(taskGetMotionMutex());
    return;
  }
  
  if (active_axis != 255) {
      logError("[MOTION] ERROR: Move rejected. Axis %d is already active.", active_axis);
      taskUnlockMutex(taskGetMotionMutex());
      return;
  }
  
  float effective_speed = (speed_mm_s == 0.0f) ? MOTION_MED_SPEED_DEFAULT : speed_mm_s;
  
  // SAFETY: Validate target position against soft limits (using internal counts)
  if (axes[target_axis].soft_limit_enabled) {
    if (target_pos < axes[target_axis].soft_limit_min || target_pos > axes[target_axis].soft_limit_max) {
      logError("[MOTION] ERROR: Axis %d target %d violates soft limits [%d, %d]",
               target_axis, target_pos, axes[target_axis].soft_limit_min, axes[target_axis].soft_limit_max);
      
      faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, target_axis, target_pos, 
                    "Target position %d violates axis %d limits", target_pos, target_axis);
      taskUnlockMutex(taskGetMotionMutex());
      return;
    }
  }
  
  // Setup the Move
  axes[target_axis].target_position = target_pos;
  axes[target_axis].position_at_stop = motionGetPosition(target_axis);

  speed_profile_t profile = motionMapSpeedToProfile(target_axis, effective_speed);
  axes[target_axis].saved_speed_profile = profile; // Save for Resume functionality

  motionSetPLCSpeedProfile(profile); // Note: This function ensures VS is OFF

  bool is_forward = (target_pos > motionGetPosition(target_axis));
  
  // Assert PLC Signals
  motionSetPLCAxisDirection(255, false, false); 
  motionSetPLCAxisDirection(target_axis, true, is_forward); 

  // Update State - WAIT FOR HANDSHAKE
  active_axis = target_axis;
  active_start_position = motionGetPosition(target_axis);
  
  axes[target_axis].state = MOTION_WAIT_CONSENSO; // <-- HANDSHAKE STATE
  axes[target_axis].state_entry_ms = millis();
  
  logInfo("[MOTION] Moving Axis %d to target %d. Waiting for Consensus...", target_axis, target_pos);
  
  taskUnlockMutex(taskGetMotionMutex());
}

void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s) {
  // --- FIX: Use Calibrated Scale Factors per Axis for Relative Moves ---
  float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_y = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_z = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_a = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG;
  // --------------------------------------------------------------------

  float current_x = motionGetPosition(0) / scale_x;
  float current_y = motionGetPosition(1) / scale_y;
  float current_z = motionGetPosition(2) / scale_z;
  float current_a = motionGetPosition(3) / scale_a;

  motionMoveAbsolute(current_x + dx, current_y + dy, current_z + dz, current_a + da, speed_mm_s);
}

void motionStop() {
  if (!taskLockMutex(taskGetMotionMutex(), 100)) {
    logError("[MOTION] ERROR: Could not acquire motion mutex for Stop");
    return;
  }
  
  if (active_axis != 255) {
    motionSetPLCAxisDirection(255, false, false); 
    axes[active_axis].position_at_stop = motionGetPosition(active_axis); 
    axes[active_axis].state = MOTION_STOPPING;
    axes[active_axis].state_entry_ms = millis();
    logInfo("[MOTION] Stop commanded on axis %d. Transition to MOTION_STOPPING.", active_axis);
  } else {
    logInfo("[MOTION] Stop commanded but no axis was active. Idle.");
  }

  taskUnlockMutex(taskGetMotionMutex());
}

void motionPause() {
  if (!global_enabled) return;
  if (!taskLockMutex(taskGetMotionMutex(), 100)) return;

  if (active_axis != 255 && (axes[active_axis].state == MOTION_EXECUTING || axes[active_axis].state == MOTION_WAIT_CONSENSO)) {
      // 1. Stop physical motion immediately
      motionSetPLCAxisDirection(255, false, false);
      
      // 2. Transition state
      axes[active_axis].state = MOTION_PAUSED;
      logInfo("[MOTION] PAUSED Axis %d. (Profile %d saved)", active_axis, axes[active_axis].saved_speed_profile);
  } else {
      logWarning("[MOTION] Pause ignored: No active motion to pause.");
  }

  taskUnlockMutex(taskGetMotionMutex());
}

void motionResume() {
  if (!global_enabled) return;
  if (!taskLockMutex(taskGetMotionMutex(), 100)) return;

  if (active_axis != 255 && axes[active_axis].state == MOTION_PAUSED) {
      logInfo("[MOTION] Resuming Axis %d...", active_axis);

      // 1. Restore Speed Profile
      motionSetPLCSpeedProfile(axes[active_axis].saved_speed_profile);

      // 2. Restore Direction/Axis Bits
      int32_t current_pos = motionGetPosition(active_axis);
      int32_t target_pos = axes[active_axis].target_position;
      bool is_forward = (target_pos > current_pos); // Re-evaluate direction based on current pos

      motionSetPLCAxisDirection(255, false, false); // Clear first
      motionSetPLCAxisDirection(active_axis, true, is_forward);

      // 3. Re-enter Handshake State (Safety First)
      axes[active_axis].state = MOTION_WAIT_CONSENSO;
      axes[active_axis].state_entry_ms = millis();
  } else {
      logWarning("[MOTION] Resume ignored: Not in PAUSED state.");
  }

  taskUnlockMutex(taskGetMotionMutex());
}

void motionEmergencyStop() {
  bool got_mutex = taskLockMutex(taskGetMotionMutex(), 10); 
  motionSetPLCAxisDirection(255, false, false);
  global_enabled = false;
  
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].state = MOTION_ERROR;
  }
  active_axis = 255;
  
  if (got_mutex) {
    taskUnlockMutex(taskGetMotionMutex());
  } else {
    logError("[MOTION] WARNING: Could not acquire mutex for emergency stop (forced PLC I/O clear)");
  }
  
  logError("[MOTION] ⚠️  EMERGENCY STOP ACTIVATED ⚠️");
  faultLogError(FAULT_EMERGENCY_HALT, "Emergency stop activated - motion halted");
}

bool motionClearEmergencyStop() {
  if (global_enabled == true) {
    Serial.println("[MOTION] INFO: Emergency stop already cleared");
    return true;
  }
  
  if (safetyIsAlarmed()) {
    Serial.println("[MOTION] ERROR: Cannot clear E-stop - safety system alarmed");
    faultLogWarning(FAULT_EMERGENCY_HALT, "E-stop clear attempted with safety alarm active");
    return false;
  }
  
  global_enabled = true;
  
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].state == MOTION_ERROR) {
      axes[i].state = MOTION_IDLE;
    }
  }
  active_axis = 255;
  
  // Notify fault logging system that the halt flag can be cleared
  emergencyStopSetActive(false); 

  Serial.println("[MOTION] ✅ Emergency stop cleared - System ready");
  faultLogWarning(FAULT_EMERGENCY_HALT, "Emergency stop cleared - system recovered");
  
  return true;
}