#include "motion.h"
#include "system_constants.h"
#include "plc_iface.h"
#include "encoder_motion_integration.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "task_manager.h"
#include "safety.h"
#include "encoder_calibration.h" 
#include "hardware_config.h"     
#include "encoder_wj66.h"        
#include <math.h> 
#include <string.h>
#include <stdlib.h> 
#include <stdio.h>

#define MOTION_MED_SPEED_DEFAULT 90.0f 

// Motion axis array
// FIX: Corrected initializer list to match struct (12 fields)
static motion_axis_t axes[MOTION_AXES] = {
  {0, 0, MOTION_IDLE, 0, 0, true, -500000, 500000, true, 0, 0, SPEED_PROFILE_1}, 
  {0, 0, MOTION_IDLE, 0, 0, true, -300000, 300000, true, 0, 0, SPEED_PROFILE_1}, 
  {0, 0, MOTION_IDLE, 0, 0, true, 0, 150000, true, 0, 0, SPEED_PROFILE_1},       
  {0, 0, MOTION_IDLE, 0, 0, true, -45000, 45000, true, 0, 0, SPEED_PROFILE_1}   
};

// Internal motion parameters 
static uint8_t active_axis = 255;
static int32_t active_start_position = 0;
static uint32_t last_update_ms = 0;
static bool global_enabled = true;
static bool encoder_feedback_enabled = false;

// PLC I/O bit map 
const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, 255}; 

// PLC Input (Consensus) bit map
// Mapping: Axis 0(X) -> Q73.1, Axis 1(Y) -> Q73.0, Axis 2(Z) -> Q73.2
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_Q73_CONSENSO_X, ELBO_Q73_CONSENSO_Y, ELBO_Q73_CONSENSO_Z, 255};

void motionInit() {
  Serial.println("[MOTION] Motion system initializing...");
  
  if (MOTION_AXES != 4) {
    faultLogError(FAULT_BOOT_FAILED, "Motion: Invalid axis count");
    return;
  }
  
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].position = 0;
    axes[i].target_position = 0;
    axes[i].state = MOTION_IDLE;
    axes[i].enabled = true;
    axes[i].state_entry_ms = 0;
    axes[i].saved_speed_profile = SPEED_PROFILE_1;
    Serial.print("  [MOTION] Axis ");
    Serial.print(i);
    Serial.println(" initialized");
  }
  
  last_update_ms = millis();
  motionSetPLCAxisDirection(255, false, false);
  Serial.println("[MOTION] ✅ Motion system ready");
}

