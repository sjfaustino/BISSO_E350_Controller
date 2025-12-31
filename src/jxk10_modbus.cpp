#include "jxk10_modbus.h"
#include "modbus_rtu.h"
#include "encoder_hal.h"
#include "rs485_device_registry.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_keys.h"
#include <Arduino.h>
#include <string.h>

// ============================================================================
// MODULE STATE
// ============================================================================

static jxk10_state_t jxk10_state = {
    .enabled = false,
    .slave_address = 1,
    .baud_rate = 9600,
    .current_raw = 0,
    .current_amps = 0.0f,
    .device_status = 0,
    .last_read_time_ms = 0,
    .last_error_time_ms = 0,
    .read_count = 0,
    .error_count = 0,
    .is_overload = false,
    .is_fault = false,
    .consecutive_errors = 0
};

// Modbus request buffer
static uint8_t modbus_tx_buffer[16];

// Forward declarations for registry callbacks
static bool jxk10Poll(void);
static bool jxk10OnResponse(const uint8_t* data, uint16_t len);

// Registry descriptor
static rs485_device_t jxk10_device = {
    .name = "JXK-10",
    .type = RS485_DEVICE_TYPE_CURRENT_SENSOR,
    .slave_address = 1,
    .poll_interval_ms = 100,
    .priority = 10,
    .enabled = false,
    .poll = jxk10Poll,
    .on_response = jxk10OnResponse,
    .last_poll_time_ms = 0,
    .poll_count = 0,
    .error_count = 0,
    .consecutive_errors = 0,
    .pending_response = false
};

// ============================================================================
// REGISTRY CALLBACKS
// ============================================================================

static bool jxk10Poll(void) {
    // Read 2 registers: Current (0x0000) and Status (0x0001)
    uint16_t tx_len = modbusReadRegistersRequest(jxk10_state.slave_address,
                                                  JXK10_REG_CURRENT, 2, modbus_tx_buffer);

    return encoderHalSend(modbus_tx_buffer, tx_len);
}

static bool jxk10OnResponse(const uint8_t* data, uint16_t len) {
    uint16_t regs[2];
    uint8_t err = modbusParseReadResponse(data, len, 2, regs);
    
    if (err != MODBUS_ERR_NONE) {
        jxk10_state.last_error_time_ms = millis();
        return false;
    }

    // Extract current value (big-endian conversion)
    // The JXK-10 returns Big-Endian, modbusParseReadResponse handles it but check if it's already swapped
    // modbusParseReadResponse extracts 16-bit values from the buffer (big-endian network order)
    
    jxk10_state.current_raw = (int16_t)regs[0];
    jxk10_state.current_amps = jxk10_state.current_raw / 100.0f;
    
    jxk10_state.device_status = regs[1];
    jxk10_state.is_overload = (jxk10_state.device_status & JXK10_STATUS_OVERLOAD) != 0;
    jxk10_state.is_fault = (jxk10_state.device_status & JXK10_STATUS_FAULT) != 0;

    jxk10_state.read_count++;
    jxk10_state.last_read_time_ms = millis();
    jxk10_state.consecutive_errors = 0;

    return true;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool jxk10ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    // Check if JXK-10 is enabled in configuration
    if (configGetInt(KEY_JXK10_EN, 1) == 0) {
        logInfo("[JXK10] JXK-10 current monitor disabled in configuration");
        jxk10_state.enabled = false;
        jxk10_device.enabled = false;
        return true;  // Not an error, just disabled
    }
    
    // Use address from config if available
    uint8_t cfg_addr = (uint8_t)configGetInt(KEY_JXK10_ADDR, slave_address);
    
    jxk10_state.slave_address = cfg_addr;
    jxk10_state.baud_rate = baud_rate;
    jxk10_state.enabled = true;  // Mark sensor as enabled
    
    // Update registry descriptor
    jxk10_device.slave_address = cfg_addr;
    jxk10_device.enabled = true;
    
    // Register with centralized bus manager
    if (!rs485RegisterDevice(&jxk10_device)) {
        logError("[JXK10] Failed to register device");
        return false;
    }

    logInfo("[JXK10] Initialized and registered (Addr: %u, Baud: %lu)",
            cfg_addr, (unsigned long)baud_rate);
    return true;
}

