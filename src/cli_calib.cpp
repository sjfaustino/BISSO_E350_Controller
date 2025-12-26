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
#include "vfd_current_calibration.h"
#include "altivar31_modbus.h"
#include "config_unified.h"
#include "config_keys.h"
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
    logPrintln("[CLI] Usage: calib axis distance_mm (e.g., calib X 1000.0)");
    return;
  }
  uint8_t axis = axisCharToIndex(argv[1]);
  float distance_mm = 0.0f;

  if (axis >= 4) {
    logError("[CLI] Invalid axis. Use X, Y, Z, or A.");
    return;
  }
  if (!parseAndValidateFloat(argv[2], &distance_mm, 10.0f, 10000.0f)) {
      logError("[CLI] Invalid distance. Must be > 10.0mm.");
      return;
  }
  encoderCalibrationStart(axis, distance_mm);
}

void cmd_encoder_reset(int argc, char** argv) {
    if (argc < 4) {
        logPrintln("[CLI] Usage: calibrate speed reset [AXIS]");
        return;
    }
    uint8_t axis = axisCharToIndex(argv[2]);
    
    if (axis >= 4) {
        logError("[CLI] Invalid axis.");
        return;
    }
    
    AxisCalibration* cal = getAxisCalPtrForCli(axis);
    if (cal) {
        logPrintf("[CLI] Resetting speed profiles for Axis %c...\n", axisIndexToChar(axis));
        cal->speed_slow_mm_min = 300.0f; 
        cal->speed_med_mm_min = 900.0f;
        cal->speed_fast_mm_min = 2400.0f;
        saveAllCalibration();
        logInfo("[CLI] [OK] Speed profiles reset and saved.");
    } else {
         logError("[CLI] Calibration data not found for Axis %c.", axisIndexToChar(axis));
    }
}

void cmd_calib_ppmm_start(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("[CLI] Usage: calibrate ppmm [AXIS] [DISTANCE_MM]");
        return;
    }
    uint8_t axis = axisCharToIndex(argv[1]);
    char axis_char = axisIndexToChar(axis);
    float distance_mm = 0.0f;

    if (axis >= 4) {
        logError("[CLI] Invalid axis.");
        return;
    }
    if (!parseAndValidateFloat(argv[2], &distance_mm, 10.0f, 10000.0f)) {
        logError("[CLI] Invalid distance.");
        return;
    }
    if (g_manual_calib.state != CALIBRATION_IDLE) {
        logError("[CLI] Calibration already in progress.");
        return;
    }
    
    if (axis_char == 'A') {
        logWarning("[CALIB] Note: Axis A is rotational (Distance = Degrees).");
    }

    g_manual_calib.state = CALIB_MANUAL_START;
    g_manual_calib.axis = axis;
    g_manual_calib.target_mm = distance_mm;
    g_manual_calib.start_counts = wj66GetPosition(axis);
    
    serialLoggerLock();
    Serial.println("\n=== MANUAL PPM CALIBRATION ===");
    Serial.printf("Axis: %c | Target: %.1f mm\n", axis_char, distance_mm);
    Serial.printf("Start Pos: %ld counts\n", (long)g_manual_calib.start_counts);
    Serial.printf("\nACTION: Move axis exactly %.1f mm, then type 'calibrate ppmm end'.\n\n", distance_mm);
    serialLoggerUnlock();
    
    g_manual_calib.state = CALIB_MANUAL_WAIT_MOVE;
}

void cmd_calib_ppmm_end(int argc, char** argv) {
    if (g_manual_calib.state != CALIB_MANUAL_WAIT_MOVE) {
        logError("[CLI] No calibration in progress.");
        return;
    }

    uint8_t axis = g_manual_calib.axis;
    float target_mm = g_manual_calib.target_mm;
    int32_t end_counts = wj66GetPosition(axis);
    int32_t moved_counts = abs(end_counts - g_manual_calib.start_counts);
    
    if (moved_counts == 0) {
        logError("[CLI] No movement detected.");
        g_manual_calib.state = CALIBRATION_IDLE;
        return;
    }
    
    double calculated_ppmm = (double)moved_counts / target_mm;
    encoderCalibrationSetPPM(axis, calculated_ppmm); 
    
    serialLoggerLock();
    Serial.println("\n=== CALIBRATION COMPLETE ===");
    Serial.printf("Measured: %ld counts\n", (long)moved_counts);
    Serial.printf("Target:   %.1f mm\n", target_mm);
    Serial.printf("Result:   %.3f pulses/unit\n", calculated_ppmm);
    serialLoggerUnlock();
    
    g_manual_calib.state = CALIBRATION_IDLE;
}