void motionUpdate() {
  if (!global_enabled) return;
  
  if (!taskLockMutex(taskGetMotionMutex(), 5)) {
    return; 
  }
  
  uint32_t now = millis();
  uint32_t delta_ms = now - last_update_ms;
  
  if (delta_ms < MOTION_UPDATE_INTERVAL_MS) {
    taskUnlockMutex(taskGetMotionMutex());
    return;
  }
  
  last_update_ms = now;
  
  if (active_axis != 255) {
    motion_axis_t* axis = &axes[active_axis];
    int32_t current_pos = wj66GetPosition(active_axis); 
    
    axis->position = current_pos;

    if (!axis->enabled || axis->state == MOTION_ERROR) {
      taskUnlockMutex(taskGetMotionMutex());
      return;
    }
    
    // --- Soft Limit Check ---
    if (axis->soft_limit_enabled) {
      if (current_pos < axis->soft_limit_min || current_pos > axis->soft_limit_max) {
        faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, active_axis, current_pos, 
                      "Axis %d violation: Pos %d out of [%d, %d]", 
                      active_axis, current_pos, axis->soft_limit_min, axis->soft_limit_max);
        motionEmergencyStop(); 
        Serial.print("[MOTION] Axis ");
        Serial.print(active_axis);
        Serial.println(" - Soft limit exceeded, motion halted!");
        taskUnlockMutex(taskGetMotionMutex());
        return;
      }
    }
    
    // --- State Machine Transition ---
    switch (axis->state) {
      case MOTION_IDLE:
        break;

      case MOTION_WAIT_CONSENSO:
        {
            // 1. Check for Timeout
            if (now - axis->state_entry_ms > MOTION_CONSENSO_TIMEOUT_MS) {
                faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, active_axis, 0, 
                              "PLC Consensus Timeout on Axis %d (Wait > 5s)", active_axis);
                logError("[MOTION] Consensus timeout! Aborting move.");
                motionSetPLCAxisDirection(255, false, false); // Kill outputs
                axis->state = MOTION_ERROR;
            } 
            // 2. Check for Consensus Signal
            else {
                uint8_t consenso_bit = AXIS_TO_CONSENSO_BIT[active_axis];
                bool permission_granted = false;

                if (consenso_bit == 255) {
                    // Axis A or undefined typically has no PLC feedback, proceed immediately
                    permission_granted = true;
                } else {
                    // Read Q73 via PLC interface cache
                    if (elboQ73GetConsenso(consenso_bit)) {
                        permission_granted = true;
                    }
                }

                if (permission_granted) {
                    logInfo("[MOTION] Consensus received for Axis %d. Starting move.", active_axis);
                    axis->state = MOTION_EXECUTING;
                    axis->state_entry_ms = now;
                }
            }
        }
        break;

      case MOTION_EXECUTING:
        // Check if target is reached
        if ((active_start_position < axis->target_position && current_pos >= axis->target_position) ||
            (active_start_position > axis->target_position && current_pos <= axis->target_position)) {
          
          axis->position = axis->target_position;
          axis->state = MOTION_STOPPING;         
          axis->state_entry_ms = now;
          
          motionSetPLCAxisDirection(255, false, false); 
          
          axis->position_at_stop = current_pos;

          logInfo("[MOTION] Axis %d reached target %d. Stopping.", active_axis, axis->target_position);
        }
        break;
        
      case MOTION_STOPPING:
        // Simple settle time or wait for encoder to stabilize
        if (abs(current_pos - axis->position_at_stop) < 10) { 
            axis->state = MOTION_IDLE;
            active_axis = 255;
            logInfo("[MOTION] Axis %d motion finalized and idle.", active_axis);
        }
        break;

      case MOTION_PAUSED:
        // Do nothing, wait for resume command
        break;

      case MOTION_ERROR:
        break;
        
      default:
        break;
    }

    axis->last_update_ms = now;
  }
  
  taskUnlockMutex(taskGetMotionMutex());
}

// ============================================================================
// PLC I/O CONTROL IMPLEMENTATION
// ============================================================================

void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction) {
    if (axis >= MOTION_AXES && axis != 255) {
        logError("[MOTION] Invalid axis %d for PLC I/O set", axis);
        return;
    }

    if (enable || axis == 255) {
        // Clear all AXIS bits
        elboI73SetAxis(ELBO_I73_AXIS_X, false); 
        elboI73SetAxis(ELBO_I73_AXIS_Y, false); 
        elboI73SetAxis(ELBO_I73_AXIS_Z, false); 
        // Clear both DIRECTION bits
        elboI73SetDirection(ELBO_I73_DIRECTION_PLUS, false);
        elboI73SetDirection(ELBO_I73_DIRECTION_MINUS, false);
        
        if (axis == 255) return;
    }

    if (enable) {
        uint8_t axis_bit = AXIS_TO_I73_BIT[axis];
        elboI73SetAxis(axis_bit, true);

        if (is_plus_direction) {
            elboI73SetDirection(ELBO_I73_DIRECTION_PLUS, true);
        } else {
            elboI73SetDirection(ELBO_I73_DIRECTION_MINUS, true);
        }
    }
}

// ============================================================================
// MOTION COMMANDS 
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
  // Before calculating targets or setting outputs, ensure we have valid feedback.
  for (int i = 0; i < MOTION_AXES; i++) {
      if (wj66IsStale(i)) {
          logError("[MOTION] ERROR: Move rejected. Encoder axis %d is stale.", i);
          faultLogEntry(FAULT_ERROR, FAULT_ENCODER_TIMEOUT, i, wj66GetAxisAge(i), 
                        "Move rejected: Encoder data stale (Age: %lu ms)", wj66GetAxisAge(i));
          taskUnlockMutex(taskGetMotionMutex());
          return;
      }
  }
  
  // *** CRITICAL SECTION ***
  uint8_t target_axis = 255;
  int32_t target_pos = 0;
  float targets_mm[] = {x, y, z, a};

  // Retrieve scale factors from global machineCal. 
  float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_y = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_z = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_a = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR;
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
  
  // SAFETY: Validate target position against soft limits
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
  float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_y = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_z = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
  float scale_a = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR;

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
      int32_t current_pos = wj66GetPosition(active_axis);
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
  
  Serial.println("[MOTION] ✅ Emergency stop cleared - System ready");
  faultLogWarning(FAULT_EMERGENCY_HALT, "Emergency stop cleared - system recovered");
  
  return true;
}

