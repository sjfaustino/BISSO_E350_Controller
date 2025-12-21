/**
 * @file spindle_current_monitor.cpp
 * @brief Spindle Current Monitoring System Implementation
 * @project BISSO E350 Controller - Phase 5.0
 */

#include "spindle_current_monitor.h"
#include "fault_logging.h"
#include "jxk10_modbus.h"
#include "motion.h"
#include "serial_logger.h"
#include "spindle_current_rs485.h"
#include <Arduino.h>
#include <string.h>


// Global monitor state (disabled by default for testing without VFD)
static spindle_monitor_state_t monitor_state = {
    .enabled = false,
    .poll_interval_ms = 1000,
    .last_poll_time_ms = 0,
    .overcurrent_threshold_amps = 30.0f,
    .current_amps = 0.0f,
    .current_peak_amps = 0.0f,
    .current_average_amps = 0.0f,
    .read_count = 0,
    .error_count = 0,
    .overload_count = 0,
    .shutdown_count = 0,
    .last_shutdown_time_ms = 0,
    .last_shutdown_current_amps = 0.0f,
    .jxk10_slave_address = 1,
    .jxk10_baud_rate = 9600};

// State machine for non-blocking Modbus polling
typedef enum {
  POLL_STATE_IDLE = 0,
  POLL_STATE_SWITCH_DEVICE = 1,
  POLL_STATE_SEND_REQUEST = 2,
  POLL_STATE_WAIT_RESPONSE = 3,
  POLL_STATE_PARSE_RESPONSE = 4
} poll_state_t;

static poll_state_t poll_state = POLL_STATE_IDLE;
static uint32_t poll_state_time_ms = 0;

bool spindleMonitorInit(uint8_t jxk10_address, float threshold_amps) {
  // Initialize RS485 multiplexer
  if (!rs485MuxInit()) {
    Serial.println("[SPINDLE] Failed to initialize RS485 multiplexer");
    return false;
  }

  // Initialize JXK-10 driver
  if (!jxk10ModbusInit(jxk10_address, 9600)) {
    Serial.println("[SPINDLE] Failed to initialize JXK-10 driver");
    return false;
  }

  // Set monitor parameters
  monitor_state.enabled = true;
  monitor_state.jxk10_slave_address = jxk10_address;
  monitor_state.jxk10_baud_rate = 9600;
  monitor_state.overcurrent_threshold_amps = threshold_amps;
  monitor_state.poll_interval_ms = 1000;
  monitor_state.last_poll_time_ms = millis();

  poll_state = POLL_STATE_IDLE;

  Serial.printf("[SPINDLE] Initialized (JXK-10 ID: %u, Threshold: %.1f A)\n",
                jxk10_address, threshold_amps);
  return true;
}