void cmd_calib_ppmm_reset(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("[CLI] Usage: calibrate ppmm reset [AXIS]");
        return;
    }
    uint8_t axis = axisCharToIndex(argv[2]);

    if (axis >= 4) {
        logError("[CLI] Invalid axis.");
        return;
    }
    encoderCalibrationSetPPM(axis, (double)MOTION_POSITION_SCALE_FACTOR); 
    wj66Reset(); 
    logInfo("[CLI] [OK] PPM reset to default (%d) for Axis %c.", MOTION_POSITION_SCALE_FACTOR, axisIndexToChar(axis));
}

void cmd_auto_calibrate_speed(int argc, char** argv) {
    if (argc < 4) {
        logPrintln("[CLI] Usage: calibrate speed [AXIS] [PROFILE] [DISTANCE]");
        return;
    }

    uint8_t axis = axisCharToIndex(argv[1]);
    if (axis >= 4) {
        logError("[CLI] Invalid axis.");
        return;
    }

    speed_profile_t profile;
    if (strcmp(argv[2], "SLOW") == 0) profile = SPEED_PROFILE_1;
    else if (strcmp(argv[2], "MEDIUM") == 0) profile = SPEED_PROFILE_2;
    else if (strcmp(argv[2], "FAST") == 0) profile = SPEED_PROFILE_3;
    else {
        logError("[CLI] Invalid profile (SLOW/MEDIUM/FAST).");
        return;
    }

    float distance_mm = 0.0f;
    if (!parseAndValidateFloat(argv[3], &distance_mm, 50.0f, 10000.0f)) {
        logError("[CLI] Invalid distance (> 50.0).");
        return;
    }
    
    logPrintln("\n=== SPEED CALIBRATION SEQUENCE ===");
    logPrintf("Axis: %c | Profile: %s | Dist: %.1f mm\n", axisIndexToChar(axis), argv[2], distance_mm);
    
    calibration_run_t run_fwd = perform_single_measurement(axis, profile, distance_mm, true);
    if (run_fwd.time_ms == 0xFFFFFFFF) return; 

    calibration_run_t run_rev = perform_single_measurement(axis, profile, distance_mm, false);
    if (run_rev.time_ms == 0xFFFFFFFF) return;
    
    uint32_t total_time_ms = run_fwd.time_ms + run_rev.time_ms;
    int32_t total_counts = run_fwd.counts + run_rev.counts;
    
    if (total_time_ms == 0 || total_counts == 0) {
        logError("[CALIB] Invalid measurement data.");
        return;
    }

    float total_distance_mm = (float)total_counts / MOTION_POSITION_SCALE_FACTOR;
    float avg_time_s = (float)total_time_ms / 1000.0f;
    float speed_mm_s = total_distance_mm / avg_time_s;
    float speed_mm_min = speed_mm_s * 60.0f; 

    serialLoggerLock();
    Serial.println("\n--- SUMMARY ---");
    Serial.printf("Fwd: %.1f mm in %.2f s\n", (float)run_fwd.counts / MOTION_POSITION_SCALE_FACTOR, (float)run_fwd.time_ms / 1000.0f);
    Serial.printf("Rev: %.1f mm in %.2f s\n", (float)run_rev.counts / MOTION_POSITION_SCALE_FACTOR, (float)run_rev.time_ms / 1000.0f);
    Serial.printf("Avg Speed: %.2f mm/s (%.1f mm/min)\n", speed_mm_s, speed_mm_min);
    serialLoggerUnlock();

    AxisCalibration* cal = getAxisCalPtrForCli(axis);
    if (cal == NULL) {
        logError("[CALIB] Axis lookup failed.");
        return;
    }

    switch(profile) {
        case SPEED_PROFILE_1: cal->speed_slow_mm_min = speed_mm_min; break;
        case SPEED_PROFILE_2: cal->speed_med_mm_min = speed_mm_min; break;
        case SPEED_PROFILE_3: cal->speed_fast_mm_min = speed_mm_min; break;
    }
    
    saveAllCalibration(); 
    logInfo("[CALIB] [OK] Calibration saved to NVS.");
}

