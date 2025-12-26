/**
 * @file yhtc05_modbus.cpp
 * @brief YH-TC05 Tachometer/RPM Sensor Implementation
 * @project BISSO E350 Controller
 * @details Modbus RTU driver for YH-TC05 non-contact RPM sensor.
 */

#include "yhtc05_modbus.h"
#include "modbus_rtu.h"
#include "rs485_device_registry.h"
#include "encoder_hal.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>

// ============================================================================
// MODULE STATE
// ============================================================================

static yhtc05_state_t yhtc05_state = {
    .enabled = false,
    .slave_address = 1,
    .baud_rate = 9600,
    .rpm = 0,
    .pulse_count = 0,
    .status = 0,
    .is_spinning = false,
    .is_stalled = false,
    .stall_threshold_rpm = 100,
    .stall_time_ms = 2000,
    .stall_detect_time_ms = 0,
    .last_read_time_ms = 0,
    .last_error_time_ms = 0,
    .read_count = 0,
    .error_count = 0,
    .consecutive_errors = 0,
    .peak_rpm = 0
};

static uint8_t modbus_tx_buffer[16];
static bool was_spinning = false;
static uint32_t below_threshold_since_ms = 0;

// Device descriptor for RS485 registry
static rs485_device_t yhtc05_device = {
    .name = "YH-TC05",
    .type = RS485_DEVICE_TYPE_RPM_SENSOR,
    .slave_address = 1,
    .poll_interval_ms = 500,
    .priority = 100,
    .enabled = false,  // Disabled by default
    .poll = yhtc05ModbusReadRPM,
    .on_response = yhtc05ModbusOnResponse,
    .last_poll_time_ms = 0,
    .poll_count = 0,
    .error_count = 0,
    .consecutive_errors = 0,
    .pending_response = false
};

// ============================================================================
// INITIALIZATION
// ============================================================================

bool yhtc05ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    yhtc05_state.slave_address = slave_address;
    yhtc05_state.baud_rate = baud_rate;
    yhtc05_state.enabled = true;  // Mark sensor as enabled
    yhtc05_state.rpm = 0;
    yhtc05_state.pulse_count = 0;
    yhtc05_state.status = 0;
    yhtc05_state.is_spinning = false;
    yhtc05_state.is_stalled = false;
    yhtc05_state.read_count = 0;
    yhtc05_state.error_count = 0;
    yhtc05_state.consecutive_errors = 0;
    yhtc05_state.peak_rpm = 0;
    
    was_spinning = false;
    below_threshold_since_ms = 0;
    
    // Update device descriptor
    yhtc05_device.slave_address = slave_address;
    
    logInfo("[YH-TC05] Initialized (addr=%d, baud=%lu)",
            slave_address, (unsigned long)baud_rate);
    return true;
}

bool yhtc05RegisterWithBus(uint16_t poll_interval_ms, uint8_t priority) {
    yhtc05_device.poll_interval_ms = poll_interval_ms;
    yhtc05_device.priority = priority;
    yhtc05_device.enabled = true;
    
    if (rs485RegisterDevice(&yhtc05_device)) {
        logInfo("[YH-TC05] Registered with RS485 bus (poll=%dms, prio=%d)",
                poll_interval_ms, priority);
        return true;
    }
    
    logError("[YH-TC05] Failed to register with RS485 bus");
    return false;
}

bool yhtc05UnregisterFromBus(void) {
    yhtc05_device.enabled = false;
    return rs485UnregisterDevice(&yhtc05_device);
}

// ============================================================================
// ASYNC POLLING
// ============================================================================

bool yhtc05ModbusReadRPM(void) {
    // Build read request for RPM register (1 register)
    uint16_t frame_len = modbusReadRegistersRequest(
        yhtc05_state.slave_address,
        YHTC05_REG_RPM,
        1,  // Read 1 register (RPM only for speed)
        modbus_tx_buffer
    );
    
    // Send via encoder HAL (shared RS485)
    return encoderHalSend(modbus_tx_buffer, (uint8_t)frame_len);
}

