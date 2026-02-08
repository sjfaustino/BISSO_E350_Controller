#include "altivar31_modbus.h"
#include "modbus_rtu.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_keys.h"
#include <Arduino.h>
#include <string.h>

// Global instance
Altivar31Driver Altivar31;

// Polling sequence
static const uint16_t poll_registers[] = {
    ALTIVAR31_REG_DRIVE_STATUS,
    ALTIVAR31_REG_OUTPUT_FREQ,
    ALTIVAR31_REG_DRIVE_CURRENT,
    ALTIVAR31_REG_FAULT_CODE,
    ALTIVAR31_REG_THERMAL_STATE
};
#define POLL_STEP_COUNT (sizeof(poll_registers) / sizeof(poll_registers[0]))

// ============================================================================
// CLASS IMPLEMENTATION
// ============================================================================

Altivar31Driver::Altivar31Driver() 
    : ModbusDriver("Altivar31", RS485_DEVICE_TYPE_VFD, 2, 50, 5) 
{
    memset(&_state, 0, sizeof(_state));
    _state.slave_address = 2;
    _state.baud_rate = 19200;
    
    _poll_step = 0;
    _pending_register = 0;
}

const altivar31_state_t* Altivar31Driver::getState() const {
    altivar31_state_t* mutable_state = const_cast<altivar31_state_t*>(&_state);
    mutable_state->read_count = getPollCount();
    mutable_state->error_count = getErrorCount();
    mutable_state->consecutive_errors = getConsecutiveErrors();
    mutable_state->enabled = isEnabled();
    mutable_state->slave_address = getSlaveAddress();
    return &_state;
}

// Accessors
float Altivar31Driver::getCurrentAmps() const { return _state.current_amps; }
int16_t Altivar31Driver::getCurrentRaw() const { return _state.current_raw; }
float Altivar31Driver::getFrequencyHz() const { return _state.frequency_hz; }
int16_t Altivar31Driver::getFrequencyRaw() const { return _state.frequency_raw; }
uint16_t Altivar31Driver::getStatusWord() const { return _state.status_word; }
uint16_t Altivar31Driver::getFaultCode() const { return _state.fault_code; }
int16_t Altivar31Driver::getThermalState() const { return _state.thermal_state; }
bool Altivar31Driver::isFaulted() const { return _state.fault_code != 0; }
bool Altivar31Driver::isRunning() const { return (_state.status_word & 0x0008) != 0; }

void Altivar31Driver::queueRequest(uint16_t register_addr) {
    _pending_register = register_addr;
    rs485RequestImmediatePoll(getMutableDeviceDescriptor());
}

bool Altivar31Driver::poll() {
    // If pending register set (by queueRequest), use it for one-off transactions (e.g. fault code)
    if (_pending_register != 0) {
        uint16_t tx_len = modbusReadRegistersRequest(getSlaveAddress(),
                                                      _pending_register, 1, _tx_buffer);
        return send(_tx_buffer, tx_len);
    } 

    // BATCH OPTIMIZATION: Read contiguous block 3201-3204
    // 3201: Status (ETA)
    // 3202: Output Frequency (rFr)
    // 3204: Motor Current (LCr)
    // Note: We read 4 registers to get 3201, 3202, 3203 (not used), 3204.
    uint16_t tx_len = modbusReadRegistersRequest(getSlaveAddress(),
                                                  ALTIVAR31_REG_DRIVE_STATUS, 4, _tx_buffer);
    
    bool sent = send(_tx_buffer, tx_len);
    return sent;
}

