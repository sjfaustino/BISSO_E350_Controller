#include "cli.h"
#include "serial_logger.h"
#include "motion.h"
#include "input_validation.h"
#include "system_constants.h"
#include "fault_logging.h"
#include "encoder_wj66.h"
#include "plc_iface.h"
#include "system_utilities.h" 
#include "safety.h" // For safetyIsAlarmed
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
void cmd_estop_status(int argc, char** argv); 
void cmd_estop_on(int argc, char** argv);     
void cmd_estop_off(int argc, char** argv);    

// Forward Declaration of utility functions defined elsewhere
extern const char* safetyStateToString(safety_fsm_state_t state);
extern uint8_t axisCharToIndex(char* arg); // From system_utilities.h

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterMotionCommands() {
  // Existing Motion Commands
  cliRegisterCommand("motion", "Motion status", cmd_motion_status);
  cliRegisterCommand("move", "Move axes (move X POS_MM SPEED_MM/S)", cmd_motion_move);
  cliRegisterCommand("stop", "Stop motion", cmd_motion_stop);
  cliRegisterCommand("limits", "Manage soft limits (limits axis min max | info)", cmd_soft_limits);

  // New E-Stop CLI Commands
  cliRegisterCommand("estop_status", "Show E-Stop/safety state", cmd_estop_status);
  cliRegisterCommand("estop_on", "Manually trigger Emergency Stop (critical)", cmd_estop_on);
  cliRegisterCommand("estop_off", "Clear Emergency Stop state (requires safety checks)", cmd_estop_off);
}

// ============================================================================
// E-STOP COMMAND IMPLEMENTATIONS
// ============================================================================

void cmd_estop_status(int argc, char** argv) {
    bool is_stopped = motionIsEmergencyStopped();
    safety_fsm_state_t fsm_state = safetyGetState();

    Serial.println("\n=== Emergency Stop (E-Stop) Status ===");
    
    Serial.print("[ESTOP] Motion System State: ");
    Serial.println(is_stopped ? "🔴 HALTED" : "🟢 ACTIVE");
    
    Serial.print("[ESTOP] Safety FSM State: ");
    Serial.println(safetyStateToString(fsm_state));
    
    if (is_stopped) {
        Serial.println("[ESTOP] Action: System is locked. Run 'estop_off' to recover.");
    } else {
        Serial.println("[ESTOP] Nominal: Motion commands accepted.");
    }
}

void cmd_estop_on(int argc, char** argv) {
    if (motionIsEmergencyStopped()) {
        Serial.println("[CLI] E-Stop is already active.");
        return;
    }
    
    // 1. Log the event as critical
    faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 0, 
                  "E-STOP activated via CLI command");
    
    // 2. Trigger the system halt (motion and global disable)
    motionEmergencyStop();
    
    // 3. Set the active E-Stop flag in the safety module
    emergencyStopSetActive(true);

    Serial.println("[CLI] 🔴 EMERGENCY STOP ACTIVATED by CLI. Motion halted.");
}

void cmd_estop_off(int argc, char** argv) {
    if (!motionIsEmergencyStopped()) {
        Serial.println("[CLI] E-Stop is not currently active.");
        return;
    }
    
    Serial.println("[CLI] Attempting E-Stop clearance and system recovery...");
    
    // motionClearEmergencyStop handles the safety checks (safetyIsAlarmed)
    if (motionClearEmergencyStop()) {
        // 1. Clear the active E-Stop flag in the safety module
        emergencyStopSetActive(false);
        
        Serial.println("[CLI] ✅ EMERGENCY STOP CLEARED. System Ready.");
    } else {
        Serial.println("[CLI] ❌ RECOVERY FAILED. Safety alarm is still active.");
        Serial.println("[CLI] Check 'safety' and 'faults' commands for details.");
    }
}

// ============================================================================
// EXISTING MOTION COMMAND IMPLEMENTATIONS 
// ============================================================================

void cmd_motion_status(int argc, char** argv) {
  motionDiagnostics();
}

void cmd_motion_move(int argc, char** argv) {
  if (argc < 4) {
    Serial.println("[CLI] Usage: move [AXIS] [POS_MM] [SPEED_MM/S]");
    return;
  }
  
  // NOTE: This implementation relies on utility functions not defined in this file.
  
  uint8_t target_axis = axisCharToIndex(argv[1]);
  float pos_mm = 0.0f;
  float speed_mm_s = 0.0f;
  
  if (target_axis >= 4) {
      Serial.print("[CLI] ERROR: Invalid axis: ");
      Serial.println(argv[1]);
      return;
  }
  
  if (!parseAndValidateFloat(argv[2], &pos_mm, -5000.0f, 5000.0f) || 
      !parseAndValidateFloat(argv[3], &speed_mm_s, MOTION_MIN_SPEED_MM_S, MOTION_MAX_SPEED_MM_S)) {
      Serial.print("[CLI] ERROR: Invalid position or speed value.");
      return;
  }

  // Calculate target based on single active axis
  float targets_mm[] = {0.0f, 0.0f, 0.0f, 0.0f};
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