// ============================================================================
// REGISTRATION FUNCTION
// ============================================================================
// ============================================================================
// VFD CURRENT CALIBRATION (PHASE 5.5)
// ============================================================================

// CLI state machine for VFD calibration workflow
typedef enum {
    VFD_CALIB_IDLE,
    VFD_CALIB_MEASURING_IDLE,
    VFD_CALIB_CONFIRM_IDLE,
    VFD_CALIB_MEASURING_STD,
    VFD_CALIB_CONFIRM_STD,
    VFD_CALIB_MEASURING_HEAVY,
    VFD_CALIB_CONFIRM_HEAVY,
    VFD_CALIB_COMPLETE
} vfd_calib_state_t;

static vfd_calib_state_t vfd_calib_state = VFD_CALIB_IDLE;
static uint32_t vfd_calib_start_time = 0;
static const uint32_t MEASUREMENT_DURATION_MS = 10000;  // 10 seconds per phase

/**
 * @brief VFD current calibration command handler
 * Interactive three-phase workflow for operator calibration
 */
void cmd_vfd_calib_current(int argc, char** argv) {
    // Help text
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        serialLoggerLock();
        Serial.println("[VFDCAL] === VFD Current Calibration ===");
        Serial.println("Commands:");
        Serial.println("  calibrate vfd current start     - Start calibration workflow");
        Serial.println("  calibrate vfd current status    - Show current status");
        Serial.println("  calibrate vfd current confirm   - Confirm measurement and continue");
        Serial.println("  calibrate vfd current abort     - Abort calibration");
        Serial.println("  calibrate vfd current reset     - Reset all calibration data");
        Serial.println("  calibrate vfd current show      - Display current calibration values");
        serialLoggerUnlock();
        return;
    }

    // Parse subcommand
    if (strcmp(argv[1], "start") == 0) {
        if (vfd_calib_state != VFD_CALIB_IDLE) {
            logError("[VFDCAL] Calibration already in progress. Use 'abort' to restart.");
            return;
        }

        serialLoggerLock();
        Serial.println("\n[VFDCAL] === Starting VFD Current Calibration ===");
        Serial.println("This process measures current baselines for stall detection.");
        Serial.println("You will be guided through three phases:\n");
        Serial.println("1. IDLE BASELINE: Blade spinning, NO cutting (typically 5-10A)");
        Serial.println("2. STANDARD CUT: Reference cutting speed (typically 20-25A)");
        Serial.println("3. HEAVY LOAD: (Optional) High-speed or high-load cutting\n");
        Serial.println("Each phase will measure for 10 seconds. Press ENTER when ready for phase 1...");
        serialLoggerUnlock();

        vfd_calib_state = VFD_CALIB_MEASURING_IDLE;
        vfd_calib_start_time = millis();
        logPrintln("[VFDCAL] Phase 1: Measuring IDLE BASELINE (10 seconds)");
        logPrintln(">> Spin blade with NO cutting load, then wait for completion <<");
        vfdCalibrationStartMeasure(MEASUREMENT_DURATION_MS, "Idle Baseline");

    } else if (strcmp(argv[1], "confirm") == 0) {
        float rms, peak;

        if (vfd_calib_state == VFD_CALIB_MEASURING_IDLE && vfdCalibrationIsMeasureComplete()) {
            if (vfdCalibrationGetMeasurement(&rms, &peak)) {
                vfdCalibrationStoreMeasurement(0, rms, peak);
                logPrintf("[VFDCAL] Idle phase complete: RMS=%.2f A, Peak=%.2f A\n", rms, peak);
                logPrintln("[VFDCAL] Phase 2: Measuring STANDARD CUT (10 seconds)");
                logPrintln(">> Perform cutting at standard reference speed, then wait <<");
                vfd_calib_state = VFD_CALIB_MEASURING_STD;
                vfd_calib_start_time = millis();
                vfdCalibrationStartMeasure(MEASUREMENT_DURATION_MS, "Standard Cut");
            }

        } else if (vfd_calib_state == VFD_CALIB_MEASURING_STD && vfdCalibrationIsMeasureComplete()) {
            if (vfdCalibrationGetMeasurement(&rms, &peak)) {
                vfdCalibrationStoreMeasurement(1, rms, peak);
                logPrintf("[VFDCAL] Standard cut phase complete: RMS=%.2f A, Peak=%.2f A\n", rms, peak);
                serialLoggerLock();
                Serial.println("\n[VFDCAL] Phase 3: HEAVY LOAD (Optional)");
                Serial.println("Measure heavy-load scenario for worst-case baseline?");
                Serial.println("  - Type 'continue' to measure heavy load (10 seconds)");
                Serial.println("  - Type 'finish' to skip and calculate thresholds");
                serialLoggerUnlock();
                vfd_calib_state = VFD_CALIB_CONFIRM_STD;
            }

        } else if (vfd_calib_state == VFD_CALIB_MEASURING_HEAVY && vfdCalibrationIsMeasureComplete()) {
            if (vfdCalibrationGetMeasurement(&rms, &peak)) {
                vfdCalibrationStoreMeasurement(2, rms, peak);
                logPrintf("[VFDCAL] Heavy load phase complete: RMS=%.2f A, Peak=%.2f A\n", rms, peak);
                logPrintln("\n[VFDCAL] Calculating stall detection threshold...");
                if (vfdCalibrationCalculateThreshold(20.0f)) {
                    logPrintf("[VFDCAL] Stall threshold set to: %.2f A\n", vfdCalibrationGetThreshold());
                    vfdCalibrationPrintSummary();
                    vfd_calib_state = VFD_CALIB_COMPLETE;
                    logInfo("[VFDCAL] Calibration COMPLETE and saved!");
                }
            }

        } else {
            logError("[VFDCAL] No measurement in progress or measurement not complete yet.");
        }

    } else if (strcmp(argv[1], "continue") == 0) {
        if (vfd_calib_state == VFD_CALIB_CONFIRM_STD) {
            logPrintln("[VFDCAL] Phase 3: Measuring HEAVY LOAD (10 seconds)");
            logPrintln(">> Perform heavy-load cutting scenario, then wait <<");
            vfd_calib_state = VFD_CALIB_MEASURING_HEAVY;
            vfdCalibrationStartMeasure(MEASUREMENT_DURATION_MS, "Heavy Load");
        } else {
            logError("[VFDCAL] Not in phase confirmation state.");
        }

    } else if (strcmp(argv[1], "finish") == 0) {
        if (vfd_calib_state == VFD_CALIB_CONFIRM_STD) {
            logPrintln("[VFDCAL] Skipping heavy load measurement...");
            logPrintln("[VFDCAL] Calculating stall detection threshold...");
            if (vfdCalibrationCalculateThreshold(20.0f)) {
                logPrintf("[VFDCAL] Stall threshold set to: %.2f A\n", vfdCalibrationGetThreshold());
                vfdCalibrationPrintSummary();
                vfd_calib_state = VFD_CALIB_COMPLETE;
                logInfo("[VFDCAL] Calibration COMPLETE and saved!");
            }
        } else {
            logError("[VFDCAL] Not in phase confirmation state.");
        }

    } else if (strcmp(argv[1], "abort") == 0) {
        logWarning("[VFDCAL] Calibration aborted. Use 'start' to begin again.");
        vfd_calib_state = VFD_CALIB_IDLE;

    } else if (strcmp(argv[1], "reset") == 0) {
        logWarning("[VFDCAL] Resetting all VFD calibration data!");
        vfdCalibrationReset();
        vfd_calib_state = VFD_CALIB_IDLE;
        logInfo("[VFDCAL] All calibration data cleared.");

    } else if (strcmp(argv[1], "status") == 0) {
        const char* state_names[] = {
            "IDLE", "MEASURING_IDLE", "CONFIRM_IDLE", "MEASURING_STD",
            "CONFIRM_STD", "MEASURING_HEAVY", "CONFIRM_HEAVY", "COMPLETE"
        };
        logPrintf("[VFDCAL] Current state: %s\n", state_names[vfd_calib_state]);
        vfdCalibrationPrintSummary();

    } else if (strcmp(argv[1], "show") == 0) {
        vfdCalibrationPrintSummary();

    } else {
        logWarning("[VFDCAL] Unknown subcommand. Use 'help' for usage.");
    }
}

