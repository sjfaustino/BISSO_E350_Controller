#include "cli.h"
#include "serial_logger.h"
#include "encoder_calibration.h"
#include "encoder_wj66.h"
#include "input_validation.h"
#include "system_constants.h"
#include "hardware_config.h"
#include "motion.h" 
#include "fault_logging.h" 
#include "encoder_comm_stats.h"
#include "encoder_motion_integration.h"
#include "system_utilities.h" 
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

typedef struct {
    uint32_t time_ms;
    int32_t counts;
} calibration_run_t;

// Helper to get the correct axis pointer
AxisCalibration* getAxisCalPtrForCli(uint8_t axis) {
    if (axis == 0) return &machineCal.X;
    if (axis == 1) return &machineCal.Y;
    if (axis == 2) return &machineCal.Z;
    if (axis == 3) return &machineCal.A;
    return NULL;
}

calibration_run_t perform_single_measurement(uint8_t axis, speed_profile_t profile, float distance_mm, bool is_forward) {
    calibration_run_t run = {0, 0};

    logInfo("[CALIB] Measuring %s on Axis %d (Profile %d) for %.1f mm...", 
            is_forward ? "FORWARD" : "REVERSE", axis, (int)profile, distance_mm);
    
    int32_t start_pos = wj66GetPosition(axis);
    
    motionSetPLCSpeedProfile(profile);
    motionSetPLCAxisDirection(axis, true, is_forward); 
    
    uint32_t start_time = millis();
    const uint32_t max_timeout = 60000; 
    
    int32_t target_delta_counts = (int32_t)(distance_mm * MOTION_POSITION_SCALE_FACTOR);
    bool motion_complete = false;
    
    while (millis() - start_time < max_timeout) {
        int32_t current_pos = wj66GetPosition(axis);
        int32_t actual_delta = abs(current_pos - start_pos);
        if (actual_delta >= target_delta_counts) {
            motion_complete = true;
            break;
        }
        delay(10); 
    }
    
    motionSetPLCAxisDirection(255, false, false); 
    
    run.time_ms = millis() - start_time;
    run.counts = abs(wj66GetPosition(axis) - start_pos); 

    if (!motion_complete || run.time_ms >= max_timeout - 100) { 
        // FIX: Format specifier %ld for int32_t
        logError("[CALIB] [FAIL] Timeout on %s move. Measured: %ld counts.", is_forward ? "FORWARD" : "REVERSE", (long)run.counts);
        faultLogError(FAULT_CALIBRATION_MISSING, "Speed calibration failed: Timeout");
        run.time_ms = 0xFFFFFFFF; 
    }

    return run;
}

// ============================================================================
// COMMAND HANDLER IMPLEMENTATIONS
// ============================================================================

void cmd_encoder_calib(int argc, char** argv) {
  if (argc < 3) {
    Serial.println("[CLI] Usage: calib axis distance_mm (e.g., calib X 1000.0)");
    return;
  }
  uint8_t axis = axisCharToIndex(argv[1]);
  float distance_mm = 0.0f;

  if (axis >= 4) {
    Serial.println("[CLI] [ERR] Invalid axis. Use X, Y, Z, or A.");
    return;
  }
  if (!parseAndValidateFloat(argv[2], &distance_mm, 10.0f, 10000.0f)) {
      Serial.println("[CLI] [ERR] Invalid distance. Must be > 10.0mm.");
      return;
  }
  encoderCalibrationStart(axis, distance_mm);
}

void cmd_encoder_reset(int argc, char** argv) {
    if (argc < 4) {
        Serial.println("[CLI] Usage: calibrate speed reset [AXIS]");
        return;
    }
    uint8_t axis = axisCharToIndex(argv[2]);
    
    if (axis >= 4) {
        Serial.println("[CLI] [ERR] Invalid axis.");
        return;
    }
    
    AxisCalibration* cal = getAxisCalPtrForCli(axis);
    if (cal) {
        Serial.printf("[CLI] Resetting speed profiles for Axis %c...\n", axisIndexToChar(axis));
        cal->speed_slow_mm_min = 300.0f; 
        cal->speed_med_mm_min = 900.0f;
        cal->speed_fast_mm_min = 2400.0f;
        saveAllCalibration();
        Serial.println("[CLI] [OK] Speed profiles reset and saved.");
    } else {
         Serial.printf("[CLI] [ERR] Calibration data not found for Axis %c.\n", axisIndexToChar(axis));
    }
}

