/**
 * @file cli_motion.cpp
 * @brief Motion CLI Commands
 * @project PosiPro
 */

#include "cli.h"
#include "motion.h"
#include "axis.h"
#include "motion_state.h" // <-- CRITICAL FIX: Provides status accessors
#include "serial_logger.h"
#include "input_validation.h" 
#include "axis_utilities.h"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// STATUS & DIAGNOSTICS
// ============================================================================

void cmd_motion_status(int argc, char** argv) {
  motionDiagnostics();
}

void cmd_predict_status(int argc, char** argv) {
  uint8_t axis = 0;
  if (argc > 1) axis = axisCharToIndex(argv[1]);
  if (axis == 255) axis = 0;

  const Axis* a = motionGetAxis(axis);
  if (!serialLoggerLock()) return;
  logDirectPrintln("\n=== PREDICTION DIAGNOSTICS ===\n");
  cliPrintTableHeader(18, 22, 0, 0, 0);
  cliPrintTableRow("Metric", "Value", nullptr, 18, 22, 0, nullptr, 0, nullptr, 0);
  cliPrintTableDivider(18, 22, 0, 0, 0);
  
  char buf1[32], buf2[32];
  snprintf(buf1, sizeof(buf1), "Axis %d (%c)", axis, axisIndexToChar(axis));
  cliPrintTableRow("Target Axis", buf1, nullptr, 18, 22, 0, nullptr, 0, nullptr, 0);
  
  snprintf(buf1, sizeof(buf1), "%ld", (long)a->position);
  cliPrintTableRow("Raw Position", buf1, nullptr, 18, 22, 0, nullptr, 0, nullptr, 0);
  
  snprintf(buf1, sizeof(buf1), "%ld", (long)a->last_actual_position);
  cliPrintTableRow("Actual Latched", buf1, nullptr, 18, 22, 0, nullptr, 0, nullptr, 0);
  
  snprintf(buf1, sizeof(buf1), "%ld", (long)a->predicted_position);
  cliPrintTableRow("Predicted", buf1, nullptr, 18, 22, 0, nullptr, 0, nullptr, 0);
  
  snprintf(buf1, sizeof(buf1), "%ld", (long)(a->predicted_position - a->last_actual_position));
  cliPrintTableRow("Prediction Gap", buf1, nullptr, 18, 22, 0, nullptr, 0, nullptr, 0);
  
  snprintf(buf1, sizeof(buf1), "%.3f counts/ms", a->velocity_counts_ms);
  cliPrintTableRow("Velocity", buf1, nullptr, 18, 22, 0, nullptr, 0, nullptr, 0);
  
  snprintf(buf1, sizeof(buf1), "%lu ms ago", (unsigned long)(millis() - a->last_actual_update_ms));
  cliPrintTableRow("Update Age", buf1, nullptr, 18, 22, 0, nullptr, 0, nullptr, 0);
  
  cliPrintTableFooter(18, 22, 0, 0, 0);
  serialLoggerUnlock();
}

void cmd_estop_status(int argc, char** argv) {
  if (motionIsEmergencyStopped()) {
    logPrintf("EMERGENCY STOP ACTIVE\n");
  } else {
    logPrintf("System Enabled\n");
  }
}

// ============================================================================
// CONTROL COMMANDS
// ============================================================================

void cmd_motion_stop(int argc, char** argv) {
  motionStop();
  logPrintf("Stop command sent\n");
}

void cmd_motion_pause(int argc, char** argv) {
  motionPause();
  logPrintf("Pause command sent\n");
}

void cmd_motion_resume(int argc, char** argv) {
  motionResume();
  logPrintf("Resume command sent\n");
}

void cmd_estop_on(int argc, char** argv) {
  motionEmergencyStop();
  logError("CRITICAL: E-STOP TRIGGERED BY USER");
}