// ============================================================================
// VFD DIAGNOSTICS (PHASE 5.5)
// ============================================================================

/**
 * @brief VFD diagnostics command handler
 * Display real-time VFD telemetry and health information
 */
void cmd_vfd_diagnostics(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        serialLoggerLock();
        Serial.println("[VFDDIAG] === VFD Diagnostics ===");
        Serial.println("Commands:");
        Serial.println("  vfd diagnostics status    - Show real-time VFD status");
        Serial.println("  vfd diagnostics thermal   - Show thermal monitoring details");
        Serial.println("  vfd diagnostics current   - Show motor current measurements");
        Serial.println("  vfd diagnostics frequency - Show output frequency data");
        Serial.println("  vfd diagnostics full      - Comprehensive VFD report");
        Serial.println("  vfd diagnostics calib     - Show calibration details");
        serialLoggerUnlock();
        return;
    }

    // Real-time status snapshot
    if (strcmp(argv[1], "status") == 0) {
        logPrintln("\n[VFDDIAG] === VFD Real-Time Status ===");
        altivar31PrintDiagnostics();

    } else if (strcmp(argv[1], "thermal") == 0) {
        logPrintln("\n[VFDDIAG] === Thermal Monitoring ===");
        int16_t thermal = altivar31GetThermalState();
        int32_t warn = configGetInt(KEY_VFD_TEMP_WARN, 85);
        int32_t crit = configGetInt(KEY_VFD_TEMP_CRIT, 90);

        serialLoggerLock();
        Serial.printf("Thermal State:       %d%% (nominal: 100%%)\n", thermal);
        Serial.printf("Warning Threshold:   >%ld°C (%ld%% state)\n", (long)warn, (long)(warn * 1.3));
        Serial.printf("Critical Threshold:  >%ld°C (%ld%% state)\n", (long)crit, (long)(crit * 1.4));

        if (thermal > (crit * 1.4)) {
            Serial.println("Status:              CRITICAL - Emergency stop required!");
        } else if (thermal > (warn * 1.3)) {
            Serial.println("Status:              WARNING - Monitor closely");
        } else {
            Serial.println("Status:              NORMAL");
        }
        Serial.println();
        serialLoggerUnlock();

    } else if (strcmp(argv[1], "current") == 0) {
        logPrintln("\n[VFDDIAG] === Motor Current Measurements ===");
        float current = altivar31GetCurrentAmps();
        int16_t raw = altivar31GetCurrentRaw();

        serialLoggerLock();
        Serial.printf("Motor Current:       %.2f A (raw: %d)\n", current, raw);

        if (vfdCalibrationIsValid()) {
            const vfd_calibration_data_t* calib = vfdCalibrationGetData();
            Serial.printf("\nCalibrated Baselines:\n");
            Serial.printf("  Idle (no cut):       %.2f A (RMS) / %.2f A (peak)\n",
                          calib->idle_rms_amps, calib->idle_peak_amps);
            Serial.printf("  Standard Cut:        %.2f A (RMS) / %.2f A (peak)\n",
                          calib->standard_cut_rms_amps, calib->standard_cut_peak_amps);
            if (calib->heavy_cut_rms_amps > 0.0f) {
                Serial.printf("  Heavy Load:          %.2f A (RMS) / %.2f A (peak)\n",
                              calib->heavy_cut_rms_amps, calib->heavy_cut_peak_amps);
            }
            Serial.printf("\nStall Detection:\n");
            Serial.printf("  Threshold:           %.2f A\n", calib->stall_threshold_amps);
            Serial.printf("  Current vs Threshold: %.2f A / %.2f A = %.0f%%\n",
                          current, calib->stall_threshold_amps,
                          (calib->stall_threshold_amps > 0) ? (current / calib->stall_threshold_amps * 100.0f) : 0.0f);

            if (vfdCalibrationIsStall(current)) {
                Serial.println("  Status:              STALL DETECTED!");
            } else {
                Serial.println("  Status:              Normal");
            }
        } else {
            Serial.println("  Calibration Status:  NOT CALIBRATED");
        }
        Serial.println();
        serialLoggerUnlock();

    } else if (strcmp(argv[1], "frequency") == 0) {
        logPrintln("\n[VFDDIAG] === Output Frequency ===");
        float freq = altivar31GetFrequencyHz();
        int16_t raw = altivar31GetFrequencyRaw();

        logPrintf("Output Frequency:    %.1f Hz (raw: %d, 0.1Hz/unit)\n", freq, raw);
        logPrintf("Status:              %s\n", freq > 0.0f ? "RUNNING" : "IDLE/STOPPED");

    } else if (strcmp(argv[1], "calib") == 0) {
        logPrintln("\n[VFDDIAG] === Calibration Details ===");
        vfdCalibrationPrintSummary();

    } else if (strcmp(argv[1], "full") == 0) {
        serialLoggerLock();
        Serial.println("\n[VFDDIAG] === Comprehensive VFD Report ===");
        Serial.println("\n--- Status ---");
        serialLoggerUnlock();
        altivar31PrintDiagnostics();

        serialLoggerLock();
        Serial.println("\n--- Current Measurements ---");
        float current = altivar31GetCurrentAmps();
        int32_t raw = altivar31GetCurrentRaw();
        Serial.printf("Motor Current:       %.2f A (raw: %ld)\n", current, (long)raw);

        Serial.println("\n--- Thermal State ---");
        int16_t thermal = altivar31GetThermalState();
        int32_t warn = configGetInt(KEY_VFD_TEMP_WARN, 85);
        int32_t crit = configGetInt(KEY_VFD_TEMP_CRIT, 90);
        Serial.printf("Thermal State:       %d%% (warn: %ld%%, crit: %ld%%)\n",
                      thermal, (long)(warn * 1.3), (long)(crit * 1.4));

        Serial.println("\n--- Frequency ---");
        float freq = altivar31GetFrequencyHz();
        Serial.printf("Output Frequency:    %.1f Hz\n", freq);
        serialLoggerUnlock();

        logPrintln("\n--- Calibration ---");
        vfdCalibrationPrintSummary();

        logPrintln("\n--- Configuration ---");
        float margin = configGetFloat(KEY_VFD_STALL_MARGIN, 20.0f);
        int32_t timeout = configGetInt(KEY_STALL_TIMEOUT, 2000);
        logPrintf("Stall Margin:        %.0f%%\n", margin);
        logPrintf("Stall Timeout:       %ld ms\n", (long)timeout);

    } else {
        logWarning("[VFDDIAG] Unknown subcommand. Use 'help' for usage.");
    }
}