void cmd_calib_ppmm_start(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("[CLI] Usage: calibrate ppmm [AXIS] [DISTANCE_MM]");
        return;
    }
    uint8_t axis = axisCharToIndex(argv[1]);
    char axis_char = axisIndexToChar(axis);
    float distance_mm = 0.0f;

    if (axis >= 4) {
        Serial.println("[CLI] [ERR] Invalid axis.");
        return;
    }
    if (!parseAndValidateFloat(argv[2], &distance_mm, 10.0f, 10000.0f)) {
        Serial.println("[CLI] [ERR] Invalid distance.");
        return;
    }
    if (g_manual_calib.state != CALIBRATION_IDLE) {
        Serial.println("[CLI] [ERR] Calibration already in progress.");
        return;
    }
    
    if (axis_char == 'A') {
        logWarning("[CALIB] Note: Axis A is rotational (Distance = Degrees).");
    }

    g_manual_calib.state = CALIB_MANUAL_START;
    g_manual_calib.axis = axis;
    g_manual_calib.target_mm = distance_mm;
    g_manual_calib.start_counts = wj66GetPosition(axis);
    
    Serial.println("\n=== MANUAL PPM CALIBRATION ===");
    Serial.printf("Axis: %c | Target: %.1f mm\n", axis_char, distance_mm);
    Serial.printf("Start Pos: %ld counts\n", (long)g_manual_calib.start_counts);
    
    // FIX: Use printf instead of println for formatted string
    Serial.printf("\nACTION: Move axis exactly %.1f mm, then type 'calibrate ppmm end'.\n\n", distance_mm);
    
    g_manual_calib.state = CALIB_MANUAL_WAIT_MOVE;
}

void cmd_calib_ppmm_end(int argc, char** argv) {
    if (g_manual_calib.state != CALIB_MANUAL_WAIT_MOVE) {
        Serial.println("[CLI] [ERR] No calibration in progress.");
        return;
    }

    uint8_t axis = g_manual_calib.axis;
    float target_mm = g_manual_calib.target_mm;
    int32_t end_counts = wj66GetPosition(axis);
    int32_t moved_counts = abs(end_counts - g_manual_calib.start_counts);
    
    if (moved_counts == 0) {
        Serial.println("[CLI] [ERR] No movement detected.");
        g_manual_calib.state = CALIBRATION_IDLE;
        return;
    }
    
    double calculated_ppmm = (double)moved_counts / target_mm;
    encoderCalibrationSetPPM(axis, calculated_ppmm); 
    
    Serial.println("\n=== CALIBRATION COMPLETE ===");
    Serial.printf("Measured: %ld counts\n", (long)moved_counts);
    Serial.printf("Target:   %.1f mm\n", target_mm);
    Serial.printf("Result:   %.3f pulses/unit\n", calculated_ppmm);
    
    g_manual_calib.state = CALIBRATION_IDLE;
}

void cmd_calib_ppmm_reset(int argc, char** argv) {
    if (argc < 3) {
        Serial.println("[CLI] Usage: calibrate ppmm reset [AXIS]");
        return;
    }
    uint8_t axis = axisCharToIndex(argv[2]);

    if (axis >= 4) {
        Serial.println("[CLI] [ERR] Invalid axis.");
        return;
    }
    encoderCalibrationSetPPM(axis, (double)MOTION_POSITION_SCALE_FACTOR); 
    wj66Reset(); 
    Serial.printf("[CLI] [OK] PPM reset to default (%d) for Axis %c.\n", MOTION_POSITION_SCALE_FACTOR, axisIndexToChar(axis));
}

