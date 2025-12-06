/**
 * @file motion_control.cpp
 * @brief Real-Time Hardware Execution Layer (Gemini v3.1.0)
 * @details Handles the high-priority control loop, hardware I/O, and safety.
 * Owner of the 'axes' array.
 * @project Gemini v3.1.0
 * @author Sergio Faustino
 */

#include "motion.h"
#include "motion_state.h" // Links to Accessors
#include "motion_planner.h" // Links to the Brain
#include "system_constants.h"
#include "plc_iface.h"
#include "encoder_motion_integration.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "task_manager.h"
#include "safety.h"
#include "encoder_wj66.h"        
#include "config_unified.h" 
#include "config_keys.h"
#include <math.h> 
#include <string.h>
#include <stdlib.h> 
#include <stdio.h>

// ============================================================================
// CORE STATE DEFINITIONS (Owner)
// ============================================================================

motion_axis_t axes[MOTION_AXES] = {
  {0, 0, MOTION_IDLE, 0, 0, true, -500000, 500000, true, 0, 0, SPEED_PROFILE_1, 0.0f}, // X
  {0, 0, MOTION_IDLE, 0, 0, true, -300000, 300000, true, 0, 0, SPEED_PROFILE_1, 0.0f}, // Y
  {0, 0, MOTION_IDLE, 0, 0, true, 0, 150000, true, 0, 0, SPEED_PROFILE_1, 0.0f},       // Z
  {0, 0, MOTION_IDLE, 0, 0, true, -45000, 45000, true, 0, 0, SPEED_PROFILE_1, 0.0f}    // A
};

uint8_t active_axis = 255; 
int32_t active_start_position = 0;
bool global_enabled = true; 
static bool encoder_feedback_enabled = false; 

const uint8_t AXIS_TO_I73_BIT[] = {ELBO_I73_AXIS_X, ELBO_I73_AXIS_Y, ELBO_I73_AXIS_Z, 255}; 
const uint8_t AXIS_TO_CONSENSO_BIT[] = {ELBO_Q73_CONSENSO_X, ELBO_Q73_CONSENSO_Y, ELBO_Q73_CONSENSO_Z, 255};

// Forward declaration for motion_commands.cpp to use
extern void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);

// ============================================================================
// INITIALIZATION
// ============================================================================

void motionInit() {
  logInfo("[MOTION] Initializing Control Layer v3.1...");
  
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
  
  // 1. INPUTS
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

// ----------------------------------------------------------------------------
// CONTROL & SAFETY API (Modifiers)
// ----------------------------------------------------------------------------

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
      // Use helper from Planner
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
  motionBuffer.clear(); // Safety: Purge
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

// ----------------------------------------------------------------------------
// API WRAPPERS (Fixes for Linker Errors)
// ----------------------------------------------------------------------------

void motionSetFeedOverride(float factor) { 
    motionPlanner.setFeedOverride(factor); 
}

float motionGetFeedOverride() { 
    return motionPlanner.getFeedOverride(); 
}

void motionStartInternalMove(float x, float y, float z, float a, float speed_mm_s) {
    // This is called by Planner to execute a popped command
    motionMoveAbsolute(x, y, z, a, speed_mm_s);
}

void motionDiagnostics() {
  Serial.printf("\n[MOTION] Global: %s | Active: %d | Feed: %.0f%%\n", 
      global_enabled ? "ON" : "OFF", active_axis, motionPlanner.getFeedOverride() * 100.0f);
  for (int i = 0; i < MOTION_AXES; i++) {
    Serial.printf("  Axis %d: %s | Pos: %ld | Tgt: %ld | Spd: %.1f\n", 
                  i, motionStateToString(axes[i].state), (long)motionGetPosition(i), (long)axes[i].target_position, axes[i].commanded_speed_mm_s);
  }
}

// --- Internal Configuration ---
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