bool Altivar31Driver::onResponse(const uint8_t* data, uint16_t len) {
    // Determine how many registers we expected
    uint16_t expected_count = (_pending_register != 0) ? 1 : 4;
    uint16_t regs[4];
    uint8_t err = modbusParseReadResponse(data, len, expected_count, regs);
    
    if (err != MODBUS_ERR_NONE) {
        _state.last_error_time_ms = millis();
        _pending_register = 0; // Clear on error too
        return false;
    }

    if (_pending_register != 0) {
        // Handle single register response
        uint16_t raw_value = regs[0];
        switch (_pending_register) {
            case ALTIVAR31_REG_FAULT_CODE:
                _state.fault_code = raw_value;
                break;
            case ALTIVAR31_REG_THERMAL_STATE:
                _state.thermal_state = (int16_t)raw_value;
                break;
            case ALTIVAR31_REG_DRIVE_CURRENT:
                _state.current_raw = (int16_t)raw_value;
                _state.current_amps = _state.current_raw * 0.1f;
                break;
            case ALTIVAR31_REG_OUTPUT_FREQ:
                _state.frequency_raw = (int16_t)raw_value;
                _state.frequency_hz = _state.frequency_raw * 0.1f;
                break;
            case ALTIVAR31_REG_DRIVE_STATUS:
                _state.status_word = raw_value;
                break;
        }
        _pending_register = 0;
    } else {
        // Handle batch response (3201-3204)
        _state.status_word = regs[0];               // 3201
        _state.frequency_raw = (int16_t)regs[1];    // 3202
        _state.frequency_hz = _state.frequency_raw * 0.1f;
        // regs[2] is 3203 (not used)
        _state.current_raw = (int16_t)regs[3];      // 3204
        _state.current_amps = _state.current_raw * 0.1f;
    }

    _state.last_read_time_ms = millis();
    return true;
}


// ============================================================================
// C-API WRAPPERS
// ============================================================================

bool altivar31ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    if (configGetInt(KEY_VFD_EN, 1) == 0) {
        logInfo("[ALTIVAR31] Disabled in configuration");
        Altivar31.setEnabled(false);
        return true;
    }
    
    uint8_t cfg_addr = (uint8_t)configGetInt(KEY_VFD_ADDR, slave_address);
    Altivar31.setSlaveAddress(cfg_addr);
    
    if (Altivar31.begin(baud_rate)) {
        logInfo("[ALTIVAR31] Initialized (Addr: %u)", cfg_addr);
        return true;
    }
    return false;
}

bool altivar31ModbusReadCurrent(void) {
    Altivar31.queueRequest(ALTIVAR31_REG_DRIVE_CURRENT);
    return true;
}

bool altivar31ModbusReadFrequency(void) {
    Altivar31.queueRequest(ALTIVAR31_REG_OUTPUT_FREQ);
    return true;
}

bool altivar31ModbusReadStatus(void) {
    Altivar31.queueRequest(ALTIVAR31_REG_DRIVE_STATUS);
    return true;
}

bool altivar31ModbusReadFaultCode(void) {
    Altivar31.queueRequest(ALTIVAR31_REG_FAULT_CODE);
    return true;
}

bool altivar31ModbusReadThermalState(void) {
    Altivar31.queueRequest(ALTIVAR31_REG_THERMAL_STATE);
    return true;
}

bool altivar31ModbusReceiveResponse(void) {
    return true; 
}

float altivar31GetCurrentAmps(void) { return Altivar31.getCurrentAmps(); }
int16_t altivar31GetCurrentRaw(void) { return Altivar31.getCurrentRaw(); }

float altivar31GetFrequencyHz(void) { return Altivar31.getFrequencyHz(); }
int16_t altivar31GetFrequencyRaw(void) { return Altivar31.getFrequencyRaw(); }

uint16_t altivar31GetStatusWord(void) { return Altivar31.getStatusWord(); }
uint16_t altivar31GetFaultCode(void) { return Altivar31.getFaultCode(); }
int16_t altivar31GetThermalState(void) { return Altivar31.getThermalState(); }

bool altivar31IsFaulted(void) { return Altivar31.isFaulted(); }
bool altivar31IsRunning(void) { return Altivar31.isRunning(); }

const altivar31_state_t* altivar31GetState(void) { return Altivar31.getState(); }

void altivar31ResetErrorCounters(void) {
    // Reset base class error counters
    Altivar31.resetErrorCounters();
}

bool altivar31IsMotorRunning(void) {
    return Altivar31.getFrequencyHz() > 0.5f;
}

bool altivar31DetectFrequencyLoss(float previous_freq_hz) {
     // Logic from original...
    float current_freq = Altivar31.getFrequencyHz();
     // ...
    if (previous_freq_hz > 1.0f && current_freq < (previous_freq_hz * 0.2f)) return true;
    return false;
}

void altivar31PrintDiagnostics(void) {
    const altivar31_state_t* s = Altivar31.getState();
    serialLoggerLock();
    logPrintln("\n[ALTIVAR31] === Diagnostics ===");
    logPrintf("Addr: %u\n", s->slave_address);
    logPrintf("Freq: %.1f Hz\n", s->frequency_hz);
    logPrintf("Curr: %.1f A\n", s->current_amps);
    serialLoggerUnlock();
}
