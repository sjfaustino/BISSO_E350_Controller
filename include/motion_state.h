/**
 * @file motion_state.h
 * @brief Definition of Core Motion Data Structures and Read-Only Accessors
 * @project Gemini v3.1.0
 */

#ifndef MOTION_STATE_H
#define MOTION_STATE_H

#include <stdint.h>
#include "motion.h"

// Note: motion_axis_t is defined in motion.h
// The actual axes array is defined externally in motion_control.cpp

// Accessors (Read-Only)
int32_t motionGetPosition(uint8_t axis);
int32_t motionGetTarget(uint8_t axis);
float motionGetPositionMM(uint8_t axis); 
motion_state_t motionGetState(uint8_t axis);
bool motionIsMoving();
bool motionIsStalled(uint8_t axis);
bool motionIsEmergencyStopped();
uint8_t motionGetActiveAxis();
const char* motionStateToString(motion_state_t state);

#endif // MOTION_STATE_H