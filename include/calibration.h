#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <Arduino.h>
#include <stdint.h>
#include "hardware_config.h" // Includes definitions for AxisCalibration and MachineCalibration
#include "fault_logging.h"   // Likely needed for error logging during load

// ============================================================================
// CALIBRATION FUNCTIONS (Prototypes from calibration.cpp)
// ============================================================================

/**
 * @brief Loads all calibration data (PPM, speeds, backlash) from NVS into 
 * the global MachineCalibration structure.
 */
void loadAllCalibration();

/**
 * @brief Saves all calibration data (PPM, speeds, backlash) from the global 
 * MachineCalibration structure to NVS for persistence.
 */
void saveAllCalibration();

/**
 * @brief Global structure holding all machine calibration constants.
 * Declared here and defined externally (e.g., in calibration.cpp).
 */
extern MachineCalibration machineCal;

#endif // CALIBRATION_H
