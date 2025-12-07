/**
 * @file encoder_motion_integration.cpp
 * @brief Logic to cross-check encoder feedback against planner target
 */

#include "encoder_motion_integration.h"
#include "safety.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "encoder_wj66.h"
#include "motion.h"       // <-- CRITICAL FIX
#include "motion_state.h" // <-- CRITICAL FIX

static position_error_t position_errors[4] = {
  {0, 0, 100000, 0, 2000, false, 0},
  {0, 0, 100000, 0, 2000, false, 0},
  {0, 0, 100000, 0, 2000, false, 0},
  {0, 0, 100000, 0, 2000, false, 0}
};

static bool encoder_feedback_enabled = false;
static int32_t encoder_error_threshold = 100000;  
static uint32_t max_error_duration_ms = 2000;     

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
    position_errors[i].error_time_ms = 0; 
  }
  Serial.println("[ENC_INT] Initialized");
}

bool encoderMotionUpdate() {
  if (!encoder_feedback_enabled) return true;

  encoder_status_t status = wj66GetStatus();
  if (status != ENCODER_OK) {
    if (status == ENCODER_TIMEOUT) {
        faultLogEntry(FAULT_WARNING, FAULT_ENCODER_TIMEOUT, -1, status, "WJ66 Comm Timeout");
    }
    return false;
  }
  
  uint32_t now = millis();
  bool all_valid = true;
  
  for (int i = 0; i < 4; i++) {
    if (wj66IsStale(i)) {
      // logWarning("Encoder %d stale", i); // Reduced spam
      all_valid = false;
      continue;
    }
    
    // Now visible because headers are included
    int32_t encoder_pos = wj66GetPosition(i);
    int32_t motion_pos = motionGetPosition(i); 

    int32_t error = 0;
    if (motionGetState(i) == MOTION_EXECUTING) {
      error = encoder_pos - motionGetTarget(i); 
    } else {
      error = encoder_pos - motion_pos;
    }
    
    position_errors[i].current_error = error;
    if (abs(error) > abs(position_errors[i].max_error)) {
      position_errors[i].max_error = error;
    }
    
    if (abs(error) > encoder_error_threshold) {
      if (!position_errors[i].error_active) {
        position_errors[i].error_active = true;
        position_errors[i].error_time_ms = now; 
        position_errors[i].error_count++;
        
        logWarning("Axis %d error: %ld", i, (long)error);
        faultLogEntry(FAULT_WARNING, FAULT_ENCODER_SPIKE, i, error, "Axis deviation"); 
      }
    } else {
      if (position_errors[i].error_active) {
        position_errors[i].error_active = false;
        position_errors[i].error_time_ms = 0; 
      }
    }
  }
  return all_valid;
}

int32_t encoderMotionGetPositionError(uint8_t axis) { return (axis < 4) ? position_errors[axis].current_error : 0; }
int32_t encoderMotionGetMaxError(uint8_t axis) { return (axis < 4) ? position_errors[axis].max_error : 0; }
uint32_t encoderMotionGetErrorDuration(uint8_t axis) { return (axis < 4 && position_errors[axis].error_active) ? (millis() - position_errors[axis].error_time_ms) : 0; }

void encoderMotionResetError(uint8_t axis) {
  if (axis < 4) {
    position_errors[axis].current_error = 0;
    position_errors[axis].max_error = 0;
    position_errors[axis].error_active = false;
    position_errors[axis].error_time_ms = 0;
  }
}

bool encoderMotionHasError(uint8_t axis) { return (axis < 4) ? position_errors[axis].error_active : false; }
uint32_t encoderMotionGetErrorCount(uint8_t axis) { return (axis < 4) ? position_errors[axis].error_count : 0; }

void encoderMotionEnableFeedback(bool enable) {
  encoder_feedback_enabled = enable;
  Serial.printf("[ENC_INT] Feedback %s\n", enable ? "[ENABLED]" : "[DISABLED]");
}

bool encoderMotionIsFeedbackActive() { return encoder_feedback_enabled; }

void encoderMotionDiagnostics() {
  Serial.println("\n=== ENCODER INTEGRATION ===");
  Serial.printf("Feedback: %s\n", encoder_feedback_enabled ? "[ON]" : "[OFF]");
  Serial.printf("Threshold: %.1f mm\n", encoder_error_threshold / 1000.0f);
  
  for (int i = 0; i < 4; i++) {
    Serial.printf("Axis %d: Err=%.1f mm | State=%s\n",
        i, position_errors[i].current_error / 1000.0f, 
        position_errors[i].error_active ? "[ERR]" : "[OK]");
  }
}