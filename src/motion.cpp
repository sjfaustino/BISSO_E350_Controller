#include "motion.h"
#include "encoder_motion_integration.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "task_manager.h"
#include "safety.h"

// Motion axis array
static motion_axis_t axes[MOTION_AXES] = {
  {0, 0, 0.0f, 0.0f, MOTION_ACCELERATION, MOTION_IDLE, 0, false, true, -500000, 500000, true, 0},  // X-axis: -500mm to +500mm
  {0, 0, 0.0f, 0.0f, MOTION_ACCELERATION, MOTION_IDLE, 0, false, true, -300000, 300000, true, 0},  // Y-axis: -300mm to +300mm
  {0, 0, 0.0f, 0.0f, MOTION_ACCELERATION, MOTION_IDLE, 0, false, true, 0, 150000, true, 0},       // Z-axis: 0mm to +150mm
  {0, 0, 0.0f, 0.0f, MOTION_ACCELERATION, MOTION_IDLE, 0, false, true, -45000, 45000, true, 0}   // A-axis: -45° to +45°
};

static uint32_t last_update_ms = 0;
static bool global_enabled = true;
static bool encoder_feedback_enabled = false;

void motionInit() {
  Serial.println("[MOTION] Motion system initializing...");
  
  // Validate number of axes
  if (MOTION_AXES != 4) {
    Serial.println("[MOTION] ERROR: Invalid axis count - expected 4");
    faultLogError(FAULT_BOOT_FAILED, "Motion: Invalid axis count");
    return;
  }
  
  for (int i = 0; i < MOTION_AXES; i++) {
    if (&axes[i] == NULL) {
      Serial.print("[MOTION] ERROR: Null axis pointer at index ");
      Serial.println(i);
      faultLogError(FAULT_BOOT_FAILED, "Motion: Null axis pointer");
      return;
    }
    
    axes[i].position = 0;
    axes[i].target_position = 0;
    axes[i].current_speed = 0.0f;
    axes[i].target_speed = 0.0f;
    axes[i].state = MOTION_IDLE;
    axes[i].enabled = true;
    axes[i].homed = false;
    Serial.print("  [MOTION] Axis ");
    Serial.print(i);
    Serial.println(" initialized");
  }
  
  last_update_ms = millis();
  Serial.println("[MOTION] ✅ Motion system ready");
}