void cmd_auto_calibrate_speed(int argc, char** argv) {
    if (argc < 4) {
        Serial.println("[CLI] Usage: calibrate speed [AXIS] [PROFILE] [DISTANCE]");
        return;
    }

    uint8_t axis = axisCharToIndex(argv[1]);
    if (axis >= 4) {
        Serial.println("[CLI] [ERR] Invalid axis.");
        return;
    }

    speed_profile_t profile;
    if (strcmp(argv[2], "SLOW") == 0) profile = SPEED_PROFILE_1;
    else if (strcmp(argv[2], "MEDIUM") == 0) profile = SPEED_PROFILE_2;
    else if (strcmp(argv[2], "FAST") == 0) profile = SPEED_PROFILE_3;
    else {
        Serial.println("[CLI] [ERR] Invalid profile (SLOW/MEDIUM/FAST).");
        return;
    }

    float distance_mm = 0.0f;
    if (!parseAndValidateFloat(argv[3], &distance_mm, 50.0f, 10000.0f)) {
        Serial.println("[CLI] [ERR] Invalid distance (> 50.0).");
        return;
    }
    
    Serial.println("\n=== SPEED CALIBRATION SEQUENCE ===");
    Serial.printf("Axis: %c | Profile: %s | Dist: %.1f mm\n", axisIndexToChar(axis), argv[2], distance_mm);
    
    calibration_run_t run_fwd = perform_single_measurement(axis, profile, distance_mm, true);
    if (run_fwd.time_ms == 0xFFFFFFFF) return; 

    calibration_run_t run_rev = perform_single_measurement(axis, profile, distance_mm, false);
    if (run_rev.time_ms == 0xFFFFFFFF) return;
    
    uint32_t total_time_ms = run_fwd.time_ms + run_rev.time_ms;
    int32_t total_counts = run_fwd.counts + run_rev.counts;
    
    if (total_time_ms == 0 || total_counts == 0) {
        Serial.println("[CALIB] [ERR] Invalid measurement data.");
        return;
    }

    float total_distance_mm = (float)total_counts / MOTION_POSITION_SCALE_FACTOR;
    float avg_time_s = (float)total_time_ms / 1000.0f;
    float speed_mm_s = total_distance_mm / avg_time_s;
    float speed_mm_min = speed_mm_s * 60.0f; 

    Serial.println("\n--- SUMMARY ---");
    Serial.printf("Fwd: %.1f mm in %.2f s\n", (float)run_fwd.counts / MOTION_POSITION_SCALE_FACTOR, (float)run_fwd.time_ms / 1000.0f);
    Serial.printf("Rev: %.1f mm in %.2f s\n", (float)run_rev.counts / MOTION_POSITION_SCALE_FACTOR, (float)run_rev.time_ms / 1000.0f);
    Serial.printf("Avg Speed: %.2f mm/s (%.1f mm/min)\n", speed_mm_s, speed_mm_min);

    AxisCalibration* cal = getAxisCalPtrForCli(axis);
    if (cal == NULL) {
        Serial.println("[CALIB] [ERR] Axis lookup failed.");
        return;
    }

    switch(profile) {
        case SPEED_PROFILE_1: cal->speed_slow_mm_min = speed_mm_min; break;
        case SPEED_PROFILE_2: cal->speed_med_mm_min = speed_mm_min; break;
        case SPEED_PROFILE_3: cal->speed_fast_mm_min = speed_mm_min; break;
    }
    
    saveAllCalibration(); 
    Serial.println("[CALIB] [OK] Calibration saved to NVS.");
}

// ============================================================================
// REGISTRATION FUNCTION
// ============================================================================
void cliRegisterCalibCommands() {
  cliRegisterCommand("calib", "Start automatic distance calibration (calib axis distance)", cmd_encoder_calib);
  cliRegisterCommand("calibrate speed", "Auto-detect and save profile speeds (calibrate speed X FAST 500)", cmd_auto_calibrate_speed);
  cliRegisterCommand("calibrate speed X reset", "Reset speed profiles to defaults (e.g., calibrate speed X reset)", cmd_encoder_reset);
  cliRegisterCommand("calibrate ppmm", "Start manual PPM measurement (calibrate ppmm X 1000)", cmd_calib_ppmm_start); 
  cliRegisterCommand("calibrate ppmm end", "Signal manual move end (calculate PPM)", cmd_calib_ppmm_end);
  cliRegisterCommand("calibrate ppmm X reset", "Reset PPM calibration to default (e.g., calibrate ppmm X reset)", cmd_calib_ppmm_reset); 
}