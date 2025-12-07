/**
 * @file motion_control.cpp
 * @brief Real-Time Hardware Execution Layer (Gemini v3.4.1)
 * @details Implements Configurable "Strict Limits" to allow recovery.
 * @author Sergio Faustino
 */

#include "motion.h"
#include "motion_planner.h"
#include "motion_state.h" // Accessors (motionGetPosition, motionStateToString)
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

// Global Axes Definition
motion_axis_t axes[MOTION_AXES] = {
  {0, 0, MOTION_IDLE, 0, 0, true, -500000, 500000, true, 0, 0, SPEED_PROFILE_1, 0.0f, 0}, 
  {0, 0, MOTION_IDLE, 0, 0, true, -300000, 300000, true, 0, 0, SPEED_PROFILE_1, 0.0f, 0}, 
  {0, 0, MOTION_IDLE, 0, 0, true, 0, 150000, true, 0, 0, SPEED_PROFILE_1, 0.0f, 0},       
  {0, 0, MOTION_IDLE, 0, 0, true, -45000, 45000, true, 0, 0, SPEED_PROFILE_1, 0.0f, 0}   
};

// Global flags
uint8_t active_axis = 255; 
int32_t active_start_position = 0;
bool global_enabled = true; 
static bool encoder_feedback_enabled = false;

// Hardware Mapping Arrays
const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, ELBO_I73_AXIS_A}; 
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_I73_CONSENSO_X, ELBO_I73_CONSENSO_Y, ELBO_I73_CONSENSO_Z, ELBO_I73_CONSENSO_A};

// Forward Declarations
// FIX: These must NOT be static because they are declared in motion.h
void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction);
void motionSetPLCSpeedProfile(speed_profile_t profile);

// ============================================================================
// INITIALIZATION
// ============================================================================

void motionInit() {
  logInfo("[MOTION] Initializing Engine v3.4.1 (Configurable Limits)...");
  
  if (MOTION_AXES != 4) {
    faultLogError(FAULT_BOOT_FAILED, "Invalid axis count");
    return;
  }
  
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].state = MOTION_IDLE;
    axes[i].enabled = true;
  }
  
  motionPlanner.init(); 
  
  // Safe Start: Disable all motion relays
  motionSetPLCAxisDirection(255, false, false); 
  
  logInfo("[MOTION] [OK] Ready");
}

// ============================================================================
// MAIN REAL-TIME LOOP (10ms)
// ============================================================================