void motionUpdate() {
  if (!global_enabled) return;
  
  // Acquire mutex with 5ms timeout
  if (!taskLockMutex(taskGetMotionMutex(), 5)) {
    return; // Couldn't acquire lock, skip this cycle
  }
  
  uint32_t now = millis();
  uint32_t delta_ms = now - last_update_ms;
  
  if (delta_ms < MOTION_UPDATE_INTERVAL_MS) {
    taskUnlockMutex(taskGetMotionMutex());
    return;
  }
  
  last_update_ms = now;
  float delta_s = delta_ms / 1000.0f;
  
  for (int i = 0; i < MOTION_AXES; i++) {
    motion_axis_t* axis = &axes[i];
    
    if (!axis->enabled || axis->state == MOTION_ERROR) {
      continue;
    }
    
    int32_t distance_remaining = axis->target_position - axis->position;
    
    if (distance_remaining == 0 && axis->current_speed == 0) {
      axis->state = MOTION_IDLE;
      axis->target_speed = 0.0f;
      continue;
    }
    
    // Acceleration/deceleration logic
    if (axis->target_speed > axis->current_speed) {
      axis->current_speed += axis->acceleration * delta_s;
      if (axis->current_speed > axis->target_speed) {
        axis->current_speed = axis->target_speed;
        axis->state = MOTION_CONSTANT_SPEED;
      } else {
        axis->state = MOTION_ACCELERATING;
      }
    } else if (axis->target_speed < axis->current_speed) {
      axis->current_speed -= axis->acceleration * delta_s;
      if (axis->current_speed < axis->target_speed) {
        axis->current_speed = axis->target_speed;
      }
      axis->state = MOTION_DECELERATING;
    }
    
    // Update position
    int32_t move_distance = (int32_t)(axis->current_speed * delta_s);
    if (move_distance != 0) {
      // Calculate new position
      int32_t new_position = axis->position + move_distance;
      
      // Enforce soft limits
      if (axis->soft_limit_enabled) {
        if (new_position < axis->soft_limit_min) {
          // Hit minimum limit
          axis->position = axis->soft_limit_min;
          axis->current_speed = 0.0f;
          axis->target_speed = 0.0f;
          axis->limit_violation_count++;
          axis->state = MOTION_ERROR;
          
          Serial.print("[MOTION] Axis ");
          Serial.print(i);
          Serial.print(" - Soft limit MIN exceeded (");
          Serial.print(axis->position / 1000.0f);
          Serial.println(" mm)");
          
          faultLogWarning(FAULT_WATCHDOG_TIMEOUT, "Soft limit minimum hit");
          continue;
        } else if (new_position > axis->soft_limit_max) {
          // Hit maximum limit
          axis->position = axis->soft_limit_max;
          axis->current_speed = 0.0f;
          axis->target_speed = 0.0f;
          axis->limit_violation_count++;
          axis->state = MOTION_ERROR;
          
          Serial.print("[MOTION] Axis ");
          Serial.print(i);
          Serial.print(" - Soft limit MAX exceeded (");
          Serial.print(axis->position / 1000.0f);
          Serial.println(" mm)");
          
          faultLogWarning(FAULT_WATCHDOG_TIMEOUT, "Soft limit maximum hit");
          continue;
        }
      }
      
      // Update position (safe within limits)
      axis->position = new_position;
      
      // Check if target reached
      if ((distance_remaining > 0 && axis->position >= axis->target_position) ||
          (distance_remaining < 0 && axis->position <= axis->target_position)) {
        axis->position = axis->target_position;
        axis->current_speed = 0.0f;
        axis->target_speed = 0.0f;
        axis->state = MOTION_IDLE;
      }
    }
    
    axis->last_update_ms = now;
  }
  
  // Release mutex
  taskUnlockMutex(taskGetMotionMutex());
}

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
  if (!global_enabled) {
    Serial.println("[MOTION] ERROR: Motion system disabled");
    return;
  }
  
  // Acquire mutex with 100ms timeout for CLI/web commands
  if (!taskLockMutex(taskGetMotionMutex(), 100)) {
    Serial.println("[MOTION] ERROR: Could not acquire motion mutex");
    return;
  }
  
  axes[0].target_position = (int32_t)(x * 1000);
  axes[1].target_position = (int32_t)(y * 1000);
  axes[2].target_position = (int32_t)(z * 1000);
  axes[3].target_position = (int32_t)(a * 1000);
  
  float clamped_speed = constrain(speed_mm_s, 0.1f, MOTION_MAX_SPEED);
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].target_speed = clamped_speed;
    axes[i].state = MOTION_ACCELERATING;
  }
  
  Serial.print("[MOTION] Moving to (");
  Serial.print(x); Serial.print(", ");
  Serial.print(y); Serial.print(", ");
  Serial.print(z); Serial.print(", ");
  Serial.print(a);
  Serial.print(") at ");
  Serial.print(clamped_speed);
  Serial.println(" mm/s");
  
  // Release mutex
  taskUnlockMutex(taskGetMotionMutex());
}

void motionMoveRelative(float dx, float dy, float dz, float da, float speed_mm_s) {
  motionMoveAbsolute(
    axes[0].position / 1000.0f + dx,
    axes[1].position / 1000.0f + dy,
    axes[2].position / 1000.0f + dz,
    axes[3].position / 1000.0f + da,
    speed_mm_s
  );
}

