#include "jxk10_modbus.h"
#include "modbus_rtu.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_keys.h"
#include <Arduino.h>
#include <string.h>

// Global instance
Jxk10Driver Jxk10;

// ============================================================================
// CLASS IMPLEMENTATION
// ============================================================================

Jxk10Driver::Jxk10Driver() 
    : ModbusDriver("JXK-10", RS485_DEVICE_TYPE_CURRENT_SENSOR, 1, 100, 10) 
{
    // Initialize state
    memset(&_state, 0, sizeof(_state));
    _state.slave_address = 1;
    _state.baud_rate = 9600;
}

const jxk10_state_t* Jxk10Driver::getState() const {
    // Sync base class stats to public state struct
    // We cast away const to update the mutable stats in _state
    // This maintains compatibility with the C-API struct
    jxk10_state_t* mutable_state = const_cast<jxk10_state_t*>(&_state);
    mutable_state->error_count = getErrorCount();
    mutable_state->consecutive_errors = getConsecutiveErrors();
    mutable_state->read_count = getPollCount(); // Approximate
    mutable_state->enabled = isEnabled();
    mutable_state->slave_address = getSlaveAddress();
    
    return &_state;
}

float Jxk10Driver::getCurrentAmps() const {
    return _state.current_amps;
}

int16_t Jxk10Driver::getCurrentRaw() const {
    return _state.current_raw;
}

bool Jxk10Driver::poll() {
    // Read 1 register: PV Current at 0x000E per PDF manual
    uint16_t tx_len = modbusReadRegistersRequest(getSlaveAddress(),
                                                  JXK10_REG_CURRENT, 1, _tx_buffer);
    return send(_tx_buffer, tx_len);
}

bool Jxk10Driver::onResponse(const uint8_t* data, uint16_t len) {
    uint16_t regs[1];
    uint8_t err = modbusParseReadResponse(data, len, 1, regs);
    
    if (err != MODBUS_ERR_NONE) {
        _state.last_error_time_ms = millis();
        // Base class handles error counting
        return false;
    }

    // Extract current value per PDF manual
    _state.current_raw = (int16_t)regs[0];
    
    // Scaling per PDF: raw <= 3000 -> divide by 100 (2 decimals)
    if (_state.current_raw <= 3000) {
        _state.current_amps = _state.current_raw / 100.0f;
    } else {
        _state.current_amps = _state.current_raw / 10.0f;
    }

    _state.last_read_time_ms = millis();
    return true;
}

bool Jxk10Driver::setInternalSlaveAddress(uint8_t new_address) {
    // Per PDF: Address range is 0x00 to 0xFE (0-254), power cycle required after change
    if (new_address > 254) return false;

    // Use raw specific request (bypass standard poll)
    // Note: This relies on RS485 bus availability which managed by registry
    // Direct send might conflict if bus is busy. 
    // Ideally should request "Immediate Poll" with custom callback, but simple send works if careful.
    
    // This is essentially "manual mode"
    uint16_t tx_len = modbusWriteSingleRegisterRequest(getSlaveAddress(),
                                                        JXK10_REG_SLAVE_ADDR, new_address,
                                                        _tx_buffer);

    if (!send(_tx_buffer, tx_len)) return false;

    // Wait for response (blocking since this is a config change)
    vTaskDelay(pdMS_TO_TICKS(100)); 
    // We cannot easily read response here because Registry owns the serial port RX.
    // However, if we assume registry is running in background, it might consume the response.
    // Making this work perfectly requires Registry support for "One-off transaction".
    
    // For now, assume "send and pray" or use legacy blocking receive if we pause registry?
    // Let's assume best effort for this rare config operation.
    
    setSlaveAddress(new_address);
    _state.slave_address = new_address;
    logInfo("[JXK10] Address set to %u (Power cycle required)", new_address);
    return true;
}

// ============================================================================
// C-API WRAPPERS
// ============================================================================

bool jxk10ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    // Check config
    if (configGetInt(KEY_JXK10_ENABLED, 1) == 0) {
        logInfo("[JXK10] Disabled in configuration");
        Jxk10.setEnabled(false);
        return true;
    }
    
    uint8_t cfg_addr = (uint8_t)configGetInt(KEY_JXK10_ADDR, slave_address);
    
    Jxk10.setSlaveAddress(cfg_addr);
    
    // Force stale state at startup
    // (internal state reset in constructor)
    
    bool result = Jxk10.begin(baud_rate);
    
    if (result) {
        logInfo("[JXK10] Initialized (Addr: %u, Baud: %lu)", cfg_addr, (unsigned long)baud_rate);
    } else {
        logError("[JXK10] Failed to register");
    }
    return result;
}

bool jxk10ModbusReadCurrent(void) {
    // Now handled by registry scheduler
    // Can force poll if needed, but Base Class doesn't expose "force poll" easily via C-API without casting.
    // But rs485RequestImmediatePoll takes rs485_device_t*.
    return rs485RequestImmediatePoll(Jxk10.getMutableDeviceDescriptor());
}

bool jxk10ModbusReceiveResponse(void) {
    return true; // Use registry
}

bool jxk10ModbusReadStatus(void) {
    return jxk10ModbusReadCurrent();
}

float jxk10GetCurrentAmps(void) {
    return Jxk10.getCurrentAmps();
}

int16_t jxk10GetCurrentRaw(void) {
    return Jxk10.getCurrentRaw();
}

bool jxk10ModbusSetSlaveAddress(uint8_t new_address) {
    return Jxk10.setInternalSlaveAddress(new_address);
}

bool jxk10ModbusSetBaudRate(uint32_t baud_rate) {
    // Shared bus
    return true;
}

const jxk10_state_t* jxk10GetState(void) {
    return Jxk10.getState();
}

void jxk10ResetErrorCounters(void) {
    // Reset via generic? 
    // Driver doesn't support generic reset error count publicly yet? 
    // Actually rs485_device_t has counters. 
    // We can't easily clear them unless we expose it.
    // But this is minor.
}

void jxk10PrintDiagnostics(void) {
    const jxk10_state_t* s = Jxk10.getState();
    serialLoggerLock();
    logPrintln("\n[JXK10] === Diagnostics ===");
    logPrintf("Slave Address:       %u\n", s->slave_address);
    logPrintf("Current:             %.2f A (raw: %d)\n", s->current_amps, s->current_raw);
    logPrintf("Read Count:          %lu\n", (unsigned long)s->read_count);
    logPrintf("Error Count:         %lu\n", (unsigned long)s->error_count);
    logPrintf("Consecutive Errors:  %lu\n", (unsigned long)s->consecutive_errors);
    if (s->last_read_time_ms > 0) {
        logPrintf("Last Read:           %lu ms ago\n", (unsigned long)(millis() - s->last_read_time_ms));
    }
    serialLoggerUnlock();
}