void cmd_estop_off(int argc, char** argv) {
  if (motionClearEmergencyStop()) {
    logPrintf("E-Stop Cleared\n");
  } else {
    logPrintf("Could not clear E-Stop (Check Safety Alarms)\n");
  }
}

void cmd_estop_main(int argc, char** argv) {
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"status", cmd_estop_status, "Show E-Stop status"},
        {"on",     cmd_estop_on,     "Trigger E-Stop"},
        {"off",    cmd_estop_off,    "Clear E-Stop"}
    };
    
    // Default to status if no subcommand provided
    if (argc < 2) {
        cmd_estop_status(argc, argv);
        return;
    }
    
    cliDispatchSubcommand("", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

// ============================================================================
// CONFIGURATION COMMANDS
// ============================================================================

void cmd_soft_limits(int argc, char** argv) {
  if (argc < 4) {
    CLI_USAGE("limit", "<axis> <min> <max> [enable]");
    return;
  }
  
  uint8_t axis = axisCharToIndex(argv[1]);
  if (axis == 255) {
    logPrintf("Invalid axis\n");
    return;
  }
  
  int32_t min_pos = atol(argv[2]);
  int32_t max_pos = atol(argv[3]);
  motionSetSoftLimits(axis, min_pos, max_pos);
  
  if (argc >= 5) {
    bool enable = (atoi(argv[4]) > 0);
    motionEnableSoftLimits(axis, enable);
  }
  
  logPrintf("Soft limits updated for Axis %d\n", axis);
}

void cmd_feed_override(int argc, char** argv) {
    if (argc < 2) {
        logPrintf("Current Feed: %.0f%%\n", motionGetFeedOverride() * 100.0f);
        return;
    }

    float factor = atof(argv[1]);

    // Support percentage input (e.g., "150" -> 1.5)
    if (factor > 10.0f) factor /= 100.0f;

    motionSetFeedOverride(factor);
    logPrintf("Feed override set to %.2f\n", factor);
}

// ============================================================================
// PERFORMANCE DIAGNOSTICS
// ============================================================================


static void wrap_spinlock_stats(int argc, char** argv) { (void)argc; (void)argv; motionPrintSpinlockStats(); }
static void wrap_spinlock_reset(int argc, char** argv) { (void)argc; (void)argv; motionResetSpinlockStats(); }

void cmd_spinlock_main(int argc, char** argv) {
    // Table-driven subcommand dispatch (P1: DRY improvement)
    static const cli_subcommand_t subcmds[] = {
        {"stats", wrap_spinlock_stats, "Show critical section timing report"},
        {"reset", wrap_spinlock_reset, "Reset timing statistics"}
    };
    
    if (argc < 2) {
        logPrintln("=== Spinlock Timing Diagnostics ===");
        CLI_USAGE("spinlock", "[stats | reset]");
        logPrintln("Purpose: Audit spinlock durations (>10us -> mutex)");
        // Usage printed by helper below anyway if we pass argc < 2, but let's keep the header
    }

    cliDispatchSubcommand("", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

// ============================================================================
// REGISTRATION
// ============================================================================

void cliRegisterMotionCommands() {
  cliRegisterCommand("motionstatus", "Show low-level motion status", cmd_motion_status);
  cliRegisterCommand("stop", "Stop all motion", cmd_motion_stop);
  cliRegisterCommand("pause", "Pause motion", cmd_motion_pause);
  cliRegisterCommand("resume", "Resume motion", cmd_motion_resume);

  cliRegisterCommand("estop", "Emergency Stop management (status|on|off)", cmd_estop_main);

  cliRegisterCommand("limit", "Set soft limits", cmd_soft_limits);
  cliRegisterCommand("feed", "Set Feed Override (0.1 - 2.0)", cmd_feed_override);

  cliRegisterCommand("predict", "Show position prediction diagnostics [axis]", cmd_predict_status);
  cliRegisterCommand("spinlock", "Spinlock timing diagnostics (stats|reset)", cmd_spinlock_main);
}