bool motionIsEmergencyStopped() {
  return !global_enabled;
}

int32_t motionGetPosition(uint8_t axis) {
  if (axis < MOTION_AXES) return wj66GetPosition(axis); 
  return 0;
}

int32_t motionGetTarget(uint8_t axis) {
  if (axis < MOTION_AXES) return axes[axis].target_position;
  return 0;
}

motion_state_t motionGetState(uint8_t axis) {
  if (axis < MOTION_AXES) return axes[axis].state;
  return MOTION_ERROR;
}

bool motionIsMoving() {
  // Moving is defined as Executing OR Waiting for Consensus
  return active_axis != 255 && (axes[active_axis].state == MOTION_EXECUTING || axes[active_axis].state == MOTION_WAIT_CONSENSO);
}

bool motionIsStalled(uint8_t axis) {
  if (axis < MOTION_AXES && axis == active_axis && axes[axis].state == MOTION_EXECUTING) {
    return encoderMotionHasError(axis);
  }
  return false;
}

void motionDiagnostics() {
  Serial.println("\n[MOTION] === Motion System Diagnostics ===");
  Serial.print("Global Enabled: ");
  Serial.println(global_enabled ? "YES" : "NO");
  Serial.print("Active Axis: ");
  Serial.println(active_axis == 255 ? "NONE" : String(active_axis));
  Serial.print("System Moving: ");
  Serial.println(motionIsMoving() ? "YES" : "NO");
  
  for (int i = 0; i < MOTION_AXES; i++) {
    Serial.print("\nAxis ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(motionStateToString(axes[i].state));
    Serial.print(" | Pos: ");
    
    float scale = MOTION_POSITION_SCALE_FACTOR;
    if (i==0 && machineCal.X.pulses_per_mm > 0) scale = machineCal.X.pulses_per_mm;
    else if (i==1 && machineCal.Y.pulses_per_mm > 0) scale = machineCal.Y.pulses_per_mm;
    else if (i==2 && machineCal.Z.pulses_per_mm > 0) scale = machineCal.Z.pulses_per_mm;
    else if (i==3 && machineCal.A.pulses_per_degree > 0) scale = machineCal.A.pulses_per_degree;
    
    Serial.print(motionGetPosition(i) / scale);
    Serial.print(i==3 ? " deg" : " mm");
    Serial.print(" | Target: ");
    Serial.print(axes[i].target_position / scale);
    Serial.print(i==3 ? " deg" : " mm");
    Serial.print(" | Stalled: ");
    Serial.println(motionIsStalled(i) ? "YES" : "NO");
  }
}

void motionPrintStatus() {
  Serial.print("[MOTION] Moving=");
  Serial.println(motionIsMoving() ? "YES" : "NO");
}

void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos) {
  if (axis >= MOTION_AXES) return;
  axes[axis].soft_limit_min = min_pos;
  axes[axis].soft_limit_max = max_pos;
}

void motionEnableSoftLimits(uint8_t axis, bool enable) {
  if (axis < MOTION_AXES) axes[axis].soft_limit_enabled = enable;
}

bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos) {
  if (axis >= MOTION_AXES) return false;
  *min_pos = axes[axis].soft_limit_min;
  *max_pos = axes[axis].soft_limit_max;
  return axes[axis].soft_limit_enabled;
}

uint32_t motionGetLimitViolations(uint8_t axis) {
  if (axis < MOTION_AXES) return axes[axis].limit_violation_count;
  return 0;
}

const char* motionStateToString(motion_state_t state) {
  switch (state) {
    case MOTION_IDLE:           return "IDLE";
    case MOTION_WAIT_CONSENSO:  return "WAIT_CONSENSO";
    case MOTION_EXECUTING:      return "EXECUTING";
    case MOTION_STOPPING:       return "STOPPING";
    case MOTION_PAUSED:         return "PAUSED";
    case MOTION_ERROR:          return "ERROR";
    default:                    return "UNKNOWN";
  }
}

