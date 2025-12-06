#include "motion.h"
#include "system_constants.h"
#include "plc_iface.h"
#include "encoder_calibration.h" 
#include "fault_logging.h"
#include "serial_logger.h"
#include "task_manager.h" 
#include <math.h>

#define MOTION_MED_SPEED_DEFAULT 90.0f 
#define I2C_LOCK_TIMEOUT_MS 10 

void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction) {
    if (axis >= MOTION_AXES && axis != 255) {
        logError("[MOTION] Invalid axis %d", axis);
        return;
    }

    if (!taskLockMutex(taskGetI2cMutex(), I2C_LOCK_TIMEOUT_MS)) {
        logError("[MOTION] [CRIT] I2C Mutex Timeout");
        faultLogEntry(FAULT_CRITICAL, FAULT_I2C_ERROR, axis, 0, "Motion I2C Lock Fail");
        return; 
    }

    // Prepare Shadow Register Logic
    // PCF8574 Logic: 0 = ON (Active), 1 = OFF (Inactive)
    uint8_t clear_mask = 0;
    uint8_t set_bits = 0;

    // 1. Reset Phase: Always clear Axis and Direction bits
    uint8_t mask_all_axes = (1 << ELBO_I73_AXIS_X) | (1 << ELBO_I73_AXIS_Y) | (1 << ELBO_I73_AXIS_Z);
    uint8_t mask_all_dirs = (1 << ELBO_I73_DIRECTION_PLUS) | (1 << ELBO_I73_DIRECTION_MINUS);
    
    clear_mask |= (mask_all_axes | mask_all_dirs);
    set_bits   |= (mask_all_axes | mask_all_dirs); // Default to 1 (OFF)

    // 2. Set Phase: If enabled, pull specific bits LOW (0)
    if (enable && axis != 255) {
        uint8_t axis_bit = AXIS_TO_I73_BIT[axis];
        // Turn ON axis: clear bit in 'set_bits' (make it 0)
        set_bits &= ~(1 << axis_bit);

        if (is_plus_direction) {
            set_bits &= ~(1 << ELBO_I73_DIRECTION_PLUS);
        } else {
            set_bits &= ~(1 << ELBO_I73_DIRECTION_MINUS);
        }
    }

    // Atomic Write
    if (!elboI73WriteBatch(clear_mask, set_bits)) {
        faultLogWarning(FAULT_I2C_ERROR, "Motion Batch Write Fail");
    }

    taskUnlockMutex(taskGetI2cMutex());
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
  if (!taskLockMutex(taskGetI2cMutex(), I2C_LOCK_TIMEOUT_MS)) {
      logError("[MOTION] [CRIT] I2C Lock Fail (Speed)");
      return; 
  }

  // 1. Reset VS Mode on I73 (Batch)
  uint8_t vs_mask = (1 << ELBO_I73_V_S_MODE);
  uint8_t vs_bits = vs_mask; // Default 1 (OFF)
  elboI73WriteBatch(vs_mask, vs_bits);

  // 2. Set Speed Bits on I72 (Batch)
  // Active Low Logic: 0=ON, 1=OFF
  uint8_t spd_mask = (1 << ELBO_I72_FAST) | (1 << ELBO_I72_MED);
  uint8_t spd_bits = spd_mask; // Default 1 (OFF)

  if (profile == SPEED_PROFILE_2) spd_bits &= ~(1 << ELBO_I72_FAST); // Medium (Map check required?)
  // Correction based on original code: 
  // bit0 = (profile == SPEED_PROFILE_2); -> FAST pin?
  // bit1 = (profile == SPEED_PROFILE_3); -> MED pin?
  // Original logic was confusing naming. Let's strictly follow header defines.
  
  if (profile == SPEED_PROFILE_2) spd_bits &= ~(1 << ELBO_I72_FAST); 
  if (profile == SPEED_PROFILE_3) spd_bits &= ~(1 << ELBO_I72_MED);

  if (!elboI72WriteBatch(spd_mask, spd_bits)) {
      faultLogWarning(FAULT_I2C_ERROR, "Speed Batch Write Fail");
  } else {
      logInfo("[MOTION] Profile %d set", profile);
  }

  taskUnlockMutex(taskGetI2cMutex());
}

void motionSetVSMode(bool active) {
    if (!taskLockMutex(taskGetI2cMutex(), I2C_LOCK_TIMEOUT_MS)) {
        logError("[MOTION] I2C Lock Fail (VS)");
        return;
    }

    // 1. Clear Speed bits on I72
    uint8_t spd_mask = (1 << ELBO_I72_FAST) | (1 << ELBO_I72_MED);
    elboI72WriteBatch(spd_mask, spd_mask); // Write 1s (OFF)

    // 2. Set VS bit on I73
    uint8_t vs_mask = (1 << ELBO_I73_V_S_MODE);
    uint8_t vs_bits = active ? 0 : vs_mask; // 0=ON if active, else 1=OFF

    if (elboI73WriteBatch(vs_mask, vs_bits)) {
        logInfo("[MOTION] VS Mode: %s", active ? "ON" : "OFF");
    }

    taskUnlockMutex(taskGetI2cMutex());
}