void motionUpdate() {
  if (!global_enabled) return;
  // Non-blocking mutex lock try to keep loop tight
  if (!taskLockMutex(taskGetMotionMutex(), 0)) return; 
  
  // 1. GLOBAL MONITOR & INPUTS
  int strict_limits = configGetInt("motion_strict_limits", 1);

  for (int i = 0; i < MOTION_AXES; i++) {
      // Always update position cache
      axes[i].position = wj66GetPosition(i); 
      
      // Strict Monitor Logic
      if (strict_limits && axes[i].enabled && axes[i].soft_limit_enabled) {
          
          // Skip during Homing (must hit physical switches)
          if (axes[i].state >= MOTION_HOMING_APPROACH_FAST) continue;

          // Check Bounds
          if (axes[i].position < axes[i].soft_limit_min || axes[i].position > axes[i].soft_limit_max) {
              
              faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, i, axes[i].position, "Strict Limit Violation");
              
              // Force E-Stop
              motionSetPLCAxisDirection(255, false, false);
              global_enabled = false;
              for(int j=0; j<MOTION_AXES; j++) axes[j].state = MOTION_ERROR;
              motionBuffer.clear();
              
              logError("[MOTION] [CRITICAL] E-Stop: Axis %d Out of Bounds", i);
              taskUnlockMutex(taskGetMotionMutex());
              return;
          }
      }
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
    
    switch (axis->state) {
      // --- STANDARD MOTION STATES ---
      case MOTION_WAIT_CONSENSO:
        if (millis() - axis->state_entry_ms > MOTION_CONSENSO_TIMEOUT_MS) {
            faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, active_axis, 0, "Consensus Timeout");
            motionSetPLCAxisDirection(255, false, false);
            axis->state = MOTION_ERROR;
        } else {
            uint8_t bit = AXIS_TO_CONSENSO_BIT[active_axis];
            if (elboI73GetInput(bit)) {
                if (axis->state == MOTION_WAIT_CONSENSO) axis->state = MOTION_EXECUTING;
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

      // --- SMART HOMING LOGIC ---
      case MOTION_HOMING_APPROACH_FAST:
      case MOTION_HOMING_APPROACH_FINE:
        {
            uint8_t switch_bit = AXIS_TO_I73_BIT[active_axis];
            bool hit = elboI73GetInput(switch_bit);
            
            if (millis() - axis->state_entry_ms > 45000) { 
                logError("[HOME] Timeout finding switch");
                motionStop();
                axis->state = MOTION_ERROR;
            }

            if (hit) {
                motionSetPLCAxisDirection(255, false, false); 
                
                if (axis->state == MOTION_HOMING_APPROACH_FAST) {
                    logInfo("[HOME] Found Switch (Fast). Backing off...");
                    motionSetPLCSpeedProfile(SPEED_PROFILE_1); 
                    motionSetPLCAxisDirection(active_axis, true, true); 
                    axis->state = MOTION_HOMING_BACKOFF;
                    axis->state_entry_ms = millis();
                } 
                else {
                    axis->homing_trigger_pos = current_pos; 
                    logInfo("[HOME] Trigger at %ld. Settling...", (long)current_pos);
                    axis->state = MOTION_HOMING_SETTLE;
                    axis->state_entry_ms = millis();
                }
            }
        }
        break;

      case MOTION_HOMING_BACKOFF:
        {
            uint8_t switch_bit = AXIS_TO_I73_BIT[active_axis];
            bool hit = elboI73GetInput(switch_bit);
            if (!hit) {
                if (millis() - axis->state_entry_ms > 1000) { 
                    motionSetPLCAxisDirection(255, false, false);
                    logInfo("[HOME] Clear. Starting Fine Approach...");
                    motionSetPLCSpeedProfile(SPEED_PROFILE_1); 
                    motionSetPLCAxisDirection(active_axis, true, false); 
                    axis->state = MOTION_HOMING_APPROACH_FINE;
                    axis->state_entry_ms = millis();
                }
            }
        }
        break;

      case MOTION_HOMING_SETTLE:
        {
            if (millis() - axis->state_entry_ms > HOMING_SETTLE_MS) {
                int32_t resting_pos = current_pos;
                int32_t skid = abs(resting_pos - axis->homing_trigger_pos);
                
                logInfo("[HOME] Settled. Skid: %ld counts.", (long)skid);
                
                wj66SetZero(active_axis); 
                
                axis->position = 0;
                axis->target_position = 0;
                axis->state = MOTION_IDLE;
                active_axis = 255;
                
                logInfo("[HOME] Complete. Axis Zeroed.");
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

  for (int i = 0; i < MOTION_AXES; i++) {
      if (wj66IsStale(i)) {
          logError("[MOTION] Move rejected. Encoder %d stale.", i);
          faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, i, wj66GetAxisAge(i), "Encoder stale");
          taskUnlockMutex(taskGetMotionMutex()); return;
      }
  }
  
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
  
  float base_speed = (speed_mm_s <= 0.1f) ? 90.0f : speed_mm_s;
  axes[target_axis].commanded_speed_mm_s = base_speed; 
  
  // Soft Limits Check (Target Validation)
  if (axes[target_axis].soft_limit_enabled) {
    if (target_pos < axes[target_axis].soft_limit_min || target_pos > axes[target_axis].soft_limit_max) {
      logError("[MOTION] Soft limit violation axis %d", target_axis);
      faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, target_axis, target_pos, "Target Limit Violation");
      taskUnlockMutex(taskGetMotionMutex()); return;
    }
  }
  
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
// PLC / HARDWARE HELPERS
// ----------------------------------------------------------------------------

// FIX: Removed 'static' keyword to match header declaration
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction) {
    if (!enable || axis >= MOTION_AXES) {
        elboSetDirection(0, false); 
        elboQ73SetRelay(ELBO_Q73_ENABLE, false); 
        return;
    }
    elboSetDirection(axis, is_plus_direction);
    elboQ73SetRelay(ELBO_Q73_ENABLE, true);
}

// FIX: Removed 'static' keyword
void motionSetPLCSpeedProfile(speed_profile_t profile) {
    elboSetSpeedProfile((uint8_t)profile);
}

// ----------------------------------------------------------------------------
// API WRAPPERS & HELPERS
// ----------------------------------------------------------------------------

void motionStartInternalMove(float x, float y, float z, float a, float speed_mm_s) {
    motionMoveAbsolute(x, y, z, a, speed_mm_s);
}

void motionSetFeedOverride(float factor) { 
    motionPlanner.setFeedOverride(factor); 
}

float motionGetFeedOverride() { 
    return motionPlanner.getFeedOverride(); 
}

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

void motionEnableEncoderFeedback(bool enable) {
  encoder_feedback_enabled = enable;
  encoderMotionEnableFeedback(enable);
}

bool motionIsEncoderFeedbackEnabled() {
  return encoder_feedback_enabled;
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
  
  // Cut Power to all
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

void motionHome(uint8_t axis) {
    if (axis >= MOTION_AXES) return;
    if (!taskLockMutex(taskGetMotionMutex(), 100)) return;
    
    if (active_axis != 255) {
        logError("[HOME] Busy");
        taskUnlockMutex(taskGetMotionMutex());
        return;
    }

    logInfo("[HOME] Start Axis %d...", axis);
    active_axis = axis;
    axes[axis].enabled = true;
    
    // Config for Homing
    motionSetPLCSpeedProfile(SPEED_PROFILE_2); 
    axes[axis].saved_speed_profile = SPEED_PROFILE_2;
    
    // Start Moving NEGATIVE towards switch
    motionSetPLCAxisDirection(255, false, false); 
    motionSetPLCAxisDirection(axis, true, false); 
    
    axes[axis].state = MOTION_HOMING_APPROACH_FAST;
    axes[axis].state_entry_ms = millis();
    
    taskUnlockMutex(taskGetMotionMutex());
}

void motionDiagnostics() {
  Serial.printf("\n[MOTION] Global: %s | Active: %d | Feed: %.0f%%\n", 
      global_enabled ? "ON" : "OFF", active_axis, motionPlanner.getFeedOverride() * 100.0f);
  for (int i = 0; i < MOTION_AXES; i++) {
    Serial.printf("  Axis %d: %s | Pos: %ld | Tgt: %ld | Spd: %.1f\n", 
                  i, motionStateToString(axes[i].state), (long)motionGetPosition(i), (long)axes[i].target_position, axes[i].commanded_speed_mm_s);
  }
}