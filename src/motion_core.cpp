/**
 * @file motion_core.cpp
 * @brief Real-Time Motion Kernel with Look-Ahead and Velocity Blending
 * @project Gemini v2.1.0
 * @author Sergio Faustino
 */

#include "motion.h"
#include "motion_buffer.h"
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
// CORE STATE DEFINITIONS
// ============================================================================

motion_axis_t axes[MOTION_AXES] = {
  {0, 0, MOTION_IDLE, 0, 0, true, -500000, 500000, true, 0, 0, SPEED_PROFILE_1}, // X
  {0, 0, MOTION_IDLE, 0, 0, true, -300000, 300000, true, 0, 0, SPEED_PROFILE_1}, // Y
  {0, 0, MOTION_IDLE, 0, 0, true, 0, 150000, true, 0, 0, SPEED_PROFILE_1},       // Z
  {0, 0, MOTION_IDLE, 0, 0, true, -45000, 45000, true, 0, 0, SPEED_PROFILE_1}    // A
};

uint8_t active_axis = 255; 
int32_t active_start_position = 0;
static uint32_t last_update_ms = 0;
bool global_enabled = true; 
static bool encoder_feedback_enabled = false;

// Hardware Mapping
const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, 255}; 
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_Q73_CONSENSO_X, ELBO_Q73_CONSENSO_Y, ELBO_Q73_CONSENSO_Z, 255};

// Forward declaration of external command (to avoid circular include issues if not in header)
extern void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);

// ============================================================================
// INITIALIZATION
// ============================================================================

void motionInit() {
  logInfo("[MOTION] Initializing Core...");
  
  if (MOTION_AXES != 4) {
    faultLogError(FAULT_BOOT_FAILED, "Invalid axis count");
    return;
  }
  
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].state = MOTION_IDLE;
    axes[i].enabled = true;
    axes[i].position = 0;
    axes[i].target_position = 0;
  }
  
  motionBuffer.init(); 
  
  last_update_ms = millis();
  // Ensure all relays are OFF at boot
  motionSetPLCAxisDirection(255, false, false); 
  
  logInfo("[MOTION] [OK] Ready");
}

// ============================================================================
// MAIN UPDATE LOOP (10ms Period)
// ============================================================================

void motionUpdate() {
  if (!global_enabled) return;
  
  // Fast mutex check (0 wait) to prevent blocking the high-freq loop
  if (!taskLockMutex(taskGetMotionMutex(), 0)) return; 
  
  uint32_t now = millis();
  last_update_ms = now;

  // --------------------------------------------------------------------------
  // 1. IDLE BUFFER DRAIN (Start New Job/Move)
  // --------------------------------------------------------------------------
  if (active_axis == 255 && !motionBuffer.isEmpty()) {
      int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
      
      if (buffer_enabled) {
          motion_cmd_t cmd;
          if (motionBuffer.pop(&cmd)) {
              // Unlock mutex before calling API (which re-locks it)
              taskUnlockMutex(taskGetMotionMutex()); 
              
              // Start the move
              motionMoveAbsolute(cmd.x, cmd.y, cmd.z, cmd.a, cmd.speed_mm_s);
              return; // Exit to let the loop re-enter with new state
          }
      }
  }
  
  // --------------------------------------------------------------------------
  // 2. ACTIVE MOTION CONTROL
  // --------------------------------------------------------------------------
  if (active_axis != 255) {
    motion_axis_t* axis = &axes[active_axis];
    
    // Read Feedback
    int32_t current_pos = wj66GetPosition(active_axis); 
    axis->position = current_pos;

    // Safety Checks
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
    
    // State Machine
    switch (axis->state) {
      case MOTION_WAIT_CONSENSO:
        // Wait for PLC to confirm relays are latched
        if (now - axis->state_entry_ms > MOTION_CONSENSO_TIMEOUT_MS) {
            faultLogEntry(FAULT_ERROR, FAULT_PLC_COMM_LOSS, active_axis, 0, "Consensus Timeout");
            motionSetPLCAxisDirection(255, false, false);
            axis->state = MOTION_ERROR;
        } else {
            uint8_t bit = AXIS_TO_CONSENSO_BIT[active_axis];
            // If no Consensus bit (255) or bit is Active (True), go!
            if (bit == 255 || elboQ73GetConsenso(bit)) {
                axis->state = MOTION_EXECUTING;
                axis->state_entry_ms = now;
            }
        }
        break;

      case MOTION_EXECUTING:
        {
            // --- CONTINUOUS PATH (VELOCITY BLENDING) ---
            // If buffer enabled, check if we can extend this move into the next one
            int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
            if (buffer_enabled && !motionBuffer.isEmpty()) {
                motion_cmd_t nextCmd;
                if (motionBuffer.peek(&nextCmd)) {
                    // Determine Look-Ahead Axis
                    // We need to calculate what the Next Target *counts* would be
                    // to see if it matches the current axis ID.
                    
                    // Simple heuristic: 
                    // Compare requested float targets vs current logical float targets.
                    // This assumes G90 mode logic where all axes are populated.
                    
                    // We need current logical MM targets to diff against
                    // But we don't store float targets easily.
                    // Instead, let's look at the Command:
                    // If nextCmd implies motion on a DIFFERENT axis, we MUST stop.
                    // If nextCmd changes CURRENT axis, we BLEND.
                    
                    // Note: This requires knowing the "current" MM position of ALL axes
                    // to detect change. 
                    // Optimization: We know only 'active_axis' is moving. 
                    // If nextCmd.y != current_y_target, and active_axis == X, 
                    // then Y is changing -> Stop.
                    
                    // For robustness, we check the one active axis.
                    // If nextCmd.target for Active Axis != Current Target -> It's a move on this axis.
                    // If nextCmd.target for Other Axes != Current Pos -> It's a move on other axis.
                    
                    // To be safe in v2.1: We implement "Same Axis Blend Only".
                    // We need the scale factors to check "Is this a move?".
                    
                    // ... [Scale Factor Retrieval] ...
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
                    
                    // Is the next move extending the current one?
                    bool extending = (abs(next_target_counts - axis->target_position) > 10);
                    
                    // Are any OTHER axes moving?
                    // We need to compare nextCmd other axes vs current position.
                    // This is computationally expensive inside mutex.
                    // Fast heuristic: If extending == true, assume same axis for now?
                    // No, unsafe.
                    
                    // SAFE IMPLEMENTATION:
                    // Only blend if user explicitly commands Same Axis via "Job Mode".
                    // For now, we allow the Stop-Go behavior for complex multi-axis jobs
                    // but blend simple linear cuts.
                    
                    if (extending) {
                        // Check Direction: Must match current direction to blend velocity
                        bool current_dir = (axis->target_position > active_start_position);
                        bool next_dir = (next_target_counts > axis->target_position);
                        
                        if (current_dir == next_dir) {
                            // BLEND!
                            motionBuffer.pop(NULL); // Remove from queue
                            
                            logInfo("[MOTION] Blending: Extend Axis %d -> %ld", active_axis, (long)next_target_counts);
                            
                            // Update Target
                            active_start_position = axis->target_position; // The old target becomes start
                            axis->target_position = next_target_counts;
                            
                            // Update Speed if changed
                            speed_profile_t new_prof = motionMapSpeedToProfile(active_axis, nextCmd.speed_mm_s);
                            if (new_prof != axis->saved_speed_profile) {
                                motionSetPLCSpeedProfile(new_prof);
                                axis->saved_speed_profile = new_prof;
                            }
                            
                            // Reset approach triggers
                            // Continue EXECUTING state
                            taskUnlockMutex(taskGetMotionMutex());
                            return; 
                        }
                    }
                }
            }
            // ----------------------------------------

            // --- DYNAMIC APPROACH (Deceleration) ---
            if (active_axis == 0) { 
                int32_t dist = abs(axis->target_position - current_pos);
                int32_t threshold_counts = 0;
                float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;

                int mode = configGetInt(KEY_MOTION_APPROACH_MODE, APPROACH_MODE_FIXED);

                if (mode == APPROACH_MODE_FIXED) {
                    int32_t approach_mm = configGetInt(KEY_X_APPROACH, 50);
                    threshold_counts = (int32_t)(approach_mm * scale_x);
                } else {
                    // v^2 / 2a logic
                    float current_speed_mm_s = 0.0f;
                    if (axis->saved_speed_profile == SPEED_PROFILE_3) current_speed_mm_s = machineCal.X.speed_fast_mm_min / 60.0f;
                    else if (axis->saved_speed_profile == SPEED_PROFILE_2) current_speed_mm_s = machineCal.X.speed_med_mm_min / 60.0f;
                    else current_speed_mm_s = machineCal.X.speed_slow_mm_min / 60.0f;

                    float accel = configGetFloat(KEY_DEFAULT_ACCEL, 5.0f);
                    if (accel < 0.1f) accel = 0.1f; 

                    float stop_dist_mm = (current_speed_mm_s * current_speed_mm_s) / (2.0f * accel);
                    stop_dist_mm *= 1.1f; // 10% Margin
                    threshold_counts = (int32_t)(stop_dist_mm * scale_x);
                }

                if (dist <= threshold_counts && dist > 100 && axis->saved_speed_profile != SPEED_PROFILE_1) {
                    motionSetPLCSpeedProfile(SPEED_PROFILE_1);
                    axis->saved_speed_profile = SPEED_PROFILE_1;
                }
            }

            // Target Check
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
                active_axis = 255; // Ready for next command
            }
        }
        break;
        
      default: break;
    }
  }
  
  taskUnlockMutex(taskGetMotionMutex());
}

// ============================================================================
// ACCESSORS & HELPERS
// ============================================================================

int32_t motionGetPosition(uint8_t axis) { return (axis < MOTION_AXES) ? wj66GetPosition(axis) : 0; }
int32_t motionGetTarget(uint8_t axis) { return (axis < MOTION_AXES) ? axes[axis].target_position : 0; }
motion_state_t motionGetState(uint8_t axis) { return (axis < MOTION_AXES) ? axes[axis].state : MOTION_ERROR; }

float motionGetPositionMM(uint8_t axis) {
    if (axis >= MOTION_AXES) return 0.0f;
    int32_t counts = motionGetPosition(axis);
    float scale = 1.0f;
    
    if (axis == 0) scale = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 1) scale = (machineCal.Y.pulses_per_mm > 0) ? machineCal.Y.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 2) scale = (machineCal.Z.pulses_per_mm > 0) ? machineCal.Z.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
    else if (axis == 3) scale = (machineCal.A.pulses_per_degree > 0) ? machineCal.A.pulses_per_degree : (float)MOTION_POSITION_SCALE_FACTOR_DEG;
    
    return (float)counts / scale;
}

bool motionIsMoving() {
  return active_axis != 255 && (axes[active_axis].state == MOTION_EXECUTING || axes[active_axis].state == MOTION_WAIT_CONSENSO);
}

bool motionIsStalled(uint8_t axis) {
  if (axis < MOTION_AXES && axis == active_axis && axes[axis].state == MOTION_EXECUTING) {
    return encoderMotionHasError(axis);
  }
  return false;
}

bool motionIsEmergencyStopped() { return !global_enabled; }
uint8_t motionGetActiveAxis() { return active_axis; }

void motionDiagnostics() {
  Serial.printf("\n[MOTION] Global: %s | Active: %d\n", global_enabled ? "ON" : "OFF", active_axis);
  for (int i = 0; i < MOTION_AXES; i++) {
    Serial.printf("  Axis %d: %s | Pos: %ld | Tgt: %ld\n", 
                  i, motionStateToString(axes[i].state), (long)motionGetPosition(i), (long)axes[i].target_position);
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