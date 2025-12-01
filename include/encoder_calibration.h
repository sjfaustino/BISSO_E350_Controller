#ifndef ENCODER_CALIBRATION_H
#define ENCODER_CALIBRATION_H

#include <Arduino.h>
#include "hardware_config.h" 

#define ENCODER_CALIBRATION_TIMEOUT_MS 30000
#define ENCODER_PPM_TOLERANCE 0.1f

// ============================================================================
// ENUM DEFINITIONS (MUST be defined first)
// ============================================================================

typedef enum {
  CALIBRATION_IDLE = 0,
  CALIBRATION_IN_PROGRESS = 1,
  CALIBRATION_COMPLETE = 2,
  CALIBRATION_ERROR = 3,
  CALIB_MANUAL_START = 4, 
  CALIB_MANUAL_WAIT_MOVE = 5 
} calibration_state_t;

// ============================================================================
// STRUCT DEFINITIONS 
// ============================================================================

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

typedef struct {
  calibration_state_t state;
  uint8_t axis;
  int32_t start_counts;
  float target_mm;
} manual_calib_state_t;


// ============================================================================
// EXTERNS AND PROTOTYPES
// ============================================================================

extern MachineCalibration machineCal;
extern manual_calib_state_t g_manual_calib; 

// FIX: Prototype signature changed to BOOL to match the implementation in encoder_calibration.cpp
bool encoderCalibrationStart(uint8_t axis, float distance_mm); 

bool encoderCalibrationComplete();
bool encoderCalibrationFinalize(uint8_t axis);
double encoderCalibrationGetPPM(uint8_t axis);
calibration_state_t encoderCalibrationGetState();
uint8_t encoderCalibrationGetAxis();
void encoderCalibrationAbort();
void encoderCalibrationReset(uint8_t axis);
void encoderCalibrationDiagnostics();
void encoderCalibrationSetPPM(uint8_t axis, double ppm); 
void loadAllCalibration();
void saveAllCalibration();
void encoderCalibrationInit(); // Explicitly defined here to resolve main.cpp issue

#endif // ENCODER_CALIBRATION_H