void spindleMonitorSetEnabled(bool enable) {
  monitor_state.enabled = enable;
  if (enable) {
    Serial.println("[SPINDLE] Monitoring ENABLED");
  } else {
    Serial.println("[SPINDLE] Monitoring DISABLED");
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
  // If monitoring is disabled, do nothing
  if (!monitor_state.enabled) {
    return false;
  }

  uint32_t now = millis();

  // State machine for non-blocking Modbus polling
  switch (poll_state) {
  case POLL_STATE_IDLE: {
    // Check if it's time to poll
    if ((now - monitor_state.last_poll_time_ms) <
        monitor_state.poll_interval_ms) {
      return false; // Not yet time to poll
    }

    // Time to start polling - switch to spindle device
    poll_state = POLL_STATE_SWITCH_DEVICE;
    poll_state_time_ms = now;
    return false;
  }

  case POLL_STATE_SWITCH_DEVICE: {
    // Request device switch if needed
    if (rs485MuxGetCurrentDevice() != RS485_DEVICE_SPINDLE) {
      rs485MuxSwitchDevice(RS485_DEVICE_SPINDLE);
    }

    // Check if switch is complete
    if (rs485MuxUpdate()) {
      poll_state = POLL_STATE_SEND_REQUEST;
    }

    // Timeout: if we've been waiting too long, abort
    if ((now - poll_state_time_ms) > 100) {
      monitor_state.error_count++;
      poll_state = POLL_STATE_IDLE;
    }
    return false;
  }

  case POLL_STATE_SEND_REQUEST: {
    // Send Modbus read current request
    if (jxk10ModbusReadCurrent()) {
      poll_state = POLL_STATE_WAIT_RESPONSE;
      poll_state_time_ms = now;
    } else {
      monitor_state.error_count++;
      poll_state = POLL_STATE_IDLE;
    }
    return false;
  }

  case POLL_STATE_WAIT_RESPONSE: {
    // Wait for Modbus response (minimum 50ms after request)
    if ((now - poll_state_time_ms) < 50) {
      return false; // Still waiting
    }

    poll_state = POLL_STATE_PARSE_RESPONSE;
    return false;
  }

  case POLL_STATE_PARSE_RESPONSE: {
    // Parse Modbus response
    if (jxk10ModbusReceiveResponse()) {
      // Successfully read current
      float current = jxk10GetCurrentAmps();
      monitor_state.current_amps = current;
      monitor_state.read_count++;

      // Update peak current
      if (current > monitor_state.current_peak_amps) {
        monitor_state.current_peak_amps = current;
      }

      // Update running average (simple moving average)
      if (monitor_state.read_count == 1) {
        monitor_state.current_average_amps = current;
      } else {
        monitor_state.current_average_amps =
            (monitor_state.current_average_amps * 0.8f) + (current * 0.2f);
      }

      // Check for overcurrent condition
      if (current > monitor_state.overcurrent_threshold_amps) {
        monitor_state.overload_count++;
        spindleMonitorTriggerShutdown();
      }
    } else {
      monitor_state.error_count++;
    }

    // Switch back to encoder device for continued normal operation
    rs485MuxSwitchDevice(RS485_DEVICE_ENCODER);
    rs485MuxUpdate();

    monitor_state.last_poll_time_ms = now;
    poll_state = POLL_STATE_IDLE;
    return true; // Poll cycle completed
  }

  default:
    poll_state = POLL_STATE_IDLE;
    return false;
  }
}

float spindleMonitorGetCurrent(void) { return monitor_state.current_amps; }

float spindleMonitorGetPeakCurrent(void) {
  return monitor_state.current_peak_amps;
}

bool spindleMonitorIsOvercurrent(void) {
  return (monitor_state.current_amps >
          monitor_state.overcurrent_threshold_amps);
}

bool spindleMonitorIsOverload(void) { return jxk10IsOverload(); }

bool spindleMonitorIsFault(void) { return jxk10IsFault(); }

void spindleMonitorTriggerShutdown(void) {
  monitor_state.shutdown_count++;
  monitor_state.last_shutdown_time_ms = millis();
  monitor_state.last_shutdown_current_amps = monitor_state.current_amps;

  // Log to fault history
  faultLogError(FAULT_SPINDLE_OVERCURRENT,
                "Spindle overcurrent - triggering emergency shutdown");

  // Trigger safety shutdown
  Serial.printf(
      "[SPINDLE] OVERCURRENT SHUTDOWN! Current: %.1f A (Threshold: %.1f A)\n",
      monitor_state.current_amps, monitor_state.overcurrent_threshold_amps);

  motionEmergencyStop();
}

const spindle_monitor_state_t *spindleMonitorGetState(void) {
  return &monitor_state;
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
  Serial.println("\n[SPINDLE] === Current Monitor Diagnostics ===");
  Serial.printf("Status:              %s\n",
                monitor_state.enabled ? "ENABLED" : "DISABLED");
  Serial.printf("Current:             %.2f A\n", monitor_state.current_amps);
  Serial.printf("Peak Current:        %.2f A\n",
                monitor_state.current_peak_amps);
  Serial.printf("Average Current:     %.2f A\n",
                monitor_state.current_average_amps);
  Serial.printf("Overcurrent Threshold: %.1f A\n",
                monitor_state.overcurrent_threshold_amps);
  Serial.printf("Poll Interval:       %lu ms\n",
                (unsigned long)monitor_state.poll_interval_ms);
  Serial.printf("Read Count:          %lu\n",
                (unsigned long)monitor_state.read_count);
  Serial.printf("Error Count:         %lu\n",
                (unsigned long)monitor_state.error_count);
  Serial.printf("Overload Events:     %lu\n",
                (unsigned long)monitor_state.overload_count);
  Serial.printf("Shutdown Events:     %lu\n",
                (unsigned long)monitor_state.shutdown_count);
  if (monitor_state.shutdown_count > 0) {
    Serial.printf(
        "Last Shutdown:       %lu ms ago @ %.1f A\n",
        (unsigned long)(millis() - monitor_state.last_shutdown_time_ms),
        monitor_state.last_shutdown_current_amps);
  }
}
