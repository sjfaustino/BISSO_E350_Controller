#include "encoder_motion_integration.h"
#include "safety.h"
#include "fault_logging.h"
#include "serial_logger.h"

// ============================================================================
// ENCODER-MOTION INTEGRATION IMPLEMENTATION
// ============================================================================

static position_error_t position_errors[4] = {
  {0, 0, 100000, 0, 2000, false, 0},
  {0, 0, 100000, 0, 2000, false, 0},
  {0, 0, 100000, 0, 2000, false, 0},
  {0, 0, 100000, 0, 2000, false, 0}
};

static bool encoder_feedback_enabled = false;
static int32_t encoder_error_threshold = 100000;  // Default: 100mm error threshold
static uint32_t max_error_duration_ms = 2000;     // Default: 2 second max error

void encoderMotionInit(int32_t error_threshold, uint32_t max_error_time_ms) {
  encoder_error_threshold = error_threshold;
  max_error_duration_ms = max_error_time_ms;
  
  for (int i = 0; i < 4; i++) {
    position_errors[i].error_threshold = error_threshold;
    position_errors[i].max_error_time_ms = max_error_time_ms;
    position_errors[i].current_error = 0;
    position_errors[i].max_error = 0;
    position_errors[i].error_active = false;
    position_errors[i].error_count = 0;
  }
  
  Serial.println("[ENCODER-MOTION] Integration initialized");
  Serial.print("[ENCODER-MOTION] Error threshold: ");
  Serial.print(error_threshold / 1000.0f);
  Serial.print("mm, Max duration: ");
  Serial.print(max_error_time_ms);
  Serial.println("ms");
}

bool encoderMotionUpdate() {
  encoder_status_t status = wj66GetStatus();
  
  if (status != ENCODER_OK) {
    logDebug("Encoder status not OK: %d", status);
    return false;
  }
  
  uint32_t now = millis();
  bool all_valid = true;
  
  for (int i = 0; i < 4; i++) {
    // Check if encoder data is stale
    if (wj66IsStale(i)) {
      logWarning("Encoder axis %d stale (age: %lu ms)", i, wj66GetAxisAge(i));
      all_valid = false;
      continue;
    }
    
    // Get encoder position
    int32_t encoder_pos = wj66GetPosition(i);
    
    // Get motion position
    int32_t motion_pos = motionGetPosition(i);
    
    // Calculate error
    int32_t error = encoder_pos - motion_pos;
    position_errors[i].current_error = error;
    
    // Track maximum error
    if (abs(error) > abs(position_errors[i].max_error)) {
      position_errors[i].max_error = error;
    }
    
    // Check for position error
    if (abs(error) > encoder_error_threshold) {
      // Error exceeded threshold
      if (!position_errors[i].error_active) {
        position_errors[i].error_active = true;
        position_errors[i].error_time_ms = now;
        position_errors[i].error_count++;
        
        logWarning("Encoder-Motion Error on axis %d: error=%ld (threshold=%ld)", 
                   i, error, encoder_error_threshold);
        faultLogWarning(FAULT_BOOT_FAILED, "Position error detected");
      } else {
        // Error still active - check duration
        uint32_t error_duration = now - position_errors[i].error_time_ms;
        if (error_duration > max_error_duration_ms) {
          logError("Position error on axis %d persisted %lums", i, error_duration);
          faultLogError(FAULT_BOOT_FAILED, "Position error - possible stall");
          safetyReportStall(i);
        }
      }
    } else {
      // Error within threshold
      if (position_errors[i].error_active) {
        uint32_t error_duration = now - position_errors[i].error_time_ms;
        logInfo("Encoder-Motion Error resolved on axis %d after %lums", i, error_duration);
        position_errors[i].error_active = false;
      }
    }
    
    // Optionally use encoder as position source
    if (encoder_feedback_enabled && abs(error) < encoder_error_threshold) {
      // Use encoder position for more accurate feedback
      // This corrects for cumulative encoder drift
      // Commented out by default - enable only if needed
      // motionSetPosition(i, encoder_pos);
    }
  }
  
  return all_valid;
}

int32_t encoderMotionGetPositionError(uint8_t axis) {
  if (axis < 4) {
    return position_errors[axis].current_error;
  }
  return 0;
}

int32_t encoderMotionGetMaxError(uint8_t axis) {
  if (axis < 4) {
    return position_errors[axis].max_error;
  }
  return 0;
}

void encoderMotionResetError(uint8_t axis) {
  if (axis < 4) {
    position_errors[axis].current_error = 0;
    position_errors[axis].max_error = 0;
    position_errors[axis].error_active = false;
    logInfo("Position error reset for axis %d", axis);
  }
}

bool encoderMotionHasError(uint8_t axis) {
  if (axis < 4) {
    return position_errors[axis].error_active;
  }
  return false;
}

uint32_t encoderMotionGetErrorCount(uint8_t axis) {
  if (axis < 4) {
    return position_errors[axis].error_count;
  }
  return 0;
}

void encoderMotionEnableFeedback(bool enable) {
  encoder_feedback_enabled = enable;
  Serial.print("[ENCODER-MOTION] Feedback ");
  Serial.println(enable ? "ENABLED" : "DISABLED");
}

bool encoderMotionIsFeedbackActive() {
  return encoder_feedback_enabled;
}

void encoderMotionDiagnostics() {
  Serial.println("\n[ENCODER-MOTION] === Diagnostics ===");
  Serial.print("Feedback: ");
  Serial.println(encoder_feedback_enabled ? "ENABLED" : "DISABLED");
  Serial.print("Error threshold: ");
  Serial.print(encoder_error_threshold / 1000.0f);
  Serial.println("mm");
  Serial.print("Max error duration: ");
  Serial.print(max_error_duration_ms);
  Serial.println("ms");
  
  Serial.println("\nPer-Axis Status:");
  for (int i = 0; i < 4; i++) {
    Serial.print("  Axis ");
    Serial.print(i);
    Serial.print(": Error=");
    Serial.print(position_errors[i].current_error / 1000.0f);
    Serial.print("mm Max=");
    Serial.print(position_errors[i].max_error / 1000.0f);
    Serial.print("mm Active=");
    Serial.print(position_errors[i].error_active ? "YES" : "NO");
    Serial.print(" Count=");
    Serial.println(position_errors[i].error_count);
  }
  Serial.println("");
}