// ============================================================================
// VFD CONFIGURATION & STALL RESPONSE CUSTOMIZATION (PHASE 5.5)
// ============================================================================

/**
 * @brief VFD configuration command handler
 * Customize stall detection thresholds, margins, and thermal limits
 */
void cmd_vfd_config(int argc, char** argv) {
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        serialLoggerLock();
        Serial.println("[VFDCFG] === VFD Configuration ===");
        Serial.println("Commands:");
        Serial.println("  vfd config margin <percent>      - Set stall margin (default 20%)");
        Serial.println("  vfd config timeout <ms>          - Set stall timeout (default 2000ms)");
        Serial.println("  vfd config temp warn <C>         - Set temperature warning threshold");
        Serial.println("  vfd config temp crit <C>         - Set temperature critical threshold");
        Serial.println("  vfd config enable <on|off>       - Enable/disable VFD stall detection");
        Serial.println("  vfd config show                  - Display current settings");
        serialLoggerUnlock();
        return;
    }

    if (strcmp(argv[1], "margin") == 0) {
        if (argc < 3) {
            logPrintln("[VFDCFG] Usage: vfd config margin <percent>");
            return;
        }
        float margin = atof(argv[2]);
        if (margin < 5.0f || margin > 100.0f) {
            logError("[VFDCFG] Margin must be between 5%% and 100%%");
            return;
        }
        configSetInt(KEY_VFD_STALL_MARGIN, (int32_t)margin);
        configUnifiedFlush();
        configUnifiedSave();
        logInfo("[VFDCFG] Stall margin set to %.0f%%", margin);

    } else if (strcmp(argv[1], "timeout") == 0) {
        if (argc < 3) {
            logPrintln("[VFDCFG] Usage: vfd config timeout <milliseconds>");
            return;
        }
        uint32_t timeout_ms = (uint32_t)atoi(argv[2]);
        if (timeout_ms < 100 || timeout_ms > 60000) {
            logError("[VFDCFG] Timeout must be between 100ms and 60000ms");
            return;
        }
        configSetInt(KEY_STALL_TIMEOUT, (int32_t)timeout_ms);
        configUnifiedFlush();
        configUnifiedSave();
        logInfo("[VFDCFG] Stall timeout set to %lu ms", (unsigned long)timeout_ms);

    } else if (strcmp(argv[1], "temp") == 0) {
        if (argc < 4) {
            logPrintln("[VFDCFG] Usage: vfd config temp [warn|crit] <C>");
            return;
        }

        int32_t temp = (int32_t)atoi(argv[3]);
        if (temp < 0 || temp > 150) {
            logError("[VFDCFG] Temperature must be between 0C and 150C");
            return;
        }

        if (strcmp(argv[2], "warn") == 0) {
            configSetInt(KEY_VFD_TEMP_WARN, temp);
            logInfo("[VFDCFG] Temperature warning threshold set to %ldC", (long)temp);
        } else if (strcmp(argv[2], "crit") == 0) {
            configSetInt(KEY_VFD_TEMP_CRIT, temp);
            logInfo("[VFDCFG] Temperature critical threshold set to %ldC", (long)temp);
        } else {
            logError("[VFDCFG] Use 'warn' or 'crit'");
            return;
        }
        configUnifiedFlush();
        configUnifiedSave();

    } else if (strcmp(argv[1], "enable") == 0) {
        if (argc < 3) {
            logPrintln("[VFDCFG] Usage: vfd config enable [on|off]");
            return;
        }

        bool enable = (strcmp(argv[2], "on") == 0) || (strcmp(argv[2], "1") == 0);
        configSetInt("vfd_stall_detect", enable ? 1 : 0);
        configUnifiedFlush();
        configUnifiedSave();
        logInfo("[VFDCFG] VFD stall detection %s", enable ? "ENABLED" : "DISABLED");

    } else if (strcmp(argv[1], "show") == 0) {
        logPrintln("\n[VFDCFG] === Current VFD Configuration ===");

        float margin = configGetFloat(KEY_VFD_STALL_MARGIN, 20.0f);
        logPrintf("Stall Margin:        %.0f%%\n", margin);

        int32_t timeout = configGetInt(KEY_STALL_TIMEOUT, 2000);
        logPrintf("Stall Timeout:       %ld ms\n", (long)timeout);

        int32_t temp_warn = configGetInt(KEY_VFD_TEMP_WARN, 85);
        int32_t temp_crit = configGetInt(KEY_VFD_TEMP_CRIT, 90);
        logPrintf("Temperature Warn:    %ldC\n", (long)temp_warn);
        logPrintf("Temperature Crit:    %ldC\n", (long)temp_crit);

        int32_t enabled = configGetInt("vfd_stall_detect", 1);
        logPrintf("VFD Stall Detect:    %s\n", enabled ? "ENABLED" : "DISABLED");

        // Show calibration status
        const vfd_calibration_data_t* calib = vfdCalibrationGetData();
        if (calib->is_calibrated) {
            logPrintf("Stall Threshold:     %.2f A (margin: %.0f%%)\n",
                          calib->stall_threshold_amps, calib->stall_margin_percent);
        } else {
            logPrintln("Stall Threshold:     NOT CALIBRATED");
        }

    } else {
        logWarning("[VFDCFG] Unknown subcommand. Use 'help' for usage.");
    }
}

