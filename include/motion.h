/**
 * @file motion.h
 * @brief Core Motion Engine Definitions & API (PosiPro)
 * @details Final Polish: Full Encapsulation of Axis Array.
 */

#ifndef MOTION_H
#define MOTION_H

#include "plc_iface.h"
#include <Arduino.h>
#include <stdint.h>
#include "system_constants.h"


#define MOTION_AXES 4
#define MOTION_CONSENSO_TIMEOUT_MS 5000
#define HOMING_SETTLE_MS 1000

class Axis;

// --- DATA ACCESS ---
// Removed direct array access to enforce encapsulation
const Axis *motionGetAxis(uint8_t axis);

// --- CORE CONTROL API ---
result_t motionInit();
void motionUpdate();

bool motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);
bool motionMoveRelative(float dx, float dy, float dz, float da,
                        float speed_mm_s);
bool motionJog(float dx, float dy, float dz, float da, float speed_mm_s); // Web API jog
bool motionHome(uint8_t axis);
bool motionSetPosition(float x, float y, float z,
                       float a); // Set position without moving (for G92)

bool motionStop();
bool motionPause();
bool motionResume();
bool motionDwell(uint32_t ms); // Non-blocking dwell/pause for G4 command
bool motionWaitPin(uint8_t pin_id, uint8_t pin_type, uint8_t state,
                   uint32_t timeout_sec); // M226 Wait for pin

void motionEmergencyStop();
bool motionClearEmergencyStop();

// --- CONFIGURATION ---
void motionSetFeedOverride(float factor);
float motionGetFeedOverride();
void motionSetSoftLimits(uint8_t axis, int32_t min_pos, int32_t max_pos);
void motionEnableSoftLimits(uint8_t axis, bool enable);
bool motionGetSoftLimits(uint8_t axis, int32_t *min_pos, int32_t *max_pos);
void motionSetStrictLimits(bool enable);
void motionEnableEncoderFeedback(bool enable);
bool motionIsEncoderFeedbackEnabled();

// --- DIAGNOSTICS ---
void motionDiagnostics();
void motionSetCoordinatedMode(bool enable);
bool motionIsCoordinatedEnabled();

// --- ACCESSORS ---
int32_t motionGetPosition(uint8_t axis);
int32_t motionGetTarget(uint8_t axis);
float motionGetPositionMM(uint8_t axis);
float motionGetAxisScale(uint8_t axis);
float motionGetVelocity(uint8_t axis); // Get current velocity in mm/s
motion_state_t motionGetState(uint8_t axis);
bool motionIsMoving();
bool motionIsStalled(uint8_t axis);
bool motionIsEmergencyStopped();
uint8_t motionGetActiveAxis();
void motionClearActiveAxis();
const char *motionStateToString(motion_state_t state);

// --- HELPERS ---
speed_profile_t motionMapSpeedToProfile(uint8_t axis, float speed);
float motionGetCalibratedFeedRate(uint8_t axis, float speed_mm_s);
void motionSetPLCSpeedProfile(speed_profile_t profile);
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus);
void motionSetVFDVelocities(float freq1_hz, float freq2_hz);

// --- PERFORMANCE DIAGNOSTICS ---
void motionPrintSpinlockStats();  // Print spinlock critical section timing report
void motionResetSpinlockStats();  // Reset spinlock timing statistics

// --- JITTER TRACKING ---
uint32_t motionGetMaxJitterUS();   // Get maximum loop jitter recorded in microseconds
void motionResetMaxJitter();     // Reset jitter tracking
void motionTrackJitterUS(uint32_t jitter_us); // Internal use: track loop jitter in microseconds

// --- MAINTENANCE TASK ---
// Distance persistence and maintenance checks run in a separate low-priority task
// to keep NVS writes out of the real-time motion loop.
double motionGetAccumulatedDistance(uint8_t axis);  // Returns accumulated distance in mm
void motionStartMaintenanceTask();

extern const uint8_t AXIS_TO_I73_BIT[];
extern const uint8_t AXIS_TO_CONSENSO_BIT[];

#endif
