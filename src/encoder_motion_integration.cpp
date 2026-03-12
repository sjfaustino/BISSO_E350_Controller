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

// Defined Defaults
#define DEFAULT_ENCODER_ERROR_THRESHOLD_UM 100000 
#define DEFAULT_ERROR_TIMEOUT_MS 2000

// Zero-initialized by C++ standard
static position_error_t position_errors[4];

static bool encoder_feedback_enabled = false;
static int32_t encoder_error_threshold_um = DEFAULT_ENCODER_ERROR_THRESHOLD_UM;  
static uint32_t max_error_duration_ms = DEFAULT_ERROR_TIMEOUT_MS;     

#ifndef KEY_ENC_ERR_THRESHOLD
#define KEY_ENC_ERR_THRESHOLD "enc_thresh" 
#endif

void encoderMotionInit(int32_t default_threshold_um, uint32_t default_timeout) {
  // Load from Config System
  encoder_error_threshold_um = configGetInt(KEY_ENC_ERR_THRESHOLD, default_threshold_um > 0 ? default_threshold_um : DEFAULT_ENCODER_ERROR_THRESHOLD_UM);
  max_error_duration_ms = configGetInt(KEY_ENC_DEV_TIMEOUT, default_timeout > 0 ? default_timeout : DEFAULT_ERROR_TIMEOUT_MS);
  
  logInfo("[ENC_INT] Loading Config: Thresh=%ld um, Timeout=%lu ms", 
          (long)encoder_error_threshold_um, (unsigned long)max_error_duration_ms);

  for (int i = 0; i < 4; i++) {
    // TODO: Transition to per-axis error thresholds in future config updates.
    // For now, all axes start with the global unified limit.
    position_errors[i].error_threshold = encoder_error_threshold_um;
    position_errors[i].max_error_time_ms = max_error_duration_ms;
    position_errors[i].current_error = 0;
    position_errors[i].max_error = 0;
    position_errors[i].error_active = false;
    position_errors[i].error_count = 0;
    position_errors[i].error_time_ms = 0; 
    position_errors[i].last_encoder_pos = 0;
    position_errors[i].last_change_ms = millis();
    position_errors[i].silence_alarm = false;
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
      
      // If the sensor goes completely stale while we are supposed to be moving, 
      // trigger the safety watchdog immediately instead of silently ignoring it.
      if (motionGetState(i) != MOTION_IDLE) {
          uint32_t silence_duration = now - position_errors[i].last_change_ms;
          if (silence_duration > max_error_duration_ms) {
              if (!position_errors[i].silence_alarm) {
                  position_errors[i].silence_alarm = true;
                  logError("[ENC_INT] Axis %d STALE/DISCONNECTED Watchdog Triggered (%lu ms)", i, (unsigned long)silence_duration);
                  safetyReportFault(FAULT_CRITICAL, FAULT_ENCODER_TIMEOUT, i, "Encoder Stale during motion", SAFETY_ENCODER_ERROR);
              }
          }
      }
      continue;
    }
    
    int32_t encoder_pos = wj66GetPosition(i);
    int32_t target_pos = motionGetTarget(i);
    motion_state_t state = motionGetState(i);

    // Detect deviation during motion AND at idle
    // During motion: compare encoder to current position (detect lost steps)
    // At idle: compare encoder to target (detect drift)
    int32_t error_counts = 0;
    if (state == MOTION_IDLE) {
        error_counts = encoder_pos - target_pos;
    } else {
        // During motion, check following error (encoder vs current position)
        int32_t current_pos = motionGetPosition(i);
        error_counts = encoder_pos - current_pos;
    }

    float scale = motionGetAxisScale(i); // pulses per mm
    int32_t error_um = 0;
    if (scale > 0.0f) {
        float error_mm = (float)error_counts / scale;
        error_um = (int32_t)(error_mm * 1000.0f);
    }

    position_errors[i].current_error = error_um;
    if (abs(error_um) > abs(position_errors[i].max_error)) {
      position_errors[i].max_error = error_um;
    }
    
    if (abs(error_um) > encoder_error_threshold_um) {
      if (!position_errors[i].error_active) {
        position_errors[i].error_active = true;
        position_errors[i].error_time_ms = now; 
        position_errors[i].error_count++;
        
        logWarning("Axis %d Drift Error: %ld (Limit: %ld um)", i, (long)error_um, (long)encoder_error_threshold_um);
        faultLogEntry(FAULT_WARNING, FAULT_ENCODER_SPIKE, i, error_um, "Axis Drift (Static)"); 
      }
    } else {
        if (position_errors[i].error_active) {
          position_errors[i].error_active = false;
          position_errors[i].error_time_ms = 0; 
        }
      }

      // --- Encoder Silence Watchdog ---
      if (state != MOTION_IDLE) {
          if (encoder_pos != position_errors[i].last_encoder_pos) {
              position_errors[i].last_encoder_pos = encoder_pos;
              position_errors[i].last_change_ms = now;
              position_errors[i].silence_alarm = false;
          } else {
              uint32_t silence_duration = now - position_errors[i].last_change_ms;
              if (silence_duration > max_error_duration_ms) {
                  if (!position_errors[i].silence_alarm) {
                      position_errors[i].silence_alarm = true;
                      logError("[ENC_INT] Axis %d Silence Watchdog Triggered (%lu ms)", i, (unsigned long)silence_duration);
                      // Trigger a managed stop via safety system
                      safetyReportFault(FAULT_CRITICAL, FAULT_ENCODER_TIMEOUT, i, "Encoder Silence during motion", SAFETY_ENCODER_ERROR);
                  }
              }
          }
      } else {
          // Reset watchdog tracker when idle
          position_errors[i].last_encoder_pos = encoder_pos;
          position_errors[i].last_change_ms = now;
          position_errors[i].silence_alarm = false;
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
  logPrintln("\n=== ENCODER INTEGRATION ===");
  logPrintf("Feedback: %s\n", encoder_feedback_enabled ? "[ON]" : "[OFF]");
  logPrintf("Threshold: %.1f mm\n", encoder_error_threshold_um / 1000.0f);
  
  for (int i = 0; i < 4; i++) {
    logPrintf("Axis %d: Err=%.1f mm | State=%s\n",
        i, position_errors[i].current_error / 1000.0f, 
        position_errors[i].error_active ? "[ERR]" : "[OK]");
  }
  serialLoggerUnlock();
}
