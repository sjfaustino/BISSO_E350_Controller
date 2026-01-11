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
    // Subcommand dispatch: calibrate speed reset X
    // argv[0]=calibrate, argv[1]=speed, argv[2]=reset, argv[3]=X
    uint8_t axis = axisCharToIndex(argv[3]);
    
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
    if (argc < 4) {
        logPrintln("[CLI] Usage: calibrate ppmm [AXIS] [DISTANCE_MM]");
        return;
    }
    // argv[0]=calibrate, argv[1]=ppmm, argv[2]=AXIS, argv[3]=DIST
    uint8_t axis = axisCharToIndex(argv[2]);
    char axis_char = axisIndexToChar(axis);
    float distance_mm = 0.0f;

    if (axis >= 4) {
        logError("[CLI] Invalid axis.");
        return;
    }
    if (!parseAndValidateFloat(argv[3], &distance_mm, 10.0f, 10000.0f)) {
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
    
    logPrintln("\n=== MANUAL PPM CALIBRATION ===");
    logPrintf("Axis: %c | Target: %.1f mm\r\n", axis_char, distance_mm);
    logPrintf("Start Pos: %ld counts\r\n", (long)g_manual_calib.start_counts);
    logPrintf("\r\nACTION: Move axis exactly %.1f mm, then type 'calibrate ppmm end'.\r\n\r\n", distance_mm);
    
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
    
    logPrintln("\n=== CALIBRATION COMPLETE ===");
    logPrintf("Measured: %ld counts\r\n", (long)moved_counts);
    logPrintf("Target:   %.1f mm\r\n", target_mm);
    logPrintf("Result:   %.3f pulses/unit\r\n", calculated_ppmm);
    
    g_manual_calib.state = CALIBRATION_IDLE;
}

