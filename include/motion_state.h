/**
 * @file motion_state.h
 * @brief Read-Only Accessors for Motion State
 * @project Gemini v3.5.0
 */

#ifndef MOTION_STATE_H
#define MOTION_STATE_H

#include <stdint.h>
#include "motion.h" // Needed for enum definitions

// Accessors (Read-Only)
int32_t motionGetPosition(uint8_t axis);
int32_t motionGetTarget(uint8_t axis); // Added (Fixes encoder integration error)
float motionGetPositionMM(uint8_t axis); 
motion_state_t motionGetState(uint8_t axis);

// Status Checks
bool motionIsMoving();
bool motionIsStalled(uint8_t axis);
bool motionIsEmergencyStopped();
uint8_t motionGetActiveAxis();

// Helpers
const char* motionStateToString(motion_state_t state);

#endif // MOTION_STATE_H