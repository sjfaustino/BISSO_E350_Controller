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

// ============================================================================
// LOCAL TYPE DEFINITIONS
// ============================================================================

typedef struct {
    uint32_t time_ms;
    int32_t counts;
} calibration_run_t;

// ============================================================================
// FORWARD DECLARATIONS 
// ============================================================================

// --- NEW/FIX: Local helper to get the correct axis pointer ---
AxisCalibration* getAxisCalPtrForCli(uint8_t axis) {
    if (axis == 0) return &machineCal.X;
    if (axis == 1) return &machineCal.Y;
    if (axis == 2) return &machineCal.Z;
    if (axis == 3) return &machineCal.A;
    return NULL;
}
// -----------------------------------------------------------

void saveAllCalibration(); 
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus_direction); 
void motionSetPLCSpeedProfile(speed_profile_t profile); 
void encoderCalibrationSetPPM(uint8_t axis, double ppm); 
void encoderCalibrationDiagnostics();
bool encoderCalibrationStart(uint8_t axis, float distance_mm); 
void encoderCalibrationReset(uint8_t axis);
void encoderMotionDiagnostics();


// ============================================================================
// MODULAR COMMAND PROTOTYPES
// ============================================================================

void cmd_encoder_calib(int argc, char** argv);
void cmd_encoder_reset(int argc, char** argv);
void cmd_calib_ppmm_start(int argc, char** argv); 
void cmd_calib_ppmm_end(int argc, char** argv);
void cmd_calib_ppmm_reset(int argc, char** argv);
void cmd_auto_calibrate_speed(int argc, char** argv);
calibration_run_t perform_single_measurement(uint8_t axis, speed_profile_t profile, float distance_mm, bool is_forward);


// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterCalibCommands() {
  cliRegisterCommand("calib", "Start automatic distance calibration (calib axis distance)", cmd_encoder_calib);
  cliRegisterCommand("calibrate speed", "Auto-detect and save profile speeds (calibrate speed X FAST 500)", cmd_auto_calibrate_speed);
  cliRegisterCommand("calibrate speed X reset", "Reset speed profiles to defaults (e.g., calibrate speed X reset)", cmd_encoder_reset);
  cliRegisterCommand("calibrate ppmm", "Start manual PPM measurement (calibrate ppmm X 1000)", cmd_calib_ppmm_start); 
  cliRegisterCommand("calibrate ppmm end", "Signal manual move end (calculate PPM)", cmd_calib_ppmm_end);
  cliRegisterCommand("calibrate ppmm X reset", "Reset PPM calibration to default (e.g., calibrate ppmm X reset)", cmd_calib_ppmm_reset); 
}

// ============================================================================
// CORE IMPLEMENTATION: SPEED MEASUREMENT HELPER
// ============================================================================

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
        logError("[CALIB] ERROR: Timeout on %s move. Measured: %ld counts.", is_forward ? "FORWARD" : "REVERSE", run.counts);
        faultLogError(FAULT_CALIBRATION_MISSING, "Speed calibration failed: Timeout");
        run.time_ms = 0xFFFFFFFF; 
    }

    return run;
}


// ============================================================================
// COMMAND IMPLEMENTATIONS (FIXED AXIS LOGIC)
// ============================================================================

void cmd_encoder_calib(int argc, char** argv) {
  if (argc < 3) {
    Serial.println("[CLI] Usage: calib axis distance_mm (e.g., calib X 1000.0)");
    return;
  }
  
  uint8_t axis = axisCharToIndex(argv[1]);
  float distance_mm = 0.0f;

  if (axis >= 4) {
    Serial.println("[CLI] ERROR: Invalid axis. Use X, Y, Z, or A.");
    return;
  }
  
  if (!parseAndValidateFloat(argv[2], &distance_mm, 10.0f, 10000.0f)) {
      Serial.println("[CLI] ERROR: Invalid distance. Must be a number > 10.0mm.");
      return;
  }
  
  encoderCalibrationStart(axis, distance_mm);
}

