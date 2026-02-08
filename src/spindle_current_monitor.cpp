/**
 * @file spindle_current_monitor.cpp
 * @brief Spindle Current Monitoring System Implementation
 * @project BISSO E350 Controller - Phase 5.0
 */

#include "spindle_current_monitor.h"
#include "fault_logging.h"
#include "jxk10_modbus.h"
#include "altivar31_modbus.h"
#include "motion.h"
#include "serial_logger.h"
#include "config_cache.h"
#include "config_keys.h"
#include "system_tuning.h"
#include "cli.h" // Added for table diagnostics
#include <Arduino.h>
#include <string.h>


// Global monitor state (disabled by default for testing without VFD)
// Default thresholds tuned for FIMET M 180L4 motor:
//   - 22kW, 1460 RPM, cos φ 0.85
//   - Rated current: 24.5A @ 400V (Star), 42A @ 230V (Delta)
//   - Overcurrent threshold: 30A (122% of rated, allows startup surge)
//   - Stall threshold: 25A (102% of rated)
static spindle_monitor_state_t monitor_state = {
    .enabled = false,
    .poll_interval_ms = SPINDLE_MONITOR_POLL_DEFAULT_MS,
    .last_poll_time_ms = 0,
    .overcurrent_threshold_amps = SPINDLE_OVERCURRENT_DEFAULT_AMPS,  // 122% of 24.5A rated
    .current_amps = 0.0f,
    .current_peak_amps = 0.0f,
    .current_average_amps = 0.0f,
    .current_previous_amps = 0.0f,
    .tool_breakage_drop_amps = SPINDLE_DROP_DEFAULT_AMPS,
    .stall_threshold_amps = SPINDLE_STALL_DEFAULT_AMPS,
    .stall_timeout_ms = SPINDLE_STALL_TIMEOUT_DEFAULT_MS,
    .auto_pause_enabled = true,
    .auto_pause_threshold_amps = 25.0f,
    .auto_paused = false,
    .auto_pause_count = 0,
    .alarm_tool_breakage = false,
    .alarm_stall = false,
    .alarm_overload = false,
    .overload_start_time_ms = 0,
    .read_count = 0,
    .error_count = 0,
    .overload_count = 0,
    .shutdown_count = 0,
    .tool_breakage_count = 0,
    .stall_count = 0,
    .last_shutdown_time_ms = 0,
    .last_shutdown_current_amps = 0.0f,
    .jxk10_slave_address = 1,
    .jxk10_baud_rate = 9600};

static uint32_t last_check_time_ms = 0;

  // Load global RS485 baud for all Modbus devices (PHASE 6.7 Typed Cache)
  uint32_t rs485_baud = g_config.rs485_baud;
  
  // Initialize JXK-10 driver (registry registration happens here)
  if (!jxk10ModbusInit(jxk10_address, rs485_baud)) {
    logError("[SPINDLE] Failed to initialize JXK-10 driver");
    return false;
  }

  // Initialize VFD driver (registry registration happens here)
  uint8_t vfd_address = (uint8_t)configGetInt(KEY_VFD_ADDR, 2);
  altivar31ModbusInit(vfd_address, rs485_baud);

  // Set monitor parameters
  monitor_state.enabled = true;
  monitor_state.jxk10_slave_address = jxk10_address;
  monitor_state.jxk10_baud_rate = rs485_baud;
  monitor_state.overcurrent_threshold_amps = threshold_amps;
  monitor_state.poll_interval_ms = SPINDLE_MONITOR_POLL_DEFAULT_MS;
  monitor_state.last_poll_time_ms = millis();
  
  // Load auto-pause config from typed cache (PHASE 6.7)
  monitor_state.auto_pause_enabled = g_config.strict_limits; // Fallback to strict_limits for safety
  monitor_state.auto_pause_enabled = (configGetInt(KEY_SPINDL_PAUSE_EN, 1) != 0); // Still use specific key if possible
  monitor_state.auto_pause_threshold_amps = (float)g_config.spindle_pause_threshold;
  monitor_state.auto_paused = false;

  logInfo("[SPINDLE] Initialized (JXK-10 ID: %u, Threshold: %.1f A)",
                jxk10_address, threshold_amps);
  if (monitor_state.auto_pause_enabled) {
    logInfo("[SPINDLE] Auto-pause enabled at %.1f A", monitor_state.auto_pause_threshold_amps);
  }
  return true;
}


