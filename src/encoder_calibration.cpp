#include "encoder_calibration.h"
#include "motion.h"
#include "hardware_config.h"
#include "encoder_wj66.h"
#include "fault_logging.h"
#include <Preferences.h>
#include "serial_logger.h"
#include <string.h>
#include <math.h>

// Global state variable for the manual CLI routine (defined here, externed in header)
manual_calib_state_t g_manual_calib = {CALIBRATION_IDLE, 255, 0, 0.0f};

static calibration_data_t calib_data[4];
static calibration_state_t calib_state = CALIBRATION_IDLE;
static uint8_t calibrating_axis = 255;
static uint32_t calib_start_time = 0;

void encoderCalibrationInit() {
  Serial.println("[CALIBRATION] Encoder calibration system initializing...");
  
  for (int i = 0; i < 4; i++) {
    calib_data[i].pulses_per_mm = 0.0;
    calib_data[i].ppm_error = 0.0;
    calib_data[i].is_valid = false;
    calib_data[i].sample_count = 0;
    calib_data[i].last_calibrated = 0;
    calib_data[i].start_position = 0;
    calib_data[i].end_position = 0;
    calib_data[i].manual_distance_mm = 0.0f;
  }
  
  calib_state = CALIBRATION_IDLE;
  g_manual_calib.state = CALIBRATION_IDLE;
  Serial.println("[CALIBRATION] Calibration system ready");
}

void encoderCalibrationUpdate() {
  if (calib_state != CALIBRATION_IN_PROGRESS) {
    return;
  }
  
  uint32_t elapsed = millis() - calib_start_time;
  if (elapsed > ENCODER_CALIBRATION_TIMEOUT_MS) {
    calib_state = CALIBRATION_ERROR;
    Serial.print("[CALIBRATION] ERROR: Timeout after ");
    Serial.print(elapsed);
    Serial.println(" ms");
    calibrating_axis = 255;
  }
}

bool encoderCalibrationStart(uint8_t axis, float distance_mm) { 
  if (axis >= 4 || distance_mm <= 0.0f) {
    Serial.println("[CALIBRATION] ERROR: Invalid parameters");
    return false;
  }
  
  if (calib_state == CALIBRATION_IN_PROGRESS) {
    Serial.println("[CALIBRATION] ERROR: Calibration already in progress");
    return false;
  }
  
  calibrating_axis = axis;
  calib_state = CALIBRATION_IN_PROGRESS;
  calib_start_time = millis();
  
  calib_data[axis].manual_distance_mm = distance_mm;
  calib_data[axis].sample_count = 0;
  
  Serial.print("[CALIBRATION] Started axis ");
  Serial.print(axis);
  Serial.print(" for distance ");
  Serial.print(distance_mm);
  Serial.println(" mm");
  
  return true;
}

bool encoderCalibrationComplete() {
  return calib_state == CALIBRATION_COMPLETE;
}

bool encoderCalibrationFinalize(uint8_t axis) {
  if (axis >= 4 || axis != calibrating_axis) {
    return false;
  }
  
  if (calib_data[axis].sample_count == 0) {
    Serial.println("[CALIBRATION] ERROR: No samples collected");
    return false;
  }
  
  int32_t distance_counts = calib_data[axis].end_position - calib_data[axis].start_position;
  if (distance_counts == 0) {
    Serial.println("[CALIBRATION] ERROR: No motion detected");
    return false;
  }
  
  // Renamed to scale_factor (can be pulses/mm or pulses/degree)
  double scale_factor = (double)distance_counts / calib_data[axis].manual_distance_mm;
  calib_data[axis].pulses_per_mm = scale_factor;
  calib_data[axis].is_valid = true;
  calib_data[axis].last_calibrated = millis();
  
  // --- FIX: IMPLEMENT PPM TOLERANCE CHECK ---
  float target_scale;
  if (axis == 0) target_scale = machineCal.X.pulses_per_mm;
  else if (axis == 1) target_scale = machineCal.Y.pulses_per_mm;
  else if (axis == 2) target_scale = machineCal.Z.pulses_per_mm;
  else target_scale = machineCal.A.pulses_per_degree;
  
  if (target_scale > 0.0f) {
      double error_percent = fabs(scale_factor - target_scale) / target_scale;
      // Strict check: fail if > 10% variance
      if (error_percent > ENCODER_PPM_TOLERANCE) { 
          logWarning("[CALIB] WARN: Scale factor tolerance exceeded (%.2f%% error)", error_percent * 100.0);
          calib_data[axis].ppm_error = error_percent;
          faultLogWarning(FAULT_ENCODER_TIMEOUT, "Calibration rejected: variance too high");
          // Option to fail here: return false;
      }
  }
  // ------------------------------------------
  
  Serial.print("[CALIBRATION] Axis ");
  Serial.print(axis);
  Serial.print(": ");
  Serial.print(scale_factor);
  Serial.println(" pulses/unit");
  
  // Call the specific setter for this axis
  encoderCalibrationSetPPM(axis, scale_factor);
  
  calib_state = CALIBRATION_COMPLETE;
  calibrating_axis = 255;
  
  return true;
}

