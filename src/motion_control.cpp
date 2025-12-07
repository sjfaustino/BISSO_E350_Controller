/**
 * @file motion_control.cpp
 * @brief Real-Time Hardware Execution Layer (Gemini v3.2.1)
 * @details The "Black Box" Engine. Owns all state and logic.
 * @author Sergio Faustino
 */

#include "motion.h"
#include "motion_planner.h"
#include "motion_state.h" // For accessors like motionGetPosition
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

// ============================================================================
// CORE STATE (Owned by this module)
// ============================================================================

// FIX: Removed 'static' so motion_state.cpp can link to it via extern.
// Visibility is still restricted because 'axes' is NOT declared in motion.h.
motion_axis_t axes[MOTION_AXES] = {
  {0, 0, MOTION_IDLE, 0, 0, true, -500000, 500000, true, 0, 0, SPEED_PROFILE_1, 0.0f}, 
  {0, 0, MOTION_IDLE, 0, 0, true, -300000, 300000, true, 0, 0, SPEED_PROFILE_1, 0.0f}, 
  {0, 0, MOTION_IDLE, 0, 0, true, 0, 150000, true, 0, 0, SPEED_PROFILE_1, 0.0f},       
  {0, 0, MOTION_IDLE, 0, 0, true, -45000, 45000, true, 0, 0, SPEED_PROFILE_1, 0.0f}   
};

// Global flags
uint8_t active_axis = 255; 
int32_t active_start_position = 0;
bool global_enabled = true; 
static bool encoder_feedback_enabled = false;

const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, 255}; 
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_Q73_CONSENSO_X, ELBO_Q73_CONSENSO_Y, ELBO_Q73_CONSENSO_Z, 255};

// ============================================================================
// INITIALIZATION
// ============================================================================

void motionInit() {
  logInfo("[MOTION] Initializing Engine v3.2.1...");
  
  if (MOTION_AXES != 4) {
    faultLogError(FAULT_BOOT_FAILED, "Invalid axis count");
    return;
  }
  
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].state = MOTION_IDLE;
    axes[i].enabled = true;
  }
  
  motionPlanner.init(); 
  motionSetPLCAxisDirection(255, false, false); 
  logInfo("[MOTION] [OK] Ready");
}

// ============================================================================
// MAIN REAL-TIME LOOP (10ms)
// ============================================================================

void motionUpdate() {
  if (!global_enabled) return;
  if (!taskLockMutex(taskGetMotionMutex(), 0)) return; 
  
  // 1. UPDATE INPUTS
  if (active_axis != 255) {
      axes[active_axis].position = wj66GetPosition(active_axis);
  }

  // 2. RUN PLANNER
  motionPlanner.update(axes, active_axis, active_start_position);

  // 3. EXECUTE STATE MACHINE
  if (active_axis != 255) {
    motion_axis_t* axis = &axes[active_axis];
    int32_t current_pos = axis->position;

    if (!axis->enabled || axis->state == MOTION_ERROR) {
      taskUnlockMutex(taskGetMotionMutex());
      return;
    }
    
    // Soft Limits
    if (axis->soft_limit_enabled) {
      if (current_pos < axis->soft_limit_min || current_pos > axis->soft_limit_max) {
        faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, active_axis, current_pos, "Limit Hit");
        motionEmergencyStop(); 
        taskUnlockMutex(taskGetMotionMutex());
        return;
      }
    }
    
    switch (axis->state) {
      case MOTION_WAIT_CONSENSO:
        if (millis() - axis->state_entry_ms > MOTION_CONSENSO_TIMEOUT_MS) {
            faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, active_axis, 0, "Consensus Timeout");
            motionSetPLCAxisDirection(255, false, false);
            axis->state = MOTION_ERROR;
        } else {
            uint8_t bit = AXIS_TO_CONSENSO_BIT[active_axis];
            if (bit == 255 || elboQ73GetConsenso(bit)) {
                axis->state = MOTION_EXECUTING;
                axis->state_entry_ms = millis();
            }
        }
        break;

      case MOTION_EXECUTING:
        if ((active_start_position < axis->target_position && current_pos >= axis->target_position) ||
            (active_start_position > axis->target_position && current_pos <= axis->target_position)) {
          axis->position = axis->target_position;
          axis->state = MOTION_STOPPING;         
          axis->state_entry_ms = millis();
          motionSetPLCAxisDirection(255, false, false); 
          axis->position_at_stop = current_pos;
        }
        break;
        
      case MOTION_STOPPING:
        {
            int32_t deadband = configGetInt(KEY_MOTION_DEADBAND, 10);
            if (abs(current_pos - axis->position_at_stop) < deadband) { 
                axis->state = MOTION_IDLE;
                active_axis = 255; 
            }
        }
        break;
      default: break;
    }
  }
  taskUnlockMutex(taskGetMotionMutex());
}

