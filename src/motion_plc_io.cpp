#include "motion.h"
#include "system_constants.h"
#include "plc_iface.h"
#include "encoder_calibration.h" 
#include "fault_logging.h"
#include "serial_logger.h"
#include "task_manager.h" // <-- NEW: Required for Mutex access
#include <math.h>

#define MOTION_MED_SPEED_DEFAULT 90.0f 
#define I2C_LOCK_TIMEOUT_MS 10 // Max wait time for bus access

void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction) {
    if (axis >= MOTION_AXES && axis != 255) {
        logError("[MOTION] Invalid axis %d for IO", axis);
        return;
    }

    // --- CRITICAL SECTION START: Acquire I2C Bus ---
    // Prevents collision with PLC (P18) and Safety (P24) tasks
    if (!taskLockMutex(taskGetI2cMutex(), I2C_LOCK_TIMEOUT_MS)) {
        logError("[MOTION] [CRIT] I2C Mutex Timeout (Dir Set)");
        faultLogEntry(FAULT_CRITICAL, FAULT_I2C_ERROR, axis, 0, "Motion I2C Lock Fail");
        return; // Abort to prevent bus corruption
    }

    if (enable || axis == 255) {
        elboI73SetAxis(ELBO_I73_AXIS_X, false); 
        elboI73SetAxis(ELBO_I73_AXIS_Y, false); 
        elboI73SetAxis(ELBO_I73_AXIS_Z, false); 
        elboI73SetDirection(ELBO_I73_DIRECTION_PLUS, false);
        elboI73SetDirection(ELBO_I73_DIRECTION_MINUS, false);
        
        if (axis == 255) {
            taskUnlockMutex(taskGetI2cMutex());
            return;
        }
    }

    if (enable) {
        uint8_t axis_bit = AXIS_TO_I73_BIT[axis]; 
        elboI73SetAxis(axis_bit, true);

        if (is_plus_direction) elboI73SetDirection(ELBO_I73_DIRECTION_PLUS, true);
        else elboI73SetDirection(ELBO_I73_DIRECTION_MINUS, true);
    }

    taskUnlockMutex(taskGetI2cMutex());
    // --- CRITICAL SECTION END ---
}

speed_profile_t motionMapSpeedToProfile(uint8_t axis, float requested_speed_mm_s) {
  float slow_speed_mm_s = machineCal.X.speed_slow_mm_min / 60.0f; 
  float med_speed_mm_s  = machineCal.X.speed_med_mm_min / 60.0f;
  float fast_speed_mm_s = machineCal.X.speed_fast_mm_min / 60.0f;

  if (slow_speed_mm_s < 0.1f) slow_speed_mm_s = 5.0f;   
  if (med_speed_mm_s < 0.1f) med_speed_mm_s = 15.0f;  
  if (fast_speed_mm_s < 0.1f) fast_speed_mm_s = 40.0f; 

  float diff_slow = fabsf(requested_speed_mm_s - slow_speed_mm_s);
  float diff_med  = fabsf(requested_speed_mm_s - med_speed_mm_s);
  float diff_fast = fabsf(requested_speed_mm_s - fast_speed_mm_s);

  float min_diff = min(diff_slow, min(diff_med, diff_fast));

  if (min_diff == diff_slow) return SPEED_PROFILE_1;
  else if (min_diff == diff_med) return SPEED_PROFILE_2;
  else return SPEED_PROFILE_3;
}

void motionSetPLCSpeedProfile(speed_profile_t profile) {
  // --- CRITICAL SECTION START ---
  if (!taskLockMutex(taskGetI2cMutex(), I2C_LOCK_TIMEOUT_MS)) {
      logError("[MOTION] [CRIT] I2C Mutex Timeout (Speed Set)");
      faultLogEntry(FAULT_WARNING, FAULT_I2C_ERROR, -1, profile, "Speed Profile I2C Lock Fail");
      return; 
  }

  elboI73SetVSMode(false);

  uint8_t bit0 = (profile == SPEED_PROFILE_2);
  uint8_t bit1 = (profile == SPEED_PROFILE_3); 
  
  bool fast_ok = elboI72SetSpeed(ELBO_I72_FAST, bit0);
  bool med_ok = elboI72SetSpeed(ELBO_I72_MED, bit1);   

  if (!fast_ok || !med_ok) {
    logError("[MOTION] [CRITICAL] Speed profile I2C failed");
    faultLogError(FAULT_I2C_ERROR, "PLC speed profile I2C fail");
  } else {
    logInfo("[MOTION] Profile %d active", profile);
  }

  taskUnlockMutex(taskGetI2cMutex());
  // --- CRITICAL SECTION END ---
}

void motionSetVSMode(bool active) {
    // --- CRITICAL SECTION START ---
    if (!taskLockMutex(taskGetI2cMutex(), I2C_LOCK_TIMEOUT_MS)) {
        logError("[MOTION] [CRIT] I2C Mutex Timeout (VS Mode)");
        return;
    }

    if (active) {
        elboI72SetSpeed(ELBO_I72_FAST, false);
        elboI72SetSpeed(ELBO_I72_MED, false);
        
        if (!elboI73SetVSMode(true)) {
             faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, 0, "Failed to enable VS");
        } else {
             logInfo("[MOTION] VS Mode [ENABLED]");
        }
    } else {
        if (!elboI73SetVSMode(false)) {
             faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, 0, "Failed to disable VS");
        } else {
             logInfo("[MOTION] VS Mode [DISABLED]");
        }
    }

    taskUnlockMutex(taskGetI2cMutex());
    // --- CRITICAL SECTION END ---
}