bool motionIsValidStateTransition(uint8_t axis, motion_state_t new_state) {
  if (axis >= MOTION_AXES) return false;
  motion_state_t current = axes[axis].state;
  
  switch (current) {
    case MOTION_IDLE: return (new_state == MOTION_WAIT_CONSENSO || new_state == MOTION_ERROR);
    case MOTION_WAIT_CONSENSO: return (new_state == MOTION_EXECUTING || new_state == MOTION_IDLE || new_state == MOTION_ERROR || new_state == MOTION_PAUSED);
    case MOTION_EXECUTING: return (new_state == MOTION_STOPPING || new_state == MOTION_PAUSED || new_state == MOTION_ERROR);
    case MOTION_STOPPING: return (new_state == MOTION_IDLE || new_state == MOTION_ERROR);
    case MOTION_PAUSED: return (new_state == MOTION_WAIT_CONSENSO || new_state == MOTION_IDLE || new_state == MOTION_ERROR);
    case MOTION_ERROR: return (new_state == MOTION_IDLE);
    default: return false;
  }
}

bool motionSetState(uint8_t axis, motion_state_t new_state) {
  if (axis >= MOTION_AXES) return false;
  if (!taskLockMutex(taskGetMotionMutex(), 100)) return false;
  
  if (!motionIsValidStateTransition(axis, new_state)) {
    taskUnlockMutex(taskGetMotionMutex());
    return false;
  }
  
  axes[axis].state = new_state;
  // If entering a state that starts with waiting, record timestamp
  if (new_state == MOTION_WAIT_CONSENSO || new_state == MOTION_STOPPING) {
      axes[axis].state_entry_ms = millis();
  }
  
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

speed_profile_t motionMapSpeedToProfile(uint8_t axis, float requested_speed_mm_s) {
  float slow_speed_mm_s = machineCal.X.speed_slow_mm_min / 60.0f; 
  float med_speed_mm_s  = machineCal.X.speed_med_mm_min / 60.0f;
  float fast_speed_mm_s = machineCal.X.speed_fast_mm_min / 60.0f;

  if (slow_speed_mm_s < 0.1f) slow_speed_mm_s = 5.0f;   
  if (med_speed_mm_s < 0.1f) med_speed_mm_s = 15.0f;  
  if (fast_speed_mm_s < 0.1f) fast_speed_mm_s = 40.0f; 

  float diff_slow = fabsf(requested_speed_mm_s - slow_speed_mm_s);
  float diff_med  = fabsf(requested_speed_mm_s - med_speed_mm_s);
  float diff_fast = fabsf(requested_speed_mm_s - fast_speed_mm_s);

  float min_diff = min(diff_slow, min(diff_med, diff_fast));

  if (min_diff == diff_slow) return SPEED_PROFILE_1;
  else if (min_diff == diff_med) return SPEED_PROFILE_2;
  else return SPEED_PROFILE_3;
}

void motionSetPLCSpeedProfile(speed_profile_t profile) {
  // CRITICAL SAFETY: Ensure VS Mode is OFF before enabling normal speed profiles.
  elboI73SetVSMode(false);

  uint8_t bit0 = (profile == SPEED_PROFILE_2);
  uint8_t bit1 = (profile == SPEED_PROFILE_3); 
  
  bool fast_ok = elboI72SetSpeed(ELBO_I72_FAST, bit0);
  bool med_ok = elboI72SetSpeed(ELBO_I72_MED, bit1);   

  if (!fast_ok || !med_ok) {
    logError("[MOTION] *** CRITICAL: Speed profile I2C transmission failed! ***");
    faultLogError(FAULT_I2C_ERROR, "PLC speed profile I2C write failed");
    motionEmergencyStop();
    return;
  }
  logInfo("[MOTION] Speed profile: %d - I2C OK", profile);
}

void motionSetVSMode(bool active) {
    if (active) {
        // Enforce exclusivity: Clear speed bits FIRST
        elboI72SetSpeed(ELBO_I72_FAST, false);
        elboI72SetSpeed(ELBO_I72_MED, false);
        
        // Then enable VS
        if (!elboI73SetVSMode(true)) {
             faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, 0, "Failed to enable VS Mode");
        } else {
             logInfo("[MOTION] VS Mode ENABLED (Speed bits cleared)");
        }
    } else {
        // Just disable VS
        if (!elboI73SetVSMode(false)) {
             faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, 0, "Failed to disable VS Mode");
        } else {
             logInfo("[MOTION] VS Mode DISABLED");
        }
    }
}