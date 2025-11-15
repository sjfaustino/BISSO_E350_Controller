#include "motion.h"

// Motion axis array
static motion_axis_t axes[MOTION_AXES] = {
  {0, 0, 0.0f, 0.0f, MOTION_ACCELERATION, MOTION_IDLE, 0, false, true},
  {0, 0, 0.0f, 0.0f, MOTION_ACCELERATION, MOTION_IDLE, 0, false, true},
  {0, 0, 0.0f, 0.0f, MOTION_ACCELERATION, MOTION_IDLE, 0, false, true},
  {0, 0, 0.0f, 0.0f, MOTION_ACCELERATION, MOTION_IDLE, 0, false, true}
};

static uint32_t last_update_ms = 0;
static bool global_enabled = true;

void motionInit() {
  Serial.println("[MOTION] Motion system initializing...");
  for (int i = 0; i < MOTION_AXES; i++) {
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
  Serial.println("[MOTION] Motion system ready");
}

void motionUpdate() {
  if (!global_enabled) return;
  
  uint32_t now = millis();
  uint32_t delta_ms = now - last_update_ms;
  
  if (delta_ms < MOTION_UPDATE_INTERVAL_MS) {
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
      axis->position += move_distance;
      
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
}

void motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s) {
  if (!global_enabled) {
    Serial.println("[MOTION] ERROR: Motion system disabled");
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
  for (int i = 0; i < MOTION_AXES; i++) {
    axes[i].target_speed = 0.0f;
    axes[i].current_speed = 0.0f;
    axes[i].state = MOTION_ERROR;
  }
  Serial.println("[MOTION] EMERGENCY STOP ACTIVATED");
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