void motionSetAxisTarget(uint8_t axis, int32_t target_pos) {
  if (axis < MOTION_AXES) {
    axes[axis].target_position = target_pos;
    axes[axis].target_speed = MOTION_MAX_SPEED;
    axes[axis].state = MOTION_ACCELERATING;
  }
}

void motionStop() {
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].target_speed = 0.0f;
    axes[i].state = MOTION_IDLE;
  }
  Serial.println("[MOTION] Stop commanded");
}

void motionPause() {
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].state != MOTION_IDLE) {
      axes[i].state = MOTION_PAUSED;
    }
  }
  Serial.println("[MOTION] Paused");
}

void motionResume() {
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].state == MOTION_PAUSED) {
      axes[i].state = MOTION_ACCELERATING;
    }
  }
  Serial.println("[MOTION] Resumed");
}

void motionEmergencyStop() {
  global_enabled = false;
  
  // Stop all axes immediately
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].target_speed = 0.0f;
    axes[i].current_speed = 0.0f;
    axes[i].state = MOTION_ERROR;
  }
  
  Serial.println("[MOTION] ⚠️  EMERGENCY STOP ACTIVATED ⚠️");
  Serial.println("[MOTION] All axes stopped - Manual recovery required");
  
  // Log critical fault
  faultLogError(FAULT_EMERGENCY_HALT, "Emergency stop activated - motion halted");
}

bool motionClearEmergencyStop() {
  // Check safety conditions before clearing E-stop
  if (global_enabled == true) {
    Serial.println("[MOTION] INFO: Emergency stop already cleared");
    return true;
  }
  
  // Verify safety system is not alarmed
  if (safetyIsAlarmed()) {
    Serial.println("[MOTION] ERROR: Cannot clear E-stop - safety system alarmed");
    faultLogWarning(FAULT_EMERGENCY_HALT, "E-stop clear attempted with safety alarm active");
    return false;
  }
  
  // Verify all axes are stopped
  bool all_stopped = true;
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].current_speed != 0.0f || axes[i].target_speed != 0.0f) {
      all_stopped = false;
      break;
    }
  }
  
  if (!all_stopped) {
    Serial.println("[MOTION] ERROR: Cannot clear E-stop - axes still moving");
    faultLogWarning(FAULT_EMERGENCY_HALT, "E-stop clear attempted with moving axes");
    return false;
  }
  
  // Safe to clear emergency stop
  global_enabled = true;
  
  // Reset all axes to idle
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].state == MOTION_ERROR) {
      axes[i].state = MOTION_IDLE;
      axes[i].target_speed = 0.0f;
      axes[i].current_speed = 0.0f;
    }
  }
  
  Serial.println("[MOTION] ✅ Emergency stop cleared - System ready");
  faultLogWarning(FAULT_EMERGENCY_HALT, "Emergency stop cleared - system recovered");
  
  return true;
}

bool motionIsEmergencyStopped() {
  return !global_enabled;
}

int32_t motionGetPosition(uint8_t axis) {
  if (axis < MOTION_AXES) return axes[axis].position;
  return 0;
}

int32_t motionGetTarget(uint8_t axis) {
  if (axis < MOTION_AXES) return axes[axis].target_position;
  return 0;
}

float motionGetSpeed(uint8_t axis) {
  if (axis < MOTION_AXES) return axes[axis].current_speed;
  return 0.0f;
}

motion_state_t motionGetState(uint8_t axis) {
  if (axis < MOTION_AXES) return axes[axis].state;
  return MOTION_ERROR;
}

bool motionIsMoving() {
  for (int i = 0; i < MOTION_AXES; i++) {
    if (axes[i].state != MOTION_IDLE && axes[i].state != MOTION_ERROR) {
      return true;
    }
  }
  return false;
}