void spindleMonitorSetEnabled(bool enable) {
  monitor_state.enabled = enable;
  if (enable) {
    logInfo("[SPINDLE] Monitoring ENABLED");
  } else {
    logInfo("[SPINDLE] Monitoring DISABLED");
  }
}

bool spindleMonitorIsEnabled(void) { return monitor_state.enabled; }

void spindleMonitorSetThreshold(float threshold_amps) {
  if (threshold_amps < 0.0f || threshold_amps > 50.0f) {
    return; // Out of range
  }
  monitor_state.overcurrent_threshold_amps = threshold_amps;
}

float spindleMonitorGetThreshold(void) {
  return monitor_state.overcurrent_threshold_amps;
}

void spindleMonitorSetPollInterval(uint32_t interval_ms) {
  if (interval_ms < 100 || interval_ms > 60000) {
    return; // Out of range (100ms to 60s)
  }
  monitor_state.poll_interval_ms = interval_ms;
}

uint32_t spindleMonitorGetPollInterval(void) {
  return monitor_state.poll_interval_ms;
}

bool spindleMonitorUpdate(void) {
  if (!monitor_state.enabled) {
    return false;
  }

  uint32_t now = millis();
  if (now - last_check_time_ms < SPINDLE_MONITOR_RATE_LIMIT_MS) {
      return false; // Rate limit logic check
  }
  last_check_time_ms = now;

  // Read latest current from JXK-10 (polled by registry)
  float current = jxk10GetCurrentAmps();
  monitor_state.current_amps = current;

  // Update statistics and peaks
  if (current > monitor_state.current_peak_amps) {
      monitor_state.current_peak_amps = current;
  }

  // Auto-pause feature: Pause motion before hitting critical threshold
  // This protects blades by pausing instead of triggering E-Stop
  if (monitor_state.auto_pause_enabled && !monitor_state.auto_paused) {
    if (current > monitor_state.auto_pause_threshold_amps && 
        current < monitor_state.overcurrent_threshold_amps) {
      // Overload but not critical - pause and alert
      motionPause();
      monitor_state.auto_paused = true;
      monitor_state.auto_pause_count++;
      
      faultLogWarning(FAULT_SPINDLE_OVERCURRENT, "Spindle Auto-Pause Triggered");
      logWarning("[SPINDLE] AUTO-PAUSE: Current %.1f A exceeds %.1f A threshold",
                 current, monitor_state.auto_pause_threshold_amps);
      logInfo("[SPINDLE] Motion paused. Reduce load and use 'resume' to continue.");
      
      // Alert operator
      extern void alertWarning(void);
      alertWarning();
    }
  }
  
  // Auto-recovery: If we auto-paused and current dropped, log it
  if (monitor_state.auto_paused && current < (monitor_state.auto_pause_threshold_amps * 0.8f)) {
    logInfo("[SPINDLE] Current normalized (%.1f A). Use 'resume' to continue.", current);
    // Don't auto-resume - let operator decide
  }

  // Check for critical overcurrent condition (E-Stop threshold)
  if (current > monitor_state.overcurrent_threshold_amps) {
    monitor_state.overload_count++;
    spindleMonitorTriggerShutdown();
  }

  return true;
}

float spindleMonitorGetCurrent(void) {
  return monitor_state.current_amps;
}

float spindleMonitorGetPeakCurrent(void) {
  return monitor_state.current_peak_amps;
}

bool spindleMonitorIsOvercurrent(void) {
  if (!monitor_state.enabled) return false;
  return monitor_state.current_amps > monitor_state.overcurrent_threshold_amps;
}

// NOTE: Status register not documented in JXK-10 PDF, these always return false
bool spindleMonitorIsOverload(void) { return false; }

bool spindleMonitorIsFault(void) { return false; }

void spindleMonitorTriggerShutdown(void) {
  monitor_state.shutdown_count++;
  monitor_state.last_shutdown_time_ms = millis();
  monitor_state.last_shutdown_current_amps = monitor_state.current_amps;

  // Log to fault history
  faultLogError(FAULT_SPINDLE_OVERCURRENT,
                "Spindle overcurrent - triggering emergency shutdown");

  // Trigger safety shutdown
  logError(
      "[SPINDLE] OVERCURRENT SHUTDOWN! Current: %.1f A (Threshold: %.1f A)",
      monitor_state.current_amps, monitor_state.overcurrent_threshold_amps);

  motionEmergencyStop();
}

const spindle_monitor_state_t *spindleMonitorGetState(void) {
  return &monitor_state;
}