void cmd_encoder_reset(int argc, char** argv) {
    // Command: calibrate speed X reset 
    if (argc < 4) {
        Serial.println("[CLI] Usage: calibrate speed reset [AXIS]");
        return;
    }
    
    uint8_t axis = axisCharToIndex(argv[2]);
    
    if (axis >= 4) {
        Serial.println("[CLI] ERROR: Invalid axis. Use X, Y, Z, or A.");
        return;
    }
    
    // --- FIX: Use helper to get the correct axis pointer ---
    AxisCalibration* cal = getAxisCalPtrForCli(axis);
    
    if (cal) {
        Serial.printf("[CLI] Resetting all speed profiles for Axis %c to factory defaults...\n", axisIndexToChar(axis));
        cal->speed_slow_mm_min = 300.0f; 
        cal->speed_med_mm_min = 900.0f;
        cal->speed_fast_mm_min = 2400.0f;
        saveAllCalibration();
        Serial.printf("[CLI] ✅ Speed profiles for Axis %c reset and saved to NVS.\n", axisIndexToChar(axis));
    } else {
         Serial.printf("[CLI] ERROR: Failed to find calibration data for Axis %c.\n", axisIndexToChar(axis));
    }
    // -----------------------------------------------------
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
        Serial.println("[CLI] ERROR: Invalid axis. Use X, Y, Z, or A.");
        return;
    }
    
    if (!parseAndValidateFloat(argv[2], &distance_mm, 10.0f, 10000.0f)) {
        Serial.println("[CLI] ERROR: Invalid distance. Must be a number > 10.0mm.");
        return;
    }
    
    if (g_manual_calib.state != CALIBRATION_IDLE) {
        Serial.println("[CLI] ERROR: Calibration already in progress. Abort first.");
        return;
    }
    
    if (axis_char == 'A') {
        logWarning("[CALIB] WARNING: Axis A is rotational. Ensure %.1f is the distance traveled in DEGREES.", distance_mm);
    }

    g_manual_calib.state = CALIB_MANUAL_START;
    g_manual_calib.axis = axis;
    g_manual_calib.target_mm = distance_mm;
    g_manual_calib.start_counts = wj66GetPosition(axis);
    
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.printf("║ MANUAL PPM CALIBRATION - AXIS %c ║\n", axis_char);
    Serial.println("╚════════════════════════════════════════╝");
    
    Serial.printf("[CALIB] Target Distance: %.1f mm\n", distance_mm);
    Serial.printf("[CALIB] Starting Position: %ld counts (%.1f mm)\n", 
                  g_manual_calib.start_counts, (float)g_manual_calib.start_counts / MOTION_POSITION_SCALE_FACTOR);
    
    Serial.println("\n*** ACTION REQUIRED ***");
    Serial.printf("Please manually move Axis %c exactly %.1f mm.\n", axis_char, distance_mm);
    Serial.println("Once the movement is complete, type: calibrate ppmm end");
    
    g_manual_calib.state = CALIB_MANUAL_WAIT_MOVE;
}


void cmd_calib_ppmm_end(int argc, char** argv) {
    if (g_manual_calib.state != CALIB_MANUAL_WAIT_MOVE) {
        Serial.println("[CLI] ERROR: No manual PPM calibration in progress.");
        return;
    }

    uint8_t axis = g_manual_calib.axis;
    float target_mm = g_manual_calib.target_mm;
    
    int32_t end_counts = wj66GetPosition(axis);
    int32_t moved_counts = abs(end_counts - g_manual_calib.start_counts);
    
    if (moved_counts == 0) {
        Serial.println("[CLI] ERROR: No movement detected (0 counts moved).");
        g_manual_calib.state = CALIBRATION_IDLE;
        return;
    }
    
    double calculated_ppmm = (double)moved_counts / target_mm;
    
    encoderCalibrationSetPPM(axis, calculated_ppmm); 
    
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║        PPM CALCULATION COMPLETE        ║");
    Serial.println("╚════════════════════════════════════════╝");
    
    Serial.printf("[RESULT] Measured Counts: %ld\n", moved_counts);
    Serial.printf("[RESULT] Target Distance: %.1f mm\n", target_mm);
    Serial.printf("[RESULT] Calibrated PPM: %.3f pulses/mm\n", calculated_ppmm);
    
    g_manual_calib.state = CALIBRATION_IDLE;
}