void cliRegisterCalibCommands() {
  cliRegisterCommand("calib", "Start automatic distance calibration (calib axis distance)", cmd_encoder_calib);
  cliRegisterCommand("calibrate speed", "Auto-detect and save profile speeds (calibrate speed X FAST 500)", cmd_auto_calibrate_speed);
  cliRegisterCommand("calibrate speed X reset", "Reset speed profiles to defaults (e.g., calibrate speed X reset)", cmd_encoder_reset);
  cliRegisterCommand("calibrate ppmm", "Start manual PPM measurement (calibrate ppmm X 1000)", cmd_calib_ppmm_start);
  cliRegisterCommand("calibrate ppmm end", "Signal manual move end (calculate PPM)", cmd_calib_ppmm_end);
  cliRegisterCommand("calibrate ppmm X reset", "Reset PPM calibration to default (e.g., calibrate ppmm X reset)", cmd_calib_ppmm_reset);
  cliRegisterCommand("calibrate vfd current", "VFD motor current calibration workflow (calibrate vfd current start)", cmd_vfd_calib_current);
  cliRegisterCommand("vfd diagnostics", "VFD telemetry and health diagnostics (vfd diagnostics help)", cmd_vfd_diagnostics);
  cliRegisterCommand("vfd config", "Configure VFD stall detection and thermal limits (vfd config help)", cmd_vfd_config);
}