bool yhtc05ModbusOnResponse(const uint8_t* data, uint16_t len) {
    uint16_t values[1];
    
    uint8_t err = modbusParseReadResponse(data, len, 1, values);
    if (err != MODBUS_ERR_NONE) {
        yhtc05_state.error_count++;
        yhtc05_state.consecutive_errors++;
        yhtc05_state.last_error_time_ms = millis();
        // logWarning is already handled by registry or caller if needed
        return false;
    }
    
    // Update state
    yhtc05_state.rpm = values[0];
    yhtc05_state.last_read_time_ms = millis();
    yhtc05_state.read_count++;
    yhtc05_state.consecutive_errors = 0;
    
    // Update peak tracking
    if (yhtc05_state.rpm > yhtc05_state.peak_rpm) {
        yhtc05_state.peak_rpm = yhtc05_state.rpm;
    }
    
    // Update spinning state
    bool currently_spinning = (yhtc05_state.rpm >= yhtc05_state.stall_threshold_rpm);
    yhtc05_state.is_spinning = currently_spinning;
    
    // Stall detection logic
    uint32_t now = millis();
    if (was_spinning && !currently_spinning) {
        // Just dropped below threshold
        if (below_threshold_since_ms == 0) {
            below_threshold_since_ms = now;
        }
        
        // Check if below threshold long enough
        if (now - below_threshold_since_ms >= yhtc05_state.stall_time_ms) {
            if (!yhtc05_state.is_stalled) {
                yhtc05_state.is_stalled = true;
                yhtc05_state.stall_detect_time_ms = now;
                logWarning("[YH-TC05] STALL DETECTED (RPM=%u)", yhtc05_state.rpm);
            }
        }
    } else if (currently_spinning) {
        // Spinning normally
        below_threshold_since_ms = 0;
        if (yhtc05_state.is_stalled) {
            logInfo("[YH-TC05] Recovered from stall (RPM=%u)", yhtc05_state.rpm);
        }
        yhtc05_state.is_stalled = false;
    }
    
    was_spinning = currently_spinning;
    
    return true;
}

// ============================================================================
// DATA ACCESSORS
// ============================================================================

uint16_t yhtc05GetRPM(void) {
    return yhtc05_state.rpm;
}

uint32_t yhtc05GetPulseCount(void) {
    return yhtc05_state.pulse_count;
}

bool yhtc05IsSpinning(void) {
    return yhtc05_state.is_spinning;
}

bool yhtc05IsStalled(void) {
    return yhtc05_state.is_stalled;
}

uint16_t yhtc05GetPeakRPM(void) {
    return yhtc05_state.peak_rpm;
}

const yhtc05_state_t* yhtc05GetState(void) {
    return &yhtc05_state;
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void yhtc05SetStallThreshold(uint16_t rpm_threshold, uint32_t time_ms) {
    yhtc05_state.stall_threshold_rpm = rpm_threshold;
    yhtc05_state.stall_time_ms = time_ms;
    logInfo("[YH-TC05] Stall threshold: %u RPM for %lu ms",
            rpm_threshold, (unsigned long)time_ms);
}

void yhtc05ResetStallDetection(void) {
    yhtc05_state.is_stalled = false;
    yhtc05_state.stall_detect_time_ms = 0;
    was_spinning = false;
    below_threshold_since_ms = 0;
}

void yhtc05ResetPeakRPM(void) {
    yhtc05_state.peak_rpm = 0;
}

void yhtc05ResetErrorCounters(void) {
    yhtc05_state.read_count = 0;
    yhtc05_state.error_count = 0;
    yhtc05_state.consecutive_errors = 0;
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void yhtc05PrintDiagnostics(void) {
    serialLoggerLock();
    Serial.println("\n[YH-TC05] === RPM Sensor Diagnostics ===");
    Serial.printf("Address:         %d\n", yhtc05_state.slave_address);
    Serial.printf("Baud Rate:       %lu bps\n", (unsigned long)yhtc05_state.baud_rate);
    Serial.printf("Enabled:         %s\n", yhtc05_device.enabled ? "YES" : "NO");
    
    Serial.println("\n[Measurements]");
    Serial.printf("Current RPM:     %u\n", yhtc05_state.rpm);
    Serial.printf("Peak RPM:        %u\n", yhtc05_state.peak_rpm);
    Serial.printf("Is Spinning:     %s\n", yhtc05_state.is_spinning ? "YES" : "NO");
    Serial.printf("Is Stalled:      %s\n", yhtc05_state.is_stalled ? "YES" : "NO");
    
    Serial.println("\n[Stall Detection]");
    Serial.printf("Threshold:       %u RPM\n", yhtc05_state.stall_threshold_rpm);
    Serial.printf("Time Window:     %lu ms\n", (unsigned long)yhtc05_state.stall_time_ms);
    
    Serial.println("\n[Statistics]");
    Serial.printf("Reads:           %lu\n", (unsigned long)yhtc05_state.read_count);
    Serial.printf("Errors:          %lu\n", (unsigned long)yhtc05_state.error_count);
    Serial.printf("Consec Errors:   %lu\n", (unsigned long)yhtc05_state.consecutive_errors);
    
    if (yhtc05_state.last_read_time_ms > 0) {
        Serial.printf("Last Read:       %lu ms ago\n",
                      (unsigned long)(millis() - yhtc05_state.last_read_time_ms));
    }
    Serial.println();
    serialLoggerUnlock();
}
