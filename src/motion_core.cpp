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
#include "config_unified.h" // Required for configGetInt
#include <math.h> 
#include <string.h>
#include <stdlib.h> 
#include <stdio.h>

// ============================================================================
// CORE STATE DEFINITIONS (SINGLE DEFINITION POINT)
// ============================================================================

// Motion axis array
motion_axis_t axes[MOTION_AXES] = {
  {0, 0, MOTION_IDLE, 0, 0, true, -500000, 500000, true, 0, 0, SPEED_PROFILE_1}, 
  {0, 0, MOTION_IDLE, 0, 0, true, -300000, 300000, true, 0, 0, SPEED_PROFILE_1}, 
  {0, 0, MOTION_IDLE, 0, 0, true, 0, 150000, true, 0, 0, SPEED_PROFILE_1},       
  {0, 0, MOTION_IDLE, 0, 0, true, -45000, 45000, true, 0, 0, SPEED_PROFILE_1}   
};

// Internal motion parameters 
uint8_t active_axis = 255; // ID of the currently active axis, 255 if none
int32_t active_start_position = 0;
static uint32_t last_update_ms = 0;
bool global_enabled = true; // Overall system enable flag
static bool encoder_feedback_enabled = false;

// PLC I/O bit map (Used by motion_plc_io.cpp via extern)
const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, 255}; 

// PLC Input (Consensus) bit map (Used internally and by motion_commands.cpp)
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_Q73_CONSENSO_X, ELBO_Q73_CONSENSO_Y, ELBO_Q73_CONSENSO_Z, 255};


// ============================================================================
// PUBLIC/CORE API FUNCTIONS (Called by Task Manager)
// ============================================================================

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
  // Call to motion_plc_io.cpp function:
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
      case MOTION_PAUSED: 
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
                    permission_granted = true; // No consensus required (Axis A)
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
        {
            // --- X-AXIS FINAL APPROACH LOGIC ---
            if (active_axis == 0) { // Axis 0 = X
                int32_t dist_to_target = abs(axis->target_position - current_pos);
                
                // 1. Get Configured Approach Distance (Default 50mm if not set)
                int32_t approach_mm = configGetInt("x_approach_mm", 50);
                
                // 2. Get Calibrated Scale Factor for X
                float scale_x = (machineCal.X.pulses_per_mm > 0) ? machineCal.X.pulses_per_mm : (float)MOTION_POSITION_SCALE_FACTOR;
                
                // 3. Convert mm to Encoder Counts
                int32_t approach_counts = (int32_t)(approach_mm * scale_x);
                
                // 4. Check if we are in the zone, outside deadband, and not already slow
                if (dist_to_target <= approach_counts && 
                    dist_to_target > 100 && // Deadband (prevent jitter near target)
                    axis->saved_speed_profile != SPEED_PROFILE_1) {
                    
                    logInfo("[MOTION] Axis X entering Final Approach (Dist: %ld counts). Downshifting to SLOW.", dist_to_target);
                    
                    // Apply Slow Profile immediately
                    motionSetPLCSpeedProfile(SPEED_PROFILE_1);
                    
                    // Update state so we don't spam I2C writes
                    axis->saved_speed_profile = SPEED_PROFILE_1;
                }
            }
            // ----------------------------------------

            // Check if target is reached
            if ((active_start_position < axis->target_position && current_pos >= axis->target_position) ||
                (active_start_position > axis->target_position && current_pos <= axis->target_position)) {
              
              axis->position = axis->target_position;
              axis->state = MOTION_STOPPING;         
              axis->state_entry_ms = now;
              
              motionSetPLCAxisDirection(255, false, false); // Stop movement
              
              axis->position_at_stop = current_pos;

              logInfo("[MOTION] Axis %d reached target %d. Stopping.", active_axis, axis->target_position);
            }
        }
        break;
        
      case MOTION_STOPPING:
        {
            // FIX: Use Configurable Deadband for Settling
            // Retrieve from NVS, default to 10 counts if not set.
            int32_t settle_deadband = configGetInt("motion_settle_deadband", 10);

            // Wait for encoder to settle within deadband
            if (abs(current_pos - axis->position_at_stop) < settle_deadband) { 
                axis->state = MOTION_IDLE;
                active_axis = 255;
                logInfo("[MOTION] Axis %d motion finalized (Settled within %d counts).", active_axis, settle_deadband);
            }
        }
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

// --- MOTION QUERY GETTERS ---
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

bool motionIsEmergencyStopped() {
  return !global_enabled;
}

// --- Active Axis Getter ---
uint8_t motionGetActiveAxis() {
  return active_axis;
}

// --- DIAGNOSTICS & STATUS ---

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
    
    // Scale factor for diagnostics
    float scale = MOTION_POSITION_SCALE_FACTOR;
    if (i==0 && machineCal.X.pulses_per_mm > 0) scale = machineCal.X.pulses_per_mm;
    else if (i==1 && machineCal.Y.pulses_per_mm > 0) scale = machineCal.Y.pulses_per_mm;
    else if (i==2 && machineCal.Z.pulses_per_mm > 0) scale = machineCal.Z.pulses_per_mm;
    else if (i==3) {
      if (machineCal.A.pulses_per_degree > 0) scale = machineCal.A.pulses_per_degree;
      else scale = MOTION_POSITION_SCALE_FACTOR_DEG;
    }
    
    Serial.print(motionGetPosition(i) / scale);
    Serial.print(i==3 ? " deg" : " mm");
    Serial.print(" | Target: ");
    Serial.print(axes[i].target_position / scale);
    Serial.print(i==3 ? " deg" : " mm");
    Serial.print(" | Stalled: ");
    Serial.println(motionIsStalled(i) ? "YES" : "NO");
  }
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

// --- LIMITS/CONFIG (Setters/Getters) ---

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

// --- STATE MACHINE VALIDATION ---

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

// --- ENCODER FEEDBACK ---

void motionEnableEncoderFeedback(bool enable) {
  encoder_feedback_enabled = enable;
  encoderMotionEnableFeedback(enable);
}

bool motionIsEncoderFeedbackEnabled() {
  return encoder_feedback_enabled;
}