bool jxk10ModbusReadCurrent(void) {
    // Now handled by registry scheduler
    return rs485RequestImmediatePoll(&jxk10_device);
}

bool jxk10ModbusReceiveResponse(void) {
    // Now handled by registry update loop
    return true;
}

bool jxk10ModbusReadStatus(void) {
    // Now handled by registry scheduler (read together with current)
    return rs485RequestImmediatePoll(&jxk10_device);
}

float jxk10GetCurrentAmps(void) {
    return jxk10_state.current_amps;
}

int16_t jxk10GetCurrentRaw(void) {
    return jxk10_state.current_raw;
}

bool jxk10IsOverload(void) {
    return jxk10_state.is_overload;
}

bool jxk10IsFault(void) {
    return jxk10_state.is_fault;
}

bool jxk10ModbusSetSlaveAddress(uint8_t new_address) {
    if (new_address < 1 || new_address > 247) return false;

    // Direct write (bypass scheduler temporarily or wait for bus)
    if (!rs485IsBusAvailable()) return false;

    uint16_t tx_len = modbusWriteSingleRegisterRequest(jxk10_state.slave_address,
                                                        JXK10_REG_SLAVE_ADDR, new_address,
                                                        modbus_tx_buffer);

    if (!encoderHalSend(modbus_tx_buffer, tx_len)) return false;

    // Wait for response (blocking since this is a config change)
    delay(100);
    uint8_t rx_data[32];
    uint8_t rx_len = sizeof(rx_data);
    if (encoderHalReceive(rx_data, &rx_len) && rx_len >= 8) {
        if (modbusParseWriteResponse(rx_data, rx_len, JXK10_REG_SLAVE_ADDR, new_address) == 0) {
            // Success, now save
            tx_len = modbusWriteSingleRegisterRequest(jxk10_state.slave_address,
                                                       JXK10_REG_SAVE_CONFIG, 1,
                                                       modbus_tx_buffer);
            encoderHalSend(modbus_tx_buffer, tx_len);
            delay(100);
            
            jxk10_state.slave_address = new_address;
            jxk10_device.slave_address = new_address;
            return true;
        }
    }

    return false;
}

bool jxk10ModbusSetBaudRate(uint32_t baud_rate) {
    // For shared bus, everyone must use the same baud rate
    jxk10_state.baud_rate = baud_rate;
    return true;
}

const jxk10_state_t* jxk10GetState(void) {
    // Sync statistics from registry to state for legacy diagnostic compatibility
    jxk10_state.read_count = jxk10_device.poll_count;
    jxk10_state.error_count = jxk10_device.error_count;
    jxk10_state.consecutive_errors = jxk10_device.consecutive_errors;
    return &jxk10_state;
}

void jxk10ResetErrorCounters(void) {
    // Reset registry counters too
    jxk10_device.poll_count = 0;
    jxk10_device.error_count = 0;
    jxk10_device.consecutive_errors = 0;
}

void jxk10PrintDiagnostics(void) {
    serialLoggerLock();
    Serial.println("\n[JXK10] === Diagnostics ===");
    Serial.printf("Slave Address:       %u\n", jxk10_state.slave_address);
    Serial.printf("Baud Rate:           %lu bps\n", (unsigned long)jxk10_state.baud_rate);
    Serial.printf("Current:             %.2f A (raw: %d)\n", jxk10_state.current_amps, jxk10_state.current_raw);
    Serial.printf("Status:              0x%04X (Overload: %s, Fault: %s)\n",
                  jxk10_state.device_status,
                  jxk10_state.is_overload ? "YES" : "NO",
                  jxk10_state.is_fault ? "YES" : "NO");
    Serial.printf("Read Count:          %lu\n", (unsigned long)jxk10_device.poll_count);
    Serial.printf("Error Count:         %lu\n", (unsigned long)jxk10_device.error_count);
    Serial.printf("Consecutive Errors:  %lu\n", (unsigned long)jxk10_device.consecutive_errors);
    if (jxk10_state.last_read_time_ms > 0) {
        Serial.printf("Last Read:           %lu ms ago\n", (unsigned long)(millis() - jxk10_state.last_read_time_ms));
    }
    serialLoggerUnlock();
}
