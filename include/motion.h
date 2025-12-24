/**
 * @file motion.h
 * @brief Core Motion Engine Definitions & API (Gemini v3.5.19)
 * @details Final Polish: Full Encapsulation of Axis Array.
 */

#ifndef MOTION_H
#define MOTION_H

#include "plc_iface.h"
#include <Arduino.h>
#include <stdint.h>


#define MOTION_AXES 4
#define MOTION_CONSENSO_TIMEOUT_MS 5000
#define HOMING_SETTLE_MS 1000

typedef enum {
  SPEED_PROFILE_1 = 0,
  SPEED_PROFILE_2 = 1,
  SPEED_PROFILE_3 = 2
} speed_profile_t;

typedef enum {
  MOTION_IDLE = 0,
  MOTION_WAIT_CONSENSO = 1,
  MOTION_EXECUTING = 2,
  MOTION_STOPPING = 3,
  MOTION_PAUSED = 4,
  MOTION_ERROR = 5,
  MOTION_HOMING_APPROACH_FAST = 6,
  MOTION_HOMING_BACKOFF = 7,
  MOTION_HOMING_APPROACH_FINE = 8,
  MOTION_HOMING_SETTLE = 9,
  MOTION_DWELL = 10,   // Non-blocking dwell/pause
  MOTION_WAIT_PIN = 11 // Wait for GPIO/I2C pin state
} motion_state_t;

class Axis {
public:
  Axis();
  void init(uint8_t axis_id);
  void updateState(int32_t current_pos, int32_t global_target,
                   bool consensus_active);
  bool checkSoftLimits(bool strict_mode);

  uint8_t id;
  int32_t position;
  int32_t target_position;
  motion_state_t state;

  bool enabled;
  int32_t soft_limit_min;
  int32_t soft_limit_max;
  bool soft_limit_enabled;

  float commanded_speed_mm_s;
  speed_profile_t saved_speed_profile;
  int32_t position_at_stop;
  uint32_t state_entry_ms;
  int32_t homing_trigger_pos;
  uint32_t dwell_end_ms; // When dwell completes (for MOTION_DWELL state)

  // Pin wait state (MOTION_WAIT_PIN)
  uint8_t wait_pin_id;          // Pin to monitor
  uint8_t wait_pin_type;        // 0=I73, 1=Board, 2=GPIO
  uint8_t wait_pin_state;       // State to wait for (0 or 1)
  uint32_t wait_pin_timeout_ms; // Timeout (0 = no timeout)

  // Velocity tracking (for encoder deviation detection)
  float current_velocity_mm_s; // Current velocity in mm/s
  int32_t prev_position;       // Previous position (for velocity calculation)
  uint32_t prev_update_ms;     // Previous update timestamp

private:
  bool _error_logged;
};

// --- DATA ACCESS ---
// Removed direct array access to enforce encapsulation
const Axis *motionGetAxis(uint8_t axis);

// --- CORE CONTROL API ---
void motionInit();
void motionUpdate();

bool motionMoveAbsolute(float x, float y, float z, float a, float speed_mm_s);
bool motionMoveRelative(float dx, float dy, float dz, float da,
                        float speed_mm_s);
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

// --- ACCESSORS ---
int32_t motionGetPosition(uint8_t axis);
int32_t motionGetTarget(uint8_t axis);
float motionGetPositionMM(uint8_t axis);
float motionGetVelocity(uint8_t axis); // Get current velocity in mm/s
motion_state_t motionGetState(uint8_t axis);
bool motionIsMoving();
bool motionIsStalled(uint8_t axis);
bool motionIsEmergencyStopped();
uint8_t motionGetActiveAxis();
int32_t motionGetActiveStartPosition();
void motionSetActiveStartPosition(int32_t position);
void motionClearActiveAxis();
const char *motionStateToString(motion_state_t state);

// --- HELPERS ---
speed_profile_t motionMapSpeedToProfile(uint8_t axis, float speed);
void motionSetPLCSpeedProfile(speed_profile_t profile);
void motionSetPLCAxisDirection(uint8_t axis, bool enable, bool is_plus);

extern const uint8_t AXIS_TO_I73_BIT[];
extern const uint8_t AXIS_TO_CONSENSO_BIT[];

#endif