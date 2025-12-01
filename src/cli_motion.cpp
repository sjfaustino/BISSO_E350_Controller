#include "cli.h"
#include "serial_logger.h"
#include "motion.h"
#include "input_validation.h"
#include "system_constants.h"
#include "fault_logging.h"
#include "encoder_wj66.h"
#include "plc_iface.h"
#include "system_utilities.h" // <-- NEW: Centralized Axis Utilities
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

// ============================================================================
// FORWARD DECLARATIONS (Local Command Handlers)
// ============================================================================

void cmd_motion_status(int argc, char** argv);
void cmd_motion_move(int argc, char** argv);
void cmd_motion_stop(int argc, char** argv);
void cmd_soft_limits(int argc, char** argv);

// Forward Declaration of utility functions defined elsewhere
// NOTE: uint8_t parse_axis_arg(char* arg); REMOVED. Use axisCharToIndex.

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterMotionCommands() {
  cliRegisterCommand("motion", "Motion status", cmd_motion_status);
  cliRegisterCommand("move", "Move axes (move X POS_MM SPEED_MM/S)", cmd_motion_move);
  cliRegisterCommand("stop", "Stop motion", cmd_motion_stop);
  cliRegisterCommand("limits", "Manage soft limits (limits axis min max | info | test)", cmd_soft_limits);
}

// ============================================================================
// MOTION COMMAND IMPLEMENTATIONS
// ============================================================================

void cmd_motion_status(int argc, char** argv) {
  motionDiagnostics();
}

void cmd_motion_move(int argc, char** argv) {
  if (argc < 4) {
    Serial.println("[CLI] Usage: move [AXIS] [POS_MM] [SPEED_MM/S]");
    return;
  }
  
  // Use centralized utility to get axis index
  uint8_t target_axis = axisCharToIndex(argv[1]); 

  if (target_axis >= 4) {
      Serial.print("[CLI] ERROR: Invalid axis: ");
      Serial.println(argv[1]);
      return;
  }
  
  float targets_mm[] = {0.0f, 0.0f, 0.0f, 0.0f};
  float pos_mm = 0.0f;
  float speed_mm_s = 0.0f;
  
  if (!parseAndValidateFloat(argv[2], &pos_mm, -5000.0f, 5000.0f)) {
      Serial.print("[CLI] ERROR: Invalid position value: ");
      Serial.println(argv[2]);
      return;
  }

  if (!parseAndValidateFloat(argv[3], &speed_mm_s, MOTION_MIN_SPEED_MM_S, MOTION_MAX_SPEED_MM_S)) {
      Serial.print("[CLI] ERROR: Invalid speed value or out of range: ");
      Serial.println(argv[3]);
      return;
  }

  // Assign the target position only to the validated axis
  targets_mm[target_axis] = pos_mm;

  motionMoveAbsolute(targets_mm[0], targets_mm[1], targets_mm[2], targets_mm[3], speed_mm_s);
}

void cmd_motion_stop(int argc, char** argv) {
  motionStop();
}

void cmd_soft_limits(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("\n[LIMITS] === Soft Limit Management ===");
    Serial.println("[LIMITS] Usage: limits info | limits [AXIS] [MIN_MM] [MAX_MM]");
    return;
  }
  
  if (strcmp(argv[1], "info") == 0) {
    motionDiagnostics();
    return;
  }
  
  if (argc < 4) {
    Serial.println("[LIMITS] ERROR: Missing min/max values");
    return;
  }
  
  // Use centralized utility for axis validation
  uint8_t axis = axisCharToIndex(argv[1]); 
  
  if (axis >= 4) {
    Serial.println("[LIMITS] ERROR: Invalid axis. Use X, Y, Z, or A");
    return;
  }
  
  float min_mm = 0.0f;
  float max_mm = 0.0f;

  if (!parseAndValidateFloat(argv[2], &min_mm, -5000.0f, 5000.0f) || 
      !parseAndValidateFloat(argv[3], &max_mm, -5000.0f, 5000.0f)) {
      Serial.println("[LIMITS] ERROR: Invalid min/max limit value.");
      return;
  }
  
  int32_t min_val = (int32_t)(min_mm * MOTION_POSITION_SCALE_FACTOR);  
  int32_t max_val = (int32_t)(max_mm * MOTION_POSITION_SCALE_FACTOR);
  
  if (min_val >= max_val) {
    Serial.println("[LIMITS] ERROR: Min value must be less than max value");
    return;
  }
  
  motionSetSoftLimits(axis, min_val, max_val);
  Serial.println("[LIMITS] ✅ Soft limits updated");
}