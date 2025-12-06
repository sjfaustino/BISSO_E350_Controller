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
#include "config_unified.h" 
#include "config_keys.h"
#include <math.h> 
#include <string.h>
#include <stdlib.h> 
#include <stdio.h>

motion_axis_t axes[MOTION_AXES] = {
  {0, 0, MOTION_IDLE, 0, 0, true, -500000, 500000, true, 0, 0, SPEED_PROFILE_1}, 
  {0, 0, MOTION_IDLE, 0, 0, true, -300000, 300000, true, 0, 0, SPEED_PROFILE_1}, 
  {0, 0, MOTION_IDLE, 0, 0, true, 0, 150000, true, 0, 0, SPEED_PROFILE_1},       
  {0, 0, MOTION_IDLE, 0, 0, true, -45000, 45000, true, 0, 0, SPEED_PROFILE_1}   
};

uint8_t active_axis = 255; 
int32_t active_start_position = 0;
bool global_enabled = true; 
static bool encoder_feedback_enabled = false;

const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, 255}; 
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_Q73_CONSENSO_X, ELBO_Q73_CONSENSO_Y, ELBO_Q73_CONSENSO_Z, 255};

void motionInit() {
  logInfo("[MOTION] Initializing...");
  if (MOTION_AXES != 4) {
    faultLogError(FAULT_BOOT_FAILED, "Invalid axis count");
    return;
  }
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].state = MOTION_IDLE;
    axes[i].enabled = true;
  }
  motionSetPLCAxisDirection(255, false, false); 
  logInfo("[MOTION] [OK] Ready");
}