bool motionIsStalled(uint8_t axis) {
  if (axis < MOTION_AXES) {
    uint32_t time_since_update = millis() - axes[axis].last_update_ms;
    return time_since_update > MOTION_STALL_TIMEOUT_MS;
  }
  return false;
}

void motionDiagnostics() {
  Serial.println("\n[MOTION] === Motion System Diagnostics ===");
  Serial.print("Global Enabled: ");
  Serial.println(global_enabled ? "YES" : "NO");
  Serial.print("System Moving: ");
  Serial.println(motionIsMoving() ? "YES" : "NO");
  
  for (int i = 0; i < MOTION_AXES; i++) {
    Serial.print("\nAxis ");
    Serial.print(i);
    Serial.print(": ");
    switch(axes[i].state) {
      case MOTION_IDLE: Serial.print("IDLE"); break;
      case MOTION_ACCELERATING: Serial.print("ACCEL"); break;
      case MOTION_CONSTANT_SPEED: Serial.print("CONST"); break;
      case MOTION_DECELERATING: Serial.print("DECEL"); break;
      case MOTION_PAUSED: Serial.print("PAUSED"); break;
      case MOTION_ERROR: Serial.print("ERROR"); break;
      default: Serial.print("UNKNOWN");
    }
    Serial.print(" | Pos: ");
    Serial.print(axes[i].position / 1000.0f);
    Serial.print(" mm | Speed: ");
    Serial.print(axes[i].current_speed);
    Serial.print(" mm/s | Stalled: ");
    Serial.println(motionIsStalled(i) ? "YES" : "NO");
  }
}

void motionPrintStatus() {
  Serial.print("[MOTION] X=");
  Serial.print(axes[0].position / 1000.0f);
  Serial.print(" Y=");
  Serial.print(axes[1].position / 1000.0f);
  Serial.print(" Z=");
  Serial.print(axes[2].position / 1000.0f);
  Serial.print(" A=");
  Serial.print(axes[3].position / 1000.0f);
  Serial.print(" Moving=");
  Serial.println(motionIsMoving() ? "YES" : "NO");
}

// ============================================================================
// SOFT LIMIT MANAGEMENT
// ============================================================================

void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos) {
  if (axis >= MOTION_AXES) {
    Serial.println("[MOTION] ERROR: Invalid axis for soft limits");
    return;
  }
  
  if (min_pos >= max_pos) {
    Serial.println("[MOTION] ERROR: Invalid limit range (min >= max)");
    return;
  }
  
  axes[axis].soft_limit_min = min_pos;
  axes[axis].soft_limit_max = max_pos;
  
  Serial.print("[MOTION] Axis ");
  Serial.print(axis);
  Serial.print(" soft limits set: ");
  Serial.print(min_pos / 1000.0f);
  Serial.print(" to ");
  Serial.print(max_pos / 1000.0f);
  Serial.println(" mm");
}

void motionEnableSoftLimits(uint8_t axis, bool enable) {
  if (axis < MOTION_AXES) {
    axes[axis].soft_limit_enabled = enable;
    Serial.print("[MOTION] Axis ");
    Serial.print(axis);
    Serial.print(" soft limits ");
    Serial.println(enable ? "ENABLED" : "DISABLED");
  }
}

bool motionGetSoftLimits(uint8_t axis, int32_t* min_pos, int32_t* max_pos) {
  if (axis >= MOTION_AXES || !min_pos || !max_pos) {
    return false;
  }
  
  *min_pos = axes[axis].soft_limit_min;
  *max_pos = axes[axis].soft_limit_max;
  return axes[axis].soft_limit_enabled;
}

uint32_t motionGetLimitViolations(uint8_t axis) {
  if (axis < MOTION_AXES) {
    return axes[axis].limit_violation_count;
  }
  return 0;
}

// ============================================================================
// STATE MACHINE VALIDATION
// ============================================================================

