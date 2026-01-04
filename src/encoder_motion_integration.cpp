/**
 * @file encoder_motion_integration.cpp
 * @brief Logic to cross-check encoder feedback against planner target (PosiPro)
 */

#include "encoder_motion_integration.h"
#include "safety.h"
#include "fault_logging.h"
#include "serial_logger.h"
#include "encoder_wj66.h"
#include "motion.h"       
#include "motion_state.h" 
#include "config_unified.h"
#include "config_keys.h"

// Defined Defaults (Magic Number Fix)
#define DEFAULT_ENCODER_ERROR_THRESHOLD 100000 
#define DEFAULT_ERROR_TIMEOUT_MS 2000

static position_error_t position_errors[4] = {
  {0, 0, 0, 0, 0, false, 0},
  {0, 0, 0, 0, 0, false, 0},
  {0, 0, 0, 0, 0, false, 0},
  {0, 0, 0, 0, 0, false, 0}
};

static bool encoder_feedback_enabled = false;
static int32_t encoder_error_threshold = DEFAULT_ENCODER_ERROR_THRESHOLD;  
static uint32_t max_error_duration_ms = DEFAULT_ERROR_TIMEOUT_MS;     

#ifndef KEY_ENC_ERR_THRESHOLD
#define KEY_ENC_ERR_THRESHOLD "enc_thresh" 
#endif

void encoderMotionInit(int32_t default_threshold, uint32_t default_timeout) {
  // Load from Config System
  encoder_error_threshold = configGetInt(KEY_ENC_ERR_THRESHOLD, default_threshold > 0 ? default_threshold : DEFAULT_ENCODER_ERROR_THRESHOLD);
  max_error_duration_ms = configGetInt(KEY_STALL_TIMEOUT, default_timeout > 0 ? default_timeout : DEFAULT_ERROR_TIMEOUT_MS);
  
  logInfo("[ENC_INT] Loading Config: Thresh=%ld, Timeout=%lu ms", 
          (long)encoder_error_threshold, (unsigned long)max_error_duration_ms);

  for (int i = 0; i < 4; i++) {
    position_errors[i].error_threshold = encoder_error_threshold;
    position_errors[i].max_error_time_ms = max_error_duration_ms;
    position_errors[i].current_error = 0;
    position_errors[i].max_error = 0;
    position_errors[i].error_active = false;
    position_errors[i].error_count = 0;
    position_errors[i].error_time_ms = 0; 
  }
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
      all_valid = false;
      continue;
    }
    
    int32_t encoder_pos = wj66GetPosition(i);
    int32_t target_pos = motionGetTarget(i);
    motion_state_t state = motionGetState(i);

    // PHASE 5.10: Detect deviation during motion AND at idle
    // During motion: compare encoder to current position (detect lost steps)
    // At idle: compare encoder to target (detect drift)
    int32_t error = 0;
    if (state == MOTION_IDLE) {
        error = encoder_pos - target_pos;
    } else {
        // During motion, check following error (encoder vs current position)
        int32_t current_pos = motionGetPosition(i);
        error = encoder_pos - current_pos;
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
        
        logWarning("Axis %d Drift Error: %ld (Limit: %ld)", i, (long)error, (long)encoder_error_threshold);
        faultLogEntry(FAULT_WARNING, FAULT_ENCODER_SPIKE, i, error, "Axis Drift (Static)"); 
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

// --- Accessors ---

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
  logInfo("[ENC_INT] Feedback %s", enable ? "[ENABLED]" : "[DISABLED]");
}

bool encoderMotionIsFeedbackActive() { return encoder_feedback_enabled; }

void encoderMotionDiagnostics() {
  serialLoggerLock();
  Serial.println("\n=== ENCODER INTEGRATION ===");
  Serial.printf("Feedback: %s\n", encoder_feedback_enabled ? "[ON]" : "[OFF]");
  Serial.printf("Threshold: %.1f mm\n", encoder_error_threshold / 1000.0f);
  
  for (int i = 0; i < 4; i++) {
    Serial.printf("Axis %d: Err=%.1f mm | State=%s\n",
        i, position_errors[i].current_error / 1000.0f, 
        position_errors[i].error_active ? "[ERR]" : "[OK]");
  }
  serialLoggerUnlock();
}