void cmd_calib_ppmm_reset(int argc, char** argv) {
    // Command: calibrate ppmm X reset 
    if (argc < 3) {
        Serial.println("[CLI] Usage: calibrate ppmm reset [AXIS]");
        return;
    }
    
    uint8_t axis = axisCharToIndex(argv[2]);

    if (axis >= 4) {
        Serial.println("[CLI] ERROR: Invalid axis. Use X, Y, Z, or A.");
        return;
    }
    
    encoderCalibrationSetPPM(axis, (double)MOTION_POSITION_SCALE_FACTOR); 

    wj66Reset(); 
    
    Serial.printf("[CLI] ✅ PPM calibration for Axis %c reset to %lu pulses/mm and encoder position reset to 0.\n", axisIndexToChar(axis), MOTION_POSITION_SCALE_FACTOR);
}


void cmd_auto_calibrate_speed(int argc, char** argv) {
    // Command: calibrate speed X FAST 500
    if (argc < 4) {
        Serial.println("[CLI] Usage: calibrate speed [AXIS] [PROFILE:SLOW/MEDIUM/FAST] [DISTANCE_MM]");
        return;
    }

    uint8_t axis = axisCharToIndex(argv[1]);
    char axis_char = axisIndexToChar(axis);
    if (axis >= 4) {
        Serial.println("[CLI] ERROR: Invalid axis. Use X, Y, Z, or A.");
        return;
    }

    speed_profile_t profile;
    if (strcmp(argv[2], "SLOW") == 0) {
        profile = SPEED_PROFILE_1;
    } else if (strcmp(argv[2], "MEDIUM") == 0) {
        profile = SPEED_PROFILE_2;
    } else if (strcmp(argv[2], "FAST") == 0) {
        profile = SPEED_PROFILE_3;
    } else {
        Serial.println("[CLI] ERROR: Invalid profile. Use SLOW, MEDIUM, or FAST.");
        return;
    }

    float distance_mm = 0.0f;
    if (!parseAndValidateFloat(argv[3], &distance_mm, 50.0f, 10000.0f)) {
        Serial.println("[CLI] ERROR: Invalid distance. Must be > 50.0mm.");
        return;
    }
    
    Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
    Serial.printf("║   TWO-WAY SPEED CALIBRATION - AXIS %c, PROFILE %s   ║\n", axis_char, argv[2]);
    Serial.println("╚════════════════════════════════════════════════════════════════╝");
    
    calibration_run_t run_fwd = perform_single_measurement(axis, profile, distance_mm, true);
    if (run_fwd.time_ms == 0xFFFFFFFF) return; 

    calibration_run_t run_rev = perform_single_measurement(axis, profile, distance_mm, false);
    if (run_rev.time_ms == 0xFFFFFFFF) return;
    
    uint32_t total_time_ms = run_fwd.time_ms + run_rev.time_ms;
    int32_t total_counts = run_fwd.counts + run_rev.counts;
    
    if (total_time_ms == 0 || total_counts == 0) {
        Serial.println("[CALIB] ERROR: Total motion time or distance was zero. Aborting.");
        return;
    }

    float total_distance_mm = (float)total_counts / MOTION_POSITION_SCALE_FACTOR;
    float avg_time_s = (float)total_time_ms / 1000.0f;
    float speed_mm_s = total_distance_mm / avg_time_s;
    float speed_mm_min = speed_mm_s * 60.0f; 

    Serial.println("\n📊 CALIBRATION SUMMARY:");
    Serial.printf("  Forward: %.1f mm in %.2f s\n", (float)run_fwd.counts / MOTION_POSITION_SCALE_FACTOR, (float)run_fwd.time_ms / 1000.0f);
    Serial.printf("  Reverse: %.1f mm in %.2f s\n", (float)run_rev.counts / MOTION_POSITION_SCALE_FACTOR, (float)run_rev.time_ms / 1000.0f);
    Serial.printf("  Average Speed: %.2f mm/s (%.1f mm/min)\n", speed_mm_s, speed_mm_min);

    // --- FIX: Use helper to get the correct axis pointer and assign speed profile ---
    AxisCalibration* cal = getAxisCalPtrForCli(axis);
    
    if (cal == NULL) {
        Serial.println("[CALIB] ERROR: Internal axis pointer lookup failed for saving.");
        return;
    }

    switch(profile) {
        case SPEED_PROFILE_1: cal->speed_slow_mm_min = speed_mm_min; break;
        case SPEED_PROFILE_2: cal->speed_med_mm_min = speed_mm_min; break;
        case SPEED_PROFILE_3: cal->speed_fast_mm_min = speed_mm_min; break;
    }
    // -------------------------------------------------------------------------------
    
    saveAllCalibration(); 
    Serial.println("✅ Calibration successful and values saved to NVS.");
}