const char* motionStateToString(motion_state_t state) {
  switch (state) {
    case MOTION_IDLE:           return "IDLE";
    case MOTION_ACCELERATING:   return "ACCELERATING";
    case MOTION_CONSTANT_SPEED: return "CONSTANT_SPEED";
    case MOTION_DECELERATING:   return "DECELERATING";
    case MOTION_PAUSED:         return "PAUSED";
    case MOTION_ERROR:          return "ERROR";
    default:                    return "UNKNOWN";
  }
}

bool motionIsValidStateTransition(uint8_t axis, motion_state_t new_state) {
  if (axis >= MOTION_AXES) {
    return false;
  }
  
  motion_state_t current = axes[axis].state;
  
  // State machine validation logic
  switch (current) {
    case MOTION_IDLE:
      // From IDLE, can go to: ACCELERATING (start motion) or ERROR
      return (new_state == MOTION_ACCELERATING || new_state == MOTION_ERROR);
    
    case MOTION_ACCELERATING:
      // From ACCELERATING, can go to: CONSTANT_SPEED, DECELERATING, PAUSED, ERROR
      return (new_state == MOTION_CONSTANT_SPEED || 
              new_state == MOTION_DECELERATING || 
              new_state == MOTION_PAUSED || 
              new_state == MOTION_ERROR ||
              new_state == MOTION_IDLE);
    
    case MOTION_CONSTANT_SPEED:
      // From CONSTANT_SPEED, can go to: DECELERATING, PAUSED, ERROR, IDLE
      return (new_state == MOTION_DECELERATING || 
              new_state == MOTION_PAUSED || 
              new_state == MOTION_ERROR ||
              new_state == MOTION_IDLE);
    
    case MOTION_DECELERATING:
      // From DECELERATING, can only go to: IDLE or ERROR (motion ending)
      return (new_state == MOTION_IDLE || new_state == MOTION_ERROR);
    
    case MOTION_PAUSED:
      // From PAUSED, can go to: ACCELERATING (resume), IDLE (stop), or ERROR
      return (new_state == MOTION_ACCELERATING || 
              new_state == MOTION_IDLE || 
              new_state == MOTION_ERROR);
    
    case MOTION_ERROR:
      // From ERROR, can only go to: IDLE (after recovery/reset)
      return (new_state == MOTION_IDLE);
    
    default:
      return false;
  }
}

bool motionSetState(uint8_t axis, motion_state_t new_state) {
  if (axis >= MOTION_AXES) {
    Serial.println("[MOTION] ERROR: Invalid axis for state change");
    return false;
  }
  
  motion_state_t current = axes[axis].state;
  
  // Validate transition
  if (!motionIsValidStateTransition(axis, new_state)) {
    Serial.print("[MOTION] ERROR: Invalid state transition on axis ");
    Serial.print(axis);
    Serial.print(" from ");
    Serial.print(motionStateToString(current));
    Serial.print(" to ");
    Serial.println(motionStateToString(new_state));
    
    faultLogWarning(FAULT_WATCHDOG_TIMEOUT, "Invalid state transition");
    return false;
  }
  
  // Log state change
  Serial.print("[MOTION] Axis ");
  Serial.print(axis);
  Serial.print(" state: ");
  Serial.print(motionStateToString(current));
  Serial.print(" -> ");
  Serial.println(motionStateToString(new_state));
  
  // Update state
  axes[axis].state = new_state;
  
  // Reset speeds on specific transitions
  if (new_state == MOTION_IDLE || new_state == MOTION_ERROR) {
    axes[axis].current_speed = 0.0f;
    axes[axis].target_speed = 0.0f;
  }
  
  return true;
}

// ============================================================================
// ENCODER FEEDBACK CONTROL
// ============================================================================

void motionEnableEncoderFeedback(bool enable) {
  encoder_feedback_enabled = enable;
  encoderMotionEnableFeedback(enable);
}

bool motionIsEncoderFeedbackEnabled() {
  return encoder_feedback_enabled;
}

