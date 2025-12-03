#include "cli.h"
#include "serial_logger.h"
#include "motion.h"
#include "input_validation.h"
#include "system_constants.h"
#include "fault_logging.h"
#include "encoder_wj66.h"
#include "plc_iface.h"
#include "system_utilities.h" 
#include "safety.h" 
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

extern const char* safetyStateToString(safety_fsm_state_t state);
extern uint8_t axisCharToIndex(char* arg);

void cmd_motion_status(int argc, char** argv);
void cmd_motion_move(int argc, char** argv);
void cmd_motion_stop(int argc, char** argv);
void cmd_soft_limits(int argc, char** argv);
void cmd_estop_status(int argc, char** argv); 
void cmd_estop_on(int argc, char** argv);     
void cmd_estop_off(int argc, char** argv);    

void cliRegisterMotionCommands() {
  cliRegisterCommand("motion", "Motion status", cmd_motion_status);
  cliRegisterCommand("move", "Move axes (move X POS_MM SPEED_MM/S)", cmd_motion_move);
  cliRegisterCommand("stop", "Stop motion", cmd_motion_stop);
  cliRegisterCommand("limits", "Manage soft limits", cmd_soft_limits);
  cliRegisterCommand("estop_status", "Show E-Stop/safety state", cmd_estop_status);
  cliRegisterCommand("estop_on", "Trigger E-Stop", cmd_estop_on);
  cliRegisterCommand("estop_off", "Clear E-Stop", cmd_estop_off);
}

void cmd_estop_status(int argc, char** argv) {
    bool is_stopped = motionIsEmergencyStopped();
    safety_fsm_state_t fsm_state = safetyGetState();

    Serial.println("\n=== E-STOP STATUS ===");
    Serial.printf("Motion State: %s\n", is_stopped ? "[HALTED]" : "[ACTIVE]");
    Serial.printf("Safety FSM:   %s\n", safetyStateToString(fsm_state));
    
    if (is_stopped) {
        Serial.println("Action: System locked. Run 'estop_off' to recover.");
    } else {
        Serial.println("Status: Nominal.");
    }
}

void cmd_estop_on(int argc, char** argv) {
    if (motionIsEmergencyStopped()) {
        Serial.println("[CLI] E-Stop already active.");
        return;
    }
    faultLogEntry(FAULT_CRITICAL, FAULT_ESTOP_ACTIVATED, -1, 0, "E-STOP via CLI");
    emergencyStopSetActive(true);
    Serial.println("[CLI] [ALERT] E-STOP ACTIVATED. Motion halted.");
}

void cmd_estop_off(int argc, char** argv) {
    if (!motionIsEmergencyStopped()) {
        Serial.println("[CLI] E-Stop not active.");
        return;
    }
    Serial.println("[CLI] Attempting recovery...");
    if (motionClearEmergencyStop()) {
        emergencyStopSetActive(false);
        Serial.println("[CLI] [OK] E-STOP CLEARED. System Ready.");
    } else {
        Serial.println("[CLI] [FAIL] Recovery failed. Safety alarm active.");
    }
}

void cmd_motion_status(int argc, char** argv) {
  motionDiagnostics();
}

void cmd_motion_move(int argc, char** argv) {
  if (argc < 4) {
    Serial.println("[CLI] Usage: move [AXIS] [POS_MM] [SPEED_MM/S]");
    return;
  }
  uint8_t target_axis = axisCharToIndex(argv[1]);
  float pos_mm = 0.0f;
  float speed_mm_s = 0.0f;
  
  if (target_axis >= 4) {
      Serial.printf("[CLI] [ERR] Invalid axis: %s\n", argv[1]);
      return;
  }
  if (!parseAndValidateFloat(argv[2], &pos_mm, -5000.0f, 5000.0f) || 
      !parseAndValidateFloat(argv[3], &speed_mm_s, MOTION_MIN_SPEED_MM_S, MOTION_MAX_SPEED_MM_S)) {
      Serial.println("[CLI] [ERR] Invalid parameters.");
      return;
  }
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
    Serial.println("[LIMITS] Usage: limits info | limits [AXIS] [MIN] [MAX]");
    return;
  }
  if (strcmp(argv[1], "info") == 0) {
    motionDiagnostics();
    return;
  }
  if (argc < 4) {
    Serial.println("[LIMITS] Error: Missing values");
    return;
  }
  uint8_t axis = axisCharToIndex(argv[1]); 
  if (axis >= 4) {
    Serial.println("[LIMITS] Error: Invalid axis");
    return;
  }
  float min_mm = 0.0f;
  float max_mm = 0.0f;
  if (!parseAndValidateFloat(argv[2], &min_mm, -5000.0f, 5000.0f) || 
      !parseAndValidateFloat(argv[3], &max_mm, -5000.0f, 5000.0f)) {
      Serial.println("[LIMITS] Error: Invalid limit value.");
      return;
  }
  int32_t min_val = (int32_t)(min_mm * MOTION_POSITION_SCALE_FACTOR);  
  int32_t max_val = (int32_t)(max_mm * MOTION_POSITION_SCALE_FACTOR);
  
  if (min_val >= max_val) {
    Serial.println("[LIMITS] Error: Min must be < Max");
    return;
  }
  motionSetSoftLimits(axis, min_val, max_val);
  Serial.println("[LIMITS] [OK] Soft limits updated");
}