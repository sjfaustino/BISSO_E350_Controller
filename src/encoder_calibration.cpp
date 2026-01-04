#include "encoder_calibration.h"
#include "motion.h"
#include "hardware_config.h"
#include "encoder_wj66.h"
#include "fault_logging.h"
#include <Preferences.h>
#include "serial_logger.h"
#include <string.h>
#include <math.h>

manual_calib_state_t g_manual_calib = {CALIBRATION_IDLE, 255, 0, 0.0f};
static calibration_data_t calib_data[4];
static calibration_state_t calib_state = CALIBRATION_IDLE;
static uint8_t calibrating_axis = 255;
static uint32_t calib_start_time = 0;

void encoderCalibrationInit() {
  logInfo("[CALIB] Initializing...");
  for (int i = 0; i < 4; i++) {
    calib_data[i].pulses_per_mm = 0.0;
    calib_data[i].is_valid = false;
    calib_data[i].sample_count = 0;
  }
  calib_state = CALIBRATION_IDLE;
  g_manual_calib.state = CALIBRATION_IDLE;
  logInfo("[CALIB] [OK] Ready");
}

void encoderCalibrationUpdate() {
  if (calib_state != CALIBRATION_IN_PROGRESS) return;
  if (millis() - calib_start_time > ENCODER_CALIBRATION_TIMEOUT_MS) {
    calib_state = CALIBRATION_ERROR;
    logError("[CALIB] Timeout");
    calibrating_axis = 255;
  }
}

bool encoderCalibrationStart(uint8_t axis, float distance_mm) { 
  if (axis >= 4 || distance_mm <= 0.0f) {
    logError("[CALIB] Invalid parameters");
    return false;
  }
  if (calib_state == CALIBRATION_IN_PROGRESS) {
    logError("[CALIB] Busy");
    return false;
  }
  calibrating_axis = axis;
  calib_state = CALIBRATION_IN_PROGRESS;
  calib_start_time = millis();
  calib_data[axis].manual_distance_mm = distance_mm;
  calib_data[axis].sample_count = 0;
  logInfo("[CALIB] Started Axis %d (Dist: %.1f mm)", axis, distance_mm);
  return true;
}

bool encoderCalibrationFinalize(uint8_t axis) {
  if (axis >= 4 || axis != calibrating_axis) return false;
  if (calib_data[axis].sample_count == 0) {
    logError("[CALIB] No samples collected");
    return false;
  }
  
  int32_t distance_counts = calib_data[axis].end_position - calib_data[axis].start_position;
  if (distance_counts == 0) {
    logError("[CALIB] No motion detected");
    return false;
  }
  
  double scale_factor = (double)distance_counts / calib_data[axis].manual_distance_mm;
  calib_data[axis].pulses_per_mm = scale_factor;
  calib_data[axis].is_valid = true;
  calib_data[axis].last_calibrated = millis();
  
  float target_scale = (axis == 3) ? machineCal.A.pulses_per_degree : machineCal.X.pulses_per_mm;
  if (target_scale > 0.0f) {
      double error_percent = fabs(scale_factor - target_scale) / target_scale;
      if (error_percent > ENCODER_PPM_TOLERANCE) { 
          logWarning("[CALIB] Tolerance exceeded (%.2f%% error)", error_percent * 100.0);
      }
  }
  
  logInfo("[CALIB] Axis %d Result: %.4f pulses/unit", axis, scale_factor);
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
  logInfo("[CALIB] Aborted");
}

void encoderCalibrationReset(uint8_t axis) {
  if (axis < 4) {
    calib_data[axis].is_valid = false;
    calib_data[axis].pulses_per_mm = 0.0;
    calib_data[axis].ppm_error = 0.0;
    logInfo("[CALIB] Reset Axis %d", axis);
  }
}

void encoderCalibrationDiagnostics() {
  serialLoggerLock();
  Serial.println("\n[CALIB] === Diagnostics ===");
  Serial.printf("State: %d | Axis: %d\n", calib_state, calibrating_axis);
  for (int i = 0; i < 4; i++) {
    Serial.printf("  Axis %d: %s | Scale=%.2f\n", i, calib_data[i].is_valid ? "VALID" : "INVALID", calib_data[i].pulses_per_mm);
  }
  serialLoggerUnlock();
}

void encoderCalibrationSetPPM(uint8_t axis, double ppm) {
    if (axis >= 4) return;
    calib_data[axis].pulses_per_mm = ppm;
    calib_data[axis].is_valid = true;
    calib_data[axis].last_calibrated = millis();
    
    if (axis == 0) machineCal.X.pulses_per_mm = (float)ppm;
    else if (axis == 1) machineCal.Y.pulses_per_mm = (float)ppm;
    else if (axis == 2) machineCal.Z.pulses_per_mm = (float)ppm;
    else if (axis == 3) machineCal.A.pulses_per_degree = (float)ppm;
    
    logInfo("[CALIB] Axis %d scale set to %.3f. Saving...", axis, ppm);
    saveAllCalibration();
}
