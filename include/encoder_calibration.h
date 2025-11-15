#ifndef ENCODER_CALIBRATION_H
#define ENCODER_CALIBRATION_H

#include <Arduino.h>

#define ENCODER_CALIBRATION_TIMEOUT_MS 30000
#define ENCODER_PPM_TOLERANCE 0.1f

typedef enum {
  CALIBRATION_IDLE = 0,
  CALIBRATION_IN_PROGRESS = 1,
  CALIBRATION_COMPLETE = 2,
  CALIBRATION_ERROR = 3
} calibration_state_t;

typedef struct {
  double pulses_per_mm;
  double ppm_error;
  uint32_t last_calibrated;
  bool is_valid;
  uint32_t sample_count;
  int32_t start_position;
  int32_t end_position;
  float manual_distance_mm;
} calibration_data_t;

void encoderCalibrationInit();
void encoderCalibrationUpdate();
bool encoderCalibrationStart(uint8_t axis, float distance_mm);
bool encoderCalibrationComplete();
bool encoderCalibrationFinalize(uint8_t axis);
double encoderCalibrationGetPPM(uint8_t axis);
calibration_state_t encoderCalibrationGetState();
uint8_t encoderCalibrationGetAxis();
void encoderCalibrationAbort();
void encoderCalibrationReset(uint8_t axis);
void encoderCalibrationDiagnostics();

#endif
