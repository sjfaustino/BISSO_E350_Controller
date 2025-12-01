#include "motion.h"
#include "system_constants.h"
#include "plc_iface.h"
#include "encoder_calibration.h" 
#include "fault_logging.h"
#include "serial_logger.h"
#include <math.h>

// NOTE: AXIS_TO_I73_BIT and AXIS_TO_CONSENSO_BIT are declared extern in motion.h 
// and defined in motion_core.cpp.

#define MOTION_MED_SPEED_DEFAULT 90.0f 

// ============================================================================
// PLC I/O CONTROL IMPLEMENTATION
// ============================================================================

void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction) {
    if (axis >= MOTION_AXES && axis != 255) {
        logError("[MOTION] Invalid axis %d for PLC I/O set", axis);
        return;
    }

    if (enable || axis == 255) {
        // Clear all AXIS bits
        elboI73SetAxis(ELBO_I73_AXIS_X, false); 
        elboI73SetAxis(ELBO_I73_AXIS_Y, false); 
        elboI73SetAxis(ELBO_I73_AXIS_Z, false); 
        // Clear both DIRECTION bits
        elboI73SetDirection(ELBO_I73_DIRECTION_PLUS, false);
        elboI73SetDirection(ELBO_I73_DIRECTION_MINUS, false);
        
        if (axis == 255) return;
    }

    if (enable) {
        // AXIS_TO_I73_BIT is now accessible due to extern declaration in motion.h
        uint8_t axis_bit = AXIS_TO_I73_BIT[axis]; 
        elboI73SetAxis(axis_bit, true);

        if (is_plus_direction) {
            elboI73SetDirection(ELBO_I73_DIRECTION_PLUS, true);
        } else {
            elboI73SetDirection(ELBO_I73_DIRECTION_MINUS, true);
        }
    }
}

speed_profile_t motionMapSpeedToProfile(uint8_t axis, float requested_speed_mm_s) {
  // Logic to map speed to profile based on machineCal
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
  // CRITICAL SAFETY: Ensure VS Mode is OFF before enabling normal speed profiles.
  elboI73SetVSMode(false);

  uint8_t bit0 = (profile == SPEED_PROFILE_2);
  uint8_t bit1 = (profile == SPEED_PROFILE_3); 
  
  bool fast_ok = elboI72SetSpeed(ELBO_I72_FAST, bit0);
  bool med_ok = elboI72SetSpeed(ELBO_I72_MED, bit1);   

  if (!fast_ok || !med_ok) {
    logError("[MOTION] *** CRITICAL: Speed profile I2C transmission failed! ***");
    faultLogError(FAULT_I2C_ERROR, "PLC speed profile I2C write failed");
    // Since motionEmergencyStop is defined in motion_commands.cpp via motion.h,
    // and is a high-level API call, it must be available here.
    // motionEmergencyStop(); 
    return;
  }
  logInfo("[MOTION] Speed profile: %d - I2C OK", profile);
}

void motionSetVSMode(bool active) {
    if (active) {
        // Enforce exclusivity: Clear speed bits FIRST
        elboI72SetSpeed(ELBO_I72_FAST, false);
        elboI72SetSpeed(ELBO_I72_MED, false);
        
        // Then enable VS
        if (!elboI73SetVSMode(true)) {
             faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, 0, "Failed to enable VS Mode");
        } else {
             logInfo("[MOTION] VS Mode ENABLED (Speed bits cleared)");
        }
    } else {
        // Just disable VS
        if (!elboI73SetVSMode(false)) {
             faultLogEntry(FAULT_ERROR, FAULT_I2C_ERROR, -1, 0, "Failed to disable VS Mode");
        } else {
             logInfo("[MOTION] VS Mode DISABLED");
        }
    }
}