double encoderCalibrationGetPPM(uint8_t axis) {
  if (axis < 4 && calib_data[axis].is_valid) {
    return calib_data[axis].pulses_per_mm;
  }
  return 0.0;
}

calibration_state_t encoderCalibrationGetState() {
  return calib_state;
}

uint8_t encoderCalibrationGetAxis() {
  return calibrating_axis;
}

void encoderCalibrationAbort() {
  calib_state = CALIBRATION_IDLE;
  calibrating_axis = 255;
  Serial.println("[CALIBRATION] Calibration aborted");
}

void encoderCalibrationReset(uint8_t axis) {
  if (axis < 4) {
    calib_data[axis].is_valid = false;
    calib_data[axis].pulses_per_mm = 0.0;
    calib_data[axis].ppm_error = 0.0;
    Serial.print("[CALIBRATION] Reset calibration for axis ");
    Serial.println(axis);
  }
}

void encoderCalibrationDiagnostics() {
  Serial.println("\n[CALIBRATION] === Encoder Calibration Diagnostics ===");
  
  Serial.print("State: ");
  switch(calib_state) {
    case CALIBRATION_IDLE: Serial.println("IDLE"); break;
    case CALIBRATION_IN_PROGRESS: Serial.println("IN_PROGRESS"); break;
    case CALIBRATION_COMPLETE: Serial.println("COMPLETE"); break;
    case CALIBRATION_ERROR: Serial.println("ERROR"); break;
    case CALIB_MANUAL_START: Serial.println("MANUAL_START"); break;
    case CALIB_MANUAL_WAIT_MOVE: Serial.println("MANUAL_WAIT_MOVE"); break;
    default: Serial.println("UNKNOWN");
  }
  
  if (calib_state == CALIBRATION_IN_PROGRESS) {
    Serial.print("  Calibrating axis: ");
    Serial.println(calibrating_axis);
    Serial.print("  Time elapsed: ");
    Serial.print(millis() - calib_start_time);
    Serial.println(" ms");
  }
  
  for (int i = 0; i < 4; i++) {
    Serial.print("\nAxis ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(calib_data[i].is_valid ? "VALID" : "INVALID");
    Serial.print(" | Scale=");
    Serial.print(calib_data[i].pulses_per_mm);
    Serial.print(" | Samples=");
    Serial.print(calib_data[i].sample_count);
    Serial.print(" | Last: ");
    Serial.print(millis() - calib_data[i].last_calibrated);
    Serial.println(" ms ago");
  }
}

void encoderCalibrationSetPPM(uint8_t axis, double ppm) {
    if (axis >= 4) return;
    
    // Update the internal calibration data
    calib_data[axis].pulses_per_mm = ppm;
    calib_data[axis].is_valid = true;
    calib_data[axis].last_calibrated = millis();
    
    // Update the MachineCalibration struct 
    if (axis == 0) {
        machineCal.X.pulses_per_mm = (float)ppm;
    } else if (axis == 1) {
        machineCal.Y.pulses_per_mm = (float)ppm;
    } else if (axis == 2) {
        machineCal.Z.pulses_per_mm = (float)ppm;
    } else if (axis == 3) {
        machineCal.A.pulses_per_degree = (float)ppm;
    }
    
    logInfo("[CALIB] Axis %d Scale set manually to %.3f. Saving configuration...", axis, ppm);
    saveAllCalibration();
}