float spindleMonitorGetLoadPercent(void) {
    if (!monitor_state.enabled) return 0.0f;
    float rated = g_config.spindle_rated_amps;
    if (rated < 0.1f) return 0.0f;
    return (monitor_state.current_amps / rated) * 100.0f;
}

void spindleMonitorResetStats(void) {
  monitor_state.read_count = 0;
  monitor_state.error_count = 0;
  monitor_state.overload_count = 0;
  monitor_state.shutdown_count = 0;
  monitor_state.current_peak_amps = 0.0f;

  jxk10ResetErrorCounters();
}

void spindleMonitorPrintDiagnostics(void) {
  serialLoggerLock();
  logPrintln("\n[SPINDLE] === Current Monitor Diagnostics ===\n");
  
  cliPrintTableHeader(25, 18, 0);
  cliPrintTableRow("Metric", "Value", nullptr, 25, 18, 0);
  cliPrintTableDivider(25, 18, 0);
  
  cliPrintTableRow("Status", monitor_state.enabled ? "ENABLED" : "DISABLED", nullptr, 25, 18, 0);
  
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f A (%.1f%%)", monitor_state.current_amps, spindleMonitorGetLoadPercent());
  cliPrintTableRow("Current Load", buf, nullptr, 25, 18, 0);
  
  snprintf(buf, sizeof(buf), "%.2f A", monitor_state.current_peak_amps);
  cliPrintTableRow("Peak Current", buf, nullptr, 25, 18, 0);
  
  snprintf(buf, sizeof(buf), "%.1f A", monitor_state.overcurrent_threshold_amps);
  cliPrintTableRow("Shutdown Threshold", buf, nullptr, 25, 18, 0);
  
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)monitor_state.overload_count);
  cliPrintTableRow("Overload Events", buf, nullptr, 25, 18, 0);
  
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)monitor_state.shutdown_count);
  cliPrintTableRow("Shutdown Events", buf, nullptr, 25, 18, 0);
  
  cliPrintTableFooter(25, 18, 0);
  
  // Alarm Status Table
  logPrintln("\n[SPINDLE] === Alarm Configuration ===\n");
  cliPrintTableHeader(25, 18, 0);
  cliPrintTableRow("Alarm Type", "Status/Threshold", nullptr, 25, 18, 0);
  cliPrintTableDivider(25, 18, 0);
  
  snprintf(buf, sizeof(buf), "%s (%.1f A drop)", monitor_state.alarm_tool_breakage ? "ACTIVE" : "OK", monitor_state.tool_breakage_drop_amps);
  cliPrintTableRow("Tool Breakage", buf, nullptr, 25, 18, 0);
  
  snprintf(buf, sizeof(buf), "%s (%.1f A)", monitor_state.alarm_stall ? "ACTIVE" : "OK", monitor_state.stall_threshold_amps);
  cliPrintTableRow("Stall", buf, nullptr, 25, 18, 0);
  
  cliPrintTableFooter(25, 18, 0);
  
  serialLoggerUnlock();
}

// === ALARM API FUNCTIONS ===

bool spindleMonitorIsToolBreakage(void) {
  return monitor_state.alarm_tool_breakage;
}

bool spindleMonitorIsStall(void) {
  return monitor_state.alarm_stall;
}

void spindleMonitorClearAlarms(void) {
  monitor_state.alarm_tool_breakage = false;
  monitor_state.alarm_stall = false;
  monitor_state.alarm_overload = false;
  monitor_state.overload_start_time_ms = 0;
  logInfo("[SPINDLE] All alarms cleared");
}

void spindleMonitorSetToolBreakageThreshold(float drop_amps) {
  if (drop_amps >= 1.0f && drop_amps <= 20.0f) {
    monitor_state.tool_breakage_drop_amps = drop_amps;
    logInfo("[SPINDLE] Tool breakage threshold set to %.1f A", drop_amps);
  }
}

void spindleMonitorSetStallParams(float threshold_amps, uint32_t timeout_ms) {
  if (threshold_amps >= 5.0f && threshold_amps <= 50.0f) {
    monitor_state.stall_threshold_amps = threshold_amps;
  }
  if (timeout_ms >= 500 && timeout_ms <= 10000) {
    monitor_state.stall_timeout_ms = timeout_ms;
  }
  logInfo("[SPINDLE] Stall params: %.1f A for %lu ms",
                monitor_state.stall_threshold_amps,
                (unsigned long)monitor_state.stall_timeout_ms);
}