void cmd_calib_ppmm_reset(int argc, char** argv) {
    if (argc < 4) {
        logPrintln("[CLI] Usage: calibrate ppmm reset [AXIS]");
        return;
    }
    // argv[0]=calibrate, argv[1]=ppmm, argv[2]=reset, argv[3]=AXIS
    uint8_t axis = axisCharToIndex(argv[3]);

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
        logPrintln("       calibrate speed reset [AXIS]");
        return;
    }

    // Handle reset subcommand: calibrate speed reset X
    if (strcmp(argv[2], "reset") == 0) {
        cmd_encoder_reset(argc, argv);
        return;
    }

    if (argc < 5) {
        logPrintln("[CLI] Usage: calibrate speed [AXIS] [PROFILE] [DISTANCE]");
        return;
    }
    // argv[0]=calibrate, argv[1]=speed, argv[2]=AXIS, argv[3]=PROFILE, argv[4]=DISTANCE
    uint8_t axis = axisCharToIndex(argv[2]);
    if (axis >= 4) {
        logError("[CLI] Invalid axis.");
        return;
    }

    speed_profile_t profile;
    if (strcmp(argv[3], "SLOW") == 0) profile = SPEED_PROFILE_1;
    else if (strcmp(argv[3], "MEDIUM") == 0) profile = SPEED_PROFILE_2;
    else if (strcmp(argv[3], "FAST") == 0) profile = SPEED_PROFILE_3;
    else {
        logError("[CLI] Invalid profile (SLOW/MEDIUM/FAST).");
        return;
    }

    float distance_mm = 0.0f;
    if (!parseAndValidateFloat(argv[4], &distance_mm, 50.0f, 10000.0f)) {
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

    logPrintln("\n--- SUMMARY ---");
    logPrintf("Fwd: %.1f mm in %.2f s\r\n", (float)run_fwd.counts / MOTION_POSITION_SCALE_FACTOR, (float)run_fwd.time_ms / 1000.0f);
    logPrintf("Rev: %.1f mm in %.2f s\r\n", (float)run_rev.counts / MOTION_POSITION_SCALE_FACTOR, (float)run_rev.time_ms / 1000.0f);
    logPrintf("Avg Speed: %.2f mm/s (%.1f mm/min)\r\n", speed_mm_s, speed_mm_min);

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
    // argv[0]=calibrate, argv[1]=vfd, argv[2]=current, argv[3]=SUB
    if (argc < 4 || strcmp(argv[3], "help") == 0) {
        logPrintln("[VFDCAL] === VFD Current Calibration ===");
        logPrintln("Commands:");
        logPrintln("  calibrate vfd current start     - Start calibration workflow");
        logPrintln("  calibrate vfd current status    - Show current status");
        logPrintln("  calibrate vfd current confirm   - Confirm measurement and continue");
        logPrintln("  calibrate vfd current abort     - Abort calibration");
        logPrintln("  calibrate vfd current reset     - Reset all calibration data");
        logPrintln("  calibrate vfd current show      - Display current calibration values");
        return;
    }

    // Parse subcommand
    if (strcmp(argv[3], "start") == 0) {
        if (vfd_calib_state != VFD_CALIB_IDLE) {
            logError("[VFDCAL] Calibration already in progress. Use 'abort' to restart.");
            return;
        }

        logPrintln("\n[VFDCAL] === Starting VFD Current Calibration ===");
        logPrintln("This process measures current baselines for stall detection.");
        logPrintln("You will be guided through three phases:\n");
        logPrintln("1. IDLE BASELINE: Blade spinning, NO cutting (typically 5-10A)");
        logPrintln("2. STANDARD CUT: Reference cutting speed (typically 20-25A)");
        logPrintln("3. HEAVY LOAD: (Optional) High-speed or high-load cutting\n");
        logPrintln("Each phase will measure for 10 seconds. Press ENTER when ready for phase 1...");

        vfd_calib_state = VFD_CALIB_MEASURING_IDLE;
        vfd_calib_start_time = millis();
        logPrintln("[VFDCAL] Phase 1: Measuring IDLE BASELINE (10 seconds)");
        logPrintln(">> Spin blade with NO cutting load, then wait for completion <<");
        vfdCalibrationStartMeasure(MEASUREMENT_DURATION_MS, "Idle Baseline");

    } else if (strcmp(argv[3], "confirm") == 0) {
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
                logPrintln("\n[VFDCAL] Phase 3: HEAVY LOAD (Optional)");
                logPrintln("Measure heavy-load scenario for worst-case baseline?");
                logPrintln("  - Type 'continue' to measure heavy load (10 seconds)");
                logPrintln("  - Type 'finish' to skip and calculate thresholds");
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

    } else if (strcmp(argv[3], "continue") == 0) {
        if (vfd_calib_state == VFD_CALIB_CONFIRM_STD) {
            logPrintln("[VFDCAL] Phase 3: Measuring HEAVY LOAD (10 seconds)");
            logPrintln(">> Perform heavy-load cutting scenario, then wait <<");
            vfd_calib_state = VFD_CALIB_MEASURING_HEAVY;
            vfdCalibrationStartMeasure(MEASUREMENT_DURATION_MS, "Heavy Load");
        } else {
            logError("[VFDCAL] Not in phase confirmation state.");
        }

    } else if (strcmp(argv[3], "finish") == 0) {
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

    } else if (strcmp(argv[3], "abort") == 0) {
        logWarning("[VFDCAL] Calibration aborted. Use 'start' to begin again.");
        vfd_calib_state = VFD_CALIB_IDLE;

    } else if (strcmp(argv[3], "reset") == 0) {
        logWarning("[VFDCAL] Resetting all VFD calibration data!");
        vfdCalibrationReset();
        vfd_calib_state = VFD_CALIB_IDLE;
        logInfo("[VFDCAL] All calibration data cleared.");

    } else if (strcmp(argv[3], "status") == 0) {
        const char* state_names[] = {
            "IDLE", "MEASURING_IDLE", "CONFIRM_IDLE", "MEASURING_STD",
            "CONFIRM_STD", "MEASURING_HEAVY", "CONFIRM_HEAVY", "COMPLETE"
        };
        logPrintf("[VFDCAL] Current state: %s\n", state_names[vfd_calib_state]);
        vfdCalibrationPrintSummary();

    } else if (strcmp(argv[3], "show") == 0) {
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
    // argv[0]=vfd, argv[1]=diagnostics, argv[2]=SUB
    if (argc < 3 || strcmp(argv[2], "help") == 0) {
        logPrintln("[VFDDIAG] === VFD Diagnostics ===");
        logPrintln("Commands:");
        logPrintln("  vfd diagnostics status    - Show real-time VFD status");
        logPrintln("  vfd diagnostics thermal   - Show thermal monitoring details");
        logPrintln("  vfd diagnostics current   - Show motor current measurements");
        logPrintln("  vfd diagnostics frequency - Show output frequency data");
        logPrintln("  vfd diagnostics full      - Comprehensive VFD report");
        logPrintln("  vfd diagnostics calib     - Show calibration details");
        return;
    }

    // Real-time status snapshot
    if (strcmp(argv[2], "status") == 0) {
        logPrintln("\n[VFDDIAG] === VFD Real-Time Status ===");
        altivar31PrintDiagnostics();

    } else if (strcmp(argv[2], "thermal") == 0) {
        logPrintln("\n[VFDDIAG] === Thermal Monitoring ===");
        int16_t thermal = altivar31GetThermalState();
        int32_t warn = configGetInt(KEY_VFD_TEMP_WARN, 85);
        int32_t crit = configGetInt(KEY_VFD_TEMP_CRIT, 90);

        logPrintf("Thermal State:       %d%% (nominal: 100%%)\r\n", thermal);
        logPrintf("Warning Threshold:   >%ldC (%ld%% state)\r\n", (long)warn, (long)(warn * 1.3));
        logPrintf("Critical Threshold:  >%ldC (%ld%% state)\r\n", (long)crit, (long)(crit * 1.4));

        if (thermal > (crit * 1.4)) {
            logPrintln("Status:              CRITICAL - Emergency stop required!");
        } else if (thermal > (warn * 1.3)) {
            logPrintln("Status:              WARNING - Monitor closely");
        } else {
            logPrintln("Status:              NORMAL");
        }
        logPrintln("");

    } else if (strcmp(argv[2], "current") == 0) {
        logPrintln("\n[VFDDIAG] === Motor Current Measurements ===");
        float current = altivar31GetCurrentAmps();
        int16_t raw = altivar31GetCurrentRaw();

        logPrintf("Motor Current:       %.2f A (raw: %d)\r\n", current, raw);

        if (vfdCalibrationIsValid()) {
            const vfd_calibration_data_t* calib = vfdCalibrationGetData();
            logPrintln("\r\nCalibrated Baselines:");
            logPrintf("  Idle (no cut):       %.2f A (RMS) / %.2f A (peak)\r\n",
                          calib->idle_rms_amps, calib->idle_peak_amps);
            logPrintf("  Standard Cut:        %.2f A (RMS) / %.2f A (peak)\r\n",
                          calib->standard_cut_rms_amps, calib->standard_cut_peak_amps);
            if (calib->heavy_cut_rms_amps > 0.0f) {
                logPrintf("  Heavy Load:          %.2f A (RMS) / %.2f A (peak)\r\n",
                               calib->heavy_cut_rms_amps, calib->heavy_cut_peak_amps);
            }
            logPrintln("\r\nStall Detection:");
            logPrintf("  Threshold:           %.2f A\r\n", calib->stall_threshold_amps);
            logPrintf("  Current vs Threshold: %.2f A / %.2f A = %.0f%%\r\n",
                          current, calib->stall_threshold_amps,
                          (calib->stall_threshold_amps > 0) ? (current / calib->stall_threshold_amps * 100.0f) : 0.0f);

            if (vfdCalibrationIsStall(current)) {
                logPrintln("  Status:              STALL DETECTED!");
            } else {
                logPrintln("  Status:              Normal");
            }
        } else {
            logPrintln("  Calibration Status:  NOT CALIBRATED");
        }
        logPrintln("");

    } else if (strcmp(argv[2], "frequency") == 0) {
        logPrintln("\n[VFDDIAG] === Output Frequency ===");
        float freq = altivar31GetFrequencyHz();
        int16_t raw = altivar31GetFrequencyRaw();

        logPrintf("Output Frequency:    %.1f Hz (raw: %d, 0.1Hz/unit)\n", freq, raw);
        logPrintf("Status:              %s\n", freq > 0.0f ? "RUNNING" : "IDLE/STOPPED");

    } else if (strcmp(argv[2], "calib") == 0) {
        logPrintln("\n[VFDDIAG] === Calibration Details ===");
        vfdCalibrationPrintSummary();

    } else if (strcmp(argv[2], "full") == 0) {
        logPrintln("\n[VFDDIAG] === Comprehensive VFD Report ===");
        logPrintln("\n--- Status ---");
        altivar31PrintDiagnostics();

        logPrintln("\n--- Current Measurements ---");
        float current = altivar31GetCurrentAmps();
        int32_t raw = altivar31GetCurrentRaw();
        logPrintf("Motor Current:       %.2f A (raw: %ld)\r\n", current, (long)raw);

        logPrintln("\n--- Thermal State ---");
        int16_t thermal = altivar31GetThermalState();
        int32_t warn = configGetInt(KEY_VFD_TEMP_WARN, 85);
        int32_t crit = configGetInt(KEY_VFD_TEMP_CRIT, 90);
        logPrintf("Thermal State:       %d%% (warn: %ld%%, crit: %ld%%)\r\n",
                      thermal, (long)(warn * 1.3), (long)(crit * 1.4));

        logPrintln("\n--- Frequency ---");
        float freq = altivar31GetFrequencyHz();
        logPrintf("Output Frequency:    %.1f Hz\r\n", freq);

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
    // argv[0]=vfd, argv[1]=config, argv[2]=SUB
    if (argc < 3 || strcmp(argv[2], "help") == 0) {
        logPrintln("[VFDCFG] === VFD Configuration ===");
        logPrintln("Commands:");
        logPrintln("  vfd config margin <percent>      - Set stall margin (default 20%)");
        logPrintln("  vfd config timeout <ms>          - Set stall timeout (default 2000ms)");
        logPrintln("  vfd config temp warn <C>         - Set temperature warning threshold");
        logPrintln("  vfd config temp crit <C>         - Set temperature critical threshold");
        logPrintln("  vfd config enable <on|off>       - Enable/disable VFD stall detection");
        logPrintln("  vfd config show                  - Display current settings");
        return;
    }

    if (strcmp(argv[2], "margin") == 0) {
        if (argc < 4) {
            logPrintln("[VFDCFG] Usage: vfd config margin <percent>");
            return;
        }
        float margin = atof(argv[3]);
        if (margin < 5.0f || margin > 100.0f) {
            logError("[VFDCFG] Margin must be between 5%% and 100%%");
            return;
        }
        configSetInt(KEY_VFD_STALL_MARGIN, (int32_t)margin);
        configUnifiedFlush();
        configUnifiedSave();
        logInfo("[VFDCFG] Stall margin set to %.0f%%", margin);

    } else if (strcmp(argv[2], "timeout") == 0) {
        if (argc < 4) {
            logPrintln("[VFDCFG] Usage: vfd config timeout <milliseconds>");
            return;
        }
        uint32_t timeout_ms = (uint32_t)atoi(argv[3]);
        if (timeout_ms < 100 || timeout_ms > 60000) {
            logError("[VFDCFG] Timeout must be between 100ms and 60000ms");
            return;
        }
        configSetInt(KEY_STALL_TIMEOUT, (int32_t)timeout_ms);
        configUnifiedFlush();
        configUnifiedSave();
        logInfo("[VFDCFG] Stall timeout set to %lu ms", (unsigned long)timeout_ms);

    } else if (strcmp(argv[2], "temp") == 0) {
        if (argc < 5) {
            logPrintln("[VFDCFG] Usage: vfd config temp [warn|crit] <C>");
            return;
        }

        int32_t temp = (int32_t)atoi(argv[4]);
        if (temp < 0 || temp > 150) {
            logError("[VFDCFG] Temperature must be between 0C and 150C");
            return;
        }

        if (strcmp(argv[3], "warn") == 0) {
            configSetInt(KEY_VFD_TEMP_WARN, temp);
            logInfo("[VFDCFG] Temperature warning threshold set to %ldC", (long)temp);
        } else if (strcmp(argv[3], "crit") == 0) {
            configSetInt(KEY_VFD_TEMP_CRIT, temp);
            logInfo("[VFDCFG] Temperature critical threshold set to %ldC", (long)temp);
        } else {
            logError("[VFDCFG] Use 'warn' or 'crit'");
            return;
        }
        configUnifiedFlush();
        configUnifiedSave();

    } else if (strcmp(argv[2], "enable") == 0) {
        if (argc < 4) {
            logPrintln("[VFDCFG] Usage: vfd config enable [on|off]");
            return;
        }

        bool enable = (strcmp(argv[3], "on") == 0) || (strcmp(argv[3], "1") == 0);
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

// --- DISPATCHERS ---

void cmd_calib_ppmm_dispatch(int argc, char** argv) {
    if (argc < 3) {
        logPrintln("[CALIB] Usage: calibrate ppmm [axis distance | end | reset axis]");
        return;
    }
    
    if (strcmp(argv[2], "end") == 0) {
        cmd_calib_ppmm_end(argc, argv);
    } else if (strcmp(argv[2], "reset") == 0) {
        cmd_calib_ppmm_reset(argc, argv);
    } else {
        cmd_calib_ppmm_start(argc, argv);
    }
}

void cmd_calibrate_vfd_dispatch(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"current", cmd_vfd_calib_current, "VFD motor current calibration workflow"}
    };
    cliDispatchSubcommand("[CALIB VFD]", argc, argv, subcmds, 1, 2);
}

void cmd_calibrate_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"speed",   cmd_auto_calibrate_speed, "Auto-detect profile speeds"},
        {"ppmm",    cmd_calib_ppmm_dispatch,  "Manual PPM measurement"},
        {"vfd",     cmd_calibrate_vfd_dispatch, "VFD calibration tools"}
    };
    cliDispatchSubcommand("[CALIB]", argc, argv, subcmds, 3, 1);
}

void cmd_vfd_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"diagnostics", cmd_vfd_diagnostics, "VFD telemetry and health"},
        {"config",      cmd_vfd_config,      "Configure stall/thermal limits"}
    };
    cliDispatchSubcommand("[VFD]", argc, argv, subcmds, 2, 1);
}

void cliRegisterCalibCommands() {
  cliRegisterCommand("calib", "Start automatic distance calibration", cmd_encoder_calib);
  cliRegisterCommand("calibrate", "System calibration tools", cmd_calibrate_main);
  cliRegisterCommand("vfd", "VFD monitoring and configuration", cmd_vfd_main);
}