// ============================================================================
// MOVEMENT LOGIC
// ============================================================================

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
  if (!global_enabled) { logError("[MOTION] System disabled"); return; }
  if (!taskLockMutex(taskGetMotionMutex(), 100)) { logError("[MOTION] Mutex timeout"); return; }

  // 1. Encoder Check
  for (int i = 0; i < MOTION_AXES; i++) {
      if (wj66IsStale(i)) {
          logError("[MOTION] Move rejected. Encoder %d stale.", i);
          faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, i, wj66GetAxisAge(i), "Encoder stale");
          taskUnlockMutex(taskGetMotionMutex()); return;
      }
  }
  
  // 2. Determine Axis
  uint8_t target_axis = 255;
  int32_t target_pos = 0;
  float targets_mm[] = {x, y, z, a};
  float scales[] = {
      (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR,
      (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG
  };

  int active_axes_count = 0;
  for (int i = 0; i < MOTION_AXES; i++) {
    int32_t target_counts = (int32_t)(targets_mm[i] * scales[i]);
    if (abs(target_counts - motionGetPosition(i)) > 1) { 
      active_axes_count++;
      target_axis = i;
      target_pos = target_counts;
    }
  }

  if (active_axes_count == 0) { taskUnlockMutex(taskGetMotionMutex()); return; }
  if (active_axes_count > 1) { logError("[MOTION] Multi-axis move rejected."); taskUnlockMutex(taskGetMotionMutex()); return; }
  if (active_axis != 255) { logError("[MOTION] Axis %d busy.", active_axis); taskUnlockMutex(taskGetMotionMutex()); return; }
  
  // 3. Setup State
  float base_speed = (speed_mm_s <= 0.1f) ? 90.0f : speed_mm_s;
  axes[target_axis].commanded_speed_mm_s = base_speed; 
  
  // 4. Soft Limits
  if (axes[target_axis].soft_limit_enabled) {
    if (target_pos < axes[target_axis].soft_limit_min || target_pos > axes[target_axis].soft_limit_max) {
      logError("[MOTION] Soft limit violation axis %d", target_axis);
      faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, target_axis, target_pos, "Target limit violation");
      taskUnlockMutex(taskGetMotionMutex()); return;
    }
  }
  
  // 5. Commit
  axes[target_axis].target_position = target_pos;
  axes[target_axis].position_at_stop = motionGetPosition(target_axis);

  float effective_speed = base_speed * motionPlanner.getFeedOverride();
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
  
  logInfo("[MOTION] Moving Axis %d -> %d (F%.1f)", target_axis, target_pos, base_speed);
  
  taskUnlockMutex(taskGetMotionMutex());
  taskSignalMotionUpdate();
}

void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s) {
  float cur_x = motionGetPositionMM(0);
  float cur_y = motionGetPositionMM(1);
  float cur_z = motionGetPositionMM(2);
  float cur_a = motionGetPositionMM(3);
  motionMoveAbsolute(cur_x + dx, cur_y + dy, cur_z + dz, cur_a + da, speed_mm_s);
}

// ----------------------------------------------------------------------------
// INTERNAL HELPERS
// ----------------------------------------------------------------------------

void motionStartInternalMove(float x, float y, float z, float a, float speed_mm_s) {
    motionMoveAbsolute(x, y, z, a, speed_mm_s);
}

// ----------------------------------------------------------------------------
// API IMPLEMENTATIONS
// ----------------------------------------------------------------------------

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
    axes[axis].soft_limit_enabled = enable;
  }
}

bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos) {
  if (axis >= MOTION_AXES) return false;
  *min_pos = axes[axis].soft_limit_min;
  *max_pos = axes[axis].soft_limit_max;
  return axes[axis].soft_limit_enabled;
}

bool motionIsValidStateTransition(uint8_t axis, motion_state_t new_state) { return true; }

bool motionSetState(uint8_t axis, motion_state_t new_state) {
  if (axis >= MOTION_AXES || !taskLockMutex(taskGetMotionMutex(), 100)) return false;
  axes[axis].state = new_state;
  if (new_state == MOTION_WAIT_CONSENSO || new_state == MOTION_STOPPING) axes[axis].state_entry_ms = millis();
  if (new_state == MOTION_IDLE || new_state == MOTION_ERROR) {
      motionSetPLCAxisDirection(255, false, false);
      active_axis = 255;
  }
  taskUnlockMutex(taskGetMotionMutex());
  return true;
}

void motionEnableEncoderFeedback(bool enable) {
  encoder_feedback_enabled = enable;
  encoderMotionEnableFeedback(enable);
}

bool motionIsEncoderFeedbackEnabled() {
  return encoder_feedback_enabled;
}

// ============================================================================
// CRITICAL CONTROL
// ============================================================================

void motionStop() {
  if (!taskLockMutex(taskGetMotionMutex(), 100)) return;
  if (active_axis != 255) {
    motionSetPLCAxisDirection(255, false, false); 
    axes[active_axis].position_at_stop = motionGetPosition(active_axis); 
    axes[active_axis].state = MOTION_STOPPING;
    axes[active_axis].state_entry_ms = millis();
    logInfo("[MOTION] Stop axis %d", active_axis);
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
      axes[active_axis].saved_speed_profile = prof;
      int32_t current_pos = motionGetPosition(active_axis);
      int32_t target_pos = axes[active_axis].target_position;
      bool is_forward = (target_pos > current_pos); 
      motionSetPLCAxisDirection(255, false, false); 
      motionSetPLCAxisDirection(active_axis, true, is_forward);
      axes[active_axis].state = MOTION_WAIT_CONSENSO;
      axes[active_axis].state_entry_ms = millis();
  }
  taskUnlockMutex(taskGetMotionMutex());
  taskSignalMotionUpdate();
}

void motionEmergencyStop() {
  bool got_mutex = taskLockMutex(taskGetMotionMutex(), 10); 
  motionSetPLCAxisDirection(255, false, false);
  global_enabled = false;
  for (int i = 0; i < MOTION_AXES; i++) axes[i].state = MOTION_ERROR;
  active_axis = 255;
  motionBuffer.clear(); 
  if (got_mutex) taskUnlockMutex(taskGetMotionMutex());
  logError("[MOTION] [CRITICAL] EMERGENCY STOP ACTIVATED");
  faultLogError(FAULT_EMERGENCY_HALT, "E-Stop Activated");
  taskSignalMotionUpdate();
}

bool motionClearEmergencyStop() {
  if (global_enabled) { Serial.println("[MOTION] E-Stop already cleared"); return true; }
  if (safetyIsAlarmed()) { Serial.println("[MOTION] Cannot clear - Alarm Active"); return false; }
  global_enabled = true;
  for (int i = 0; i < MOTION_AXES; i++) if (axes[i].state == MOTION_ERROR) axes[i].state = MOTION_IDLE;
  active_axis = 255;
  emergencyStopSetActive(false); 
  Serial.println("[MOTION] [OK] Emergency stop cleared");
  taskSignalMotionUpdate();
  return true;
}

void motionDiagnostics() {
  Serial.printf("\n[MOTION] Global: %s | Active: %d | Feed: %.0f%%\n", 
      global_enabled ? "ON" : "OFF", active_axis, motionPlanner.getFeedOverride() * 100.0f);
  for (int i = 0; i < MOTION_AXES; i++) {
    Serial.printf("  Axis %d: %s | Pos: %ld | Tgt: %ld | Spd: %.1f\n", 
                  i, motionStateToString(axes[i].state), (long)motionGetPosition(i), (long)axes[i].target_position, axes[i].commanded_speed_mm_s);
  }
}