void motionUpdate() {
  if (!global_enabled) return;
  if (!taskLockMutex(taskGetMotionMutex(), 0)) return; 
  
  uint32_t now = millis();
  
  if (active_axis != 255) {
    motion_axis_t* axis = &axes[active_axis];
    int32_t current_pos = wj66GetPosition(active_axis); 
    axis->position = current_pos;

    if (!axis->enabled || axis->state == MOTION_ERROR) {
      taskUnlockMutex(taskGetMotionMutex());
      return;
    }
    
    if (axis->soft_limit_enabled) {
      if (current_pos < axis->soft_limit_min || current_pos > axis->soft_limit_max) {
        faultLogEntry(FAULT_WARNING, FAULT_SOFT_LIMIT_EXCEEDED, active_axis, current_pos, "Soft Limit Hit");
        motionEmergencyStop(); 
        taskUnlockMutex(taskGetMotionMutex());
        return;
      }
    }
    
    switch (axis->state) {
      case MOTION_WAIT_CONSENSO:
        if (now - axis->state_entry_ms > MOTION_CONSENSO_TIMEOUT_MS) {
            faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, active_axis, 0, "Consensus Timeout");
            motionSetPLCAxisDirection(255, false, false);
            axis->state = MOTION_ERROR;
        } else {
            uint8_t bit = AXIS_TO_CONSENSO_BIT[active_axis];
            if (bit == 255 || elboQ73GetConsenso(bit)) {
                axis->state = MOTION_EXECUTING;
                axis->state_entry_ms = now;
            }
        }
        break;

      case MOTION_EXECUTING:
        {
            if (active_axis == 0) { // X-Axis Final Approach Logic
                int32_t dist = abs(axis->target_position - current_pos);
                int32_t threshold_counts = 0;
                float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;

                // 1. Get Mode
                int mode = configGetInt(KEY_MOTION_APPROACH_MODE, APPROACH_MODE_FIXED);

                if (mode == APPROACH_MODE_FIXED) {
                    // FIXED MODE: Configured Distance (Default 50mm)
                    int32_t approach_mm = configGetInt(KEY_X_APPROACH, 50);
                    threshold_counts = (int32_t)(approach_mm * scale_x);
                } else {
                    // DYNAMIC MODE: Physics-based calculation
                    float current_speed_mm_s = 0.0f;
                    // Determine current target speed from state (Approximate, as VFD ramps are unknown)
                    if (axis->saved_speed_profile == SPEED_PROFILE_3) 
                        current_speed_mm_s = machineCal.X.speed_fast_mm_min / 60.0f;
                    else if (axis->saved_speed_profile == SPEED_PROFILE_2) 
                        current_speed_mm_s = machineCal.X.speed_med_mm_min / 60.0f;
                    else 
                        current_speed_mm_s = machineCal.X.speed_slow_mm_min / 60.0f;

                    // Get Deceleration (Default 5.0 mm/s^2)
                    float accel = configGetFloat(KEY_DEFAULT_ACCEL, 5.0f);
                    if (accel < 0.1f) accel = 0.1f; // Safety clamp

                    // d = v^2 / (2*a)
                    float stop_dist_mm = (current_speed_mm_s * current_speed_mm_s) / (2.0f * accel);
                    
                    // Add 10% safety margin
                    stop_dist_mm *= 1.1f;
                    
                    threshold_counts = (int32_t)(stop_dist_mm * scale_x);
                }

                // 2. Apply Downshift Logic
                if (dist <= threshold_counts && dist > 100 && axis->saved_speed_profile != SPEED_PROFILE_1) {
                    // Only log if we are actually switching modes
                    if (mode == APPROACH_MODE_DYNAMIC) {
                        logInfo("[MOTION] Dynamic Approach Trigger: %ld counts", threshold_counts);
                    }
                    motionSetPLCSpeedProfile(SPEED_PROFILE_1);
                    axis->saved_speed_profile = SPEED_PROFILE_1;
                }
            }

            // Check if target is reached
            if ((active_start_position < axis->target_position && current_pos >= axis->target_position) ||
                (active_start_position > axis->target_position && current_pos <= axis->target_position)) {
              
              axis->position = axis->target_position;
              axis->state = MOTION_STOPPING;         
              axis->state_entry_ms = now;
              motionSetPLCAxisDirection(255, false, false); 
              axis->position_at_stop = current_pos;
            }
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

int32_t motionGetPosition(uint8_t axis) { return (axis < MOTION_AXES) ? wj66GetPosition(axis) : 0; }
int32_t motionGetTarget(uint8_t axis) { return (axis < MOTION_AXES) ? axes[axis].target_position : 0; }
motion_state_t motionGetState(uint8_t axis) { return (axis < MOTION_AXES) ? axes[axis].state : MOTION_ERROR; }
bool motionIsMoving() { return active_axis != 255 && (axes[active_axis].state == MOTION_EXECUTING || axes[active_axis].state == MOTION_WAIT_CONSENSO); }
bool motionIsStalled(uint8_t axis) { return (axis < MOTION_AXES && axis == active_axis && axes[axis].state == MOTION_EXECUTING) ? encoderMotionHasError(axis) : false; }
bool motionIsEmergencyStopped() { return !global_enabled; }
uint8_t motionGetActiveAxis() { return active_axis; }

void motionDiagnostics() {
  Serial.printf("\n[MOTION] Global: %s | Active: %d\n", global_enabled ? "ON" : "OFF", active_axis);
  for (int i = 0; i < MOTION_AXES; i++) {
    Serial.printf("  Axis %d: %s | Pos: %ld | Tgt: %ld\n", i, motionStateToString(axes[i].state), motionGetPosition(i), axes[i].target_position);
  }
}

const char* motionStateToString(motion_state_t state) {
  switch (state) {
    case MOTION_IDLE: return "IDLE";
    case MOTION_WAIT_CONSENSO: return "WAIT";
    case MOTION_EXECUTING: return "RUN";
    case MOTION_STOPPING: return "STOP";
    case MOTION_PAUSED: return "PAUSE";
    case MOTION_ERROR: return "ERR";
    default: return "UNK";
  }
}

void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos) {
  if (axis >= MOTION_AXES) return;
  axes[axis].soft_limit_min = min_pos;
  axes[axis].soft_limit_max = max_pos;
}

bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos) {
  if (axis >= MOTION_AXES) return false;
  *min_pos = axes[axis].soft_limit_min;
  *max_pos = axes[axis].soft_limit_max;
  return axes[axis].soft_limit_enabled;
}

bool motionIsValidStateTransition(uint8_t axis, motion_state_t new_state) {
  if (axis >= MOTION_AXES) return false;
  return true; 
}

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

bool motionIsEncoderFeedbackEnabled() { return encoder_feedback_enabled; }