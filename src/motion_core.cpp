/**
 * @file motion_core.cpp
 * @brief Real-Time Motion Kernel (Gemini v2.4.0)
 * @details Implements Ring Buffer, Look-Ahead, Feed Override, and Safety Logic.
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

// Initialize axes with 0.0f commanded speed
motion_axis_t axes[MOTION_AXES] = {
  {0, 0, MOTION_IDLE, 0, 0, true, -500000, 500000, true, 0, 0, SPEED_PROFILE_1, 0.0f}, // X
  {0, 0, MOTION_IDLE, 0, 0, true, -300000, 300000, true, 0, 0, SPEED_PROFILE_1, 0.0f}, // Y
  {0, 0, MOTION_IDLE, 0, 0, true, 0, 150000, true, 0, 0, SPEED_PROFILE_1, 0.0f},       // Z
  {0, 0, MOTION_IDLE, 0, 0, true, -45000, 45000, true, 0, 0, SPEED_PROFILE_1, 0.0f}    // A
};

uint8_t active_axis = 255; 
int32_t active_start_position = 0;
static uint32_t last_update_ms = 0;
bool global_enabled = true; 
static bool encoder_feedback_enabled = false;

// FEED RATE OVERRIDE (Default 100%)
static float global_feed_override = 1.0f;

const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, 255}; 
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_Q73_CONSENSO_X, ELBO_Q73_CONSENSO_Y, ELBO_Q73_CONSENSO_Z, 255};

// Forward Declaration
extern void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);

// ============================================================================
// INITIALIZATION
// ============================================================================

void motionInit() {
  logInfo("[MOTION] Initializing Core v2.4...");
  
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
  // 1. IDLE BUFFER DRAIN (Start New Move)
  // --------------------------------------------------------------------------
  if (active_axis == 255 && !motionBuffer.isEmpty()) {
      int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
      
      if (buffer_enabled) {
          motion_cmd_t cmd;
          if (motionBuffer.pop(&cmd)) {
              // Unlock mutex before calling API (which re-locks it)
              taskUnlockMutex(taskGetMotionMutex()); 
              motionMoveAbsolute(cmd.x, cmd.y, cmd.z, cmd.a, cmd.speed_mm_s);
              return; // Exit to let next loop handle the state change
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
            if (bit == 255 || elboQ73GetConsenso(bit)) {
                axis->state = MOTION_EXECUTING;
                axis->state_entry_ms = now;
            }
        }
        break;

      case MOTION_EXECUTING:
        {
            // --- LIVE FEED RATE ADJUSTMENT (v2.4) ---
            float effective_speed = axis->commanded_speed_mm_s * global_feed_override;
            speed_profile_t desired_profile = motionMapSpeedToProfile(active_axis, effective_speed);
            
            if (desired_profile != axis->saved_speed_profile) {
                motionSetPLCSpeedProfile(desired_profile);
                axis->saved_speed_profile = desired_profile;
            }
            // --------------------------------

            // --- CONTINUOUS PATH (Same-Axis Look-Ahead) ---
            int buffer_enabled = configGetInt(KEY_MOTION_BUFFER_ENABLE, 0);
            if (buffer_enabled && !motionBuffer.isEmpty()) {
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
                    bool extending = (abs(next_target_counts - axis->target_position) > 10);
                    
                    if (extending) {
                        bool current_dir = (axis->target_position > active_start_position);
                        bool next_dir = (next_target_counts > axis->target_position);
                        
                        if (current_dir == next_dir) {
                            motionBuffer.pop(NULL); 
                            logInfo("[MOTION] Blending: Extend Axis %d -> %ld", active_axis, (long)next_target_counts);
                            
                            active_start_position = axis->target_position; 
                            axis->target_position = next_target_counts;
                            axis->commanded_speed_mm_s = nextCmd.speed_mm_s;
                            
                            taskUnlockMutex(taskGetMotionMutex());
                            return; 
                        }
                    }
                }
            }

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

            // Target Reached Check
            if ((active_start_position < axis->target_position && current_pos >= axis->target_position) ||
                (active_start_position > axis->target_position && current_pos <= axis->target_position)) {
              
              axis->position = axis->target_position;
              axis->state = MOTION_STOPPING;         
              axis->state_entry_ms = now;
              motionSetPLCAxisDirection(255, false, false); // Stop relays
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
  Serial.printf("\n[MOTION] Global: %s | Active: %d | Feed: %.0f%%\n", 
      global_enabled ? "ON" : "OFF", active_axis, global_feed_override * 100.0f);
      
  for (int i = 0; i < MOTION_AXES; i++) {
    Serial.printf("  Axis %d: %s | Pos: %ld | Tgt: %ld | Spd: %.1f\n", 
                  i, 
                  motionStateToString(axes[i].state), 
                  (long)motionGetPosition(i), 
                  (long)axes[i].target_position, 
                  axes[i].commanded_speed_mm_s);
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

// --- NEW: Missing Implementation ---
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

// ============================================================================
// API: FEED RATE OVERRIDE
// ============================================================================

void motionSetFeedOverride(float factor) {
    if (factor < 0.1f) factor = 0.1f; // Minimum 10%
    if (factor > 2.0f) factor = 2.0f; // Maximum 200%
    global_feed_override = factor;
    logInfo("[MOTION] Feed Override set to %.0f%%", factor * 100.0f);
}

float motionGetFeedOverride() {
    return global_feed_override;
}

// ============================================================================
// CRITICAL CONTROL FUNCTIONS
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
      
      // Calculate start speed with current override
      float effective_speed = axes[active_axis].commanded_speed_mm_s * global_feed_override;
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
  // Try to grab mutex, but proceed if failed
  bool got_mutex = taskLockMutex(taskGetMotionMutex(), 10); 
  
  // 1. Cut Power
  motionSetPLCAxisDirection(255, false, false);
  global_enabled = false;
  
  // 2. Set Error State
  for (int i = 0; i < MOTION_AXES; i++) axes[i].state = MOTION_ERROR;
  active_axis = 255;
  
  // 3. SAFETY CRITICAL: Purge Buffer
  // This prevents the machine from resuming stale commands when E-Stop is cleared.
  motionBuffer.clear();
  
  if (got_mutex) taskUnlockMutex(taskGetMotionMutex());
  else logError("[MOTION] E-Stop forced (Mutex timeout)");
  
  logError("[MOTION] [CRITICAL] EMERGENCY STOP - BUFFER PURGED");
  faultLogError(FAULT_EMERGENCY_HALT, "E-Stop Activated");
  
  taskSignalMotionUpdate();
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
  taskSignalMotionUpdate();
  return true;
}