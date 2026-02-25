#include "altivar31_modbus.h"
#include "modbus_rtu.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_keys.h"
#include <Arduino.h>
#include <string.h>

// Global instances
Altivar31Driver AltivarX("VFD1(X)");
Altivar31Driver AltivarYZA("VFD2(YZA)");

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

Altivar31Driver::Altivar31Driver(const char* name) 
    : ModbusDriver(name, RS485_DEVICE_TYPE_VFD, 2, 50, 5) 
{
    memset(&_state, 0, sizeof(_state));
    _state.slave_address = 2; // Default, will be overridden
    _state.baud_rate = 19200;
    
    _poll_step = 0;
    _pending_register = 0;
}

const altivar31_state_t* Altivar31Driver::getState() const {
    syncBaseStats(_state);
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

bool Altivar31Driver::writeFrequency(float hz) {
    if (!isEnabled()) return false;

    // Clamp Hz (0-50Hz usually)
    if (hz < 0.0f) hz = 0.0f;
    if (hz > 60.0f) hz = 60.0f; // Safety cap

    // Altivar expects 0.1 Hz units (e.g., 50.0Hz -> 500)
    uint16_t raw_val = (uint16_t)(hz * 10.0f);
    
    uint8_t buffer[16];
    uint16_t len = modbusWriteSingleRegisterRequest(getSlaveAddress(), 
                                                   ALTIVAR31_REG_FREQ_SETPOINT, 
                                                   raw_val, buffer);
    
    // Immediate send for frequency updates as they are time-critical for motion
    return send(buffer, len);
}

bool Altivar31Driver::setModbusPriority(bool active) {
    if (!isEnabled()) return false;

    // Command word: bits 0-3 = enable/run, bit 15 = Fr1/Fr2 switch
    // Assuming VFD param rFC is set to n15
    uint16_t cmd = 0x000F; // Standard Run Enable
    if (active) {
        cmd |= 0x8000; // Set Bit 15 to take priority (Fr2)
    }

    uint8_t buffer[16];
    uint16_t len = modbusWriteSingleRegisterRequest(getSlaveAddress(), 
                                                   ALTIVAR31_REG_COMMAND_WORD, 
                                                   cmd, buffer);
    
    return send(buffer, len);
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
    return true;
}


// ============================================================================
// C-API WRAPPERS
// ============================================================================

bool altivar31ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    if (configGetInt(KEY_VFD_EN, 1) == 0) {
        logInfo("[ALTIVAR31] Monitoring disabled in configuration");
        AltivarX.setEnabled(false);
        AltivarYZA.setEnabled(false);
        return true;
    }
    
    // VFD1 (X)
    uint8_t addr1 = (uint8_t)configGetInt(KEY_VFD_ADDR, 2);
    AltivarX.setSlaveAddress(addr1);
    if (AltivarX.begin(baud_rate)) {
        logInfo("[ALTIVAR31] VFD1(X) Initialized (Addr: %u)", addr1);
    }
    
    // VFD2 (YZA)
    uint8_t addr2 = (uint8_t)configGetInt(KEY_VFD2_ADDR, 4);
    AltivarYZA.setSlaveAddress(addr2);
    if (AltivarYZA.begin(baud_rate)) {
        logInfo("[ALTIVAR31] VFD2(YZA) Initialized (Addr: %u)", addr2);
    }
    
    return true;
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

void Altivar31Driver::printDiagnostics() const {
    // Print standard base class diagnostics
    ModbusDriver::printDiagnostics();
    
    // Print VFD specific stats
    serialLoggerLock();
    logPrintf("Status Word:         0x%04X\n", _state.status_word);
    logPrintf("Fault Code:          0x%04X\n", _state.fault_code);
    logPrintf("Thermal State:       %d %%\n", _state.thermal_state);
    logPrintf("Frequency:           %.1f Hz\n", _state.frequency_hz);
    logPrintf("Current:             %.1f A\n", _state.current_amps);
    serialLoggerUnlock();
}

void altivar31PrintDiagnostics(void) {
    Altivar31.printDiagnostics();
}

const char* altivar31FaultCodeToString(uint16_t code) {
    switch (code) {
        case 0:  return "No Fault";
        case 2:  return "OCF: Overcurrent";
        case 3:  return "PHF: Input Phase Loss";
        case 4:  return "OPF: Output Phase Loss";
        case 5:  return "OSF: Overvoltage";
        case 6:  return "OHF: Drive Overheat";
        case 7:  return "OLF: Motor Overload";
        case 8:  return "ObF: Braking Overload";
        case 9:  return "OSF: Mains Overvoltage";
        case 11: return "USF: Undervoltage";
        case 12: return "SCF: Short Circuit";
        case 13: return "ILF: Internal Link Fault";
        case 14: return "InF: Internal Fault";
        case 15: return "EPF: External Fault";
        case 16: return "SPF: Speed Feedback Fault";
        case 17: return "CnF: CANopen Fault";
        case 18: return "COF: CANopen Fault";
        case 19: return "tJF: IGBT Fault";
        case 20: return "OLF: Motor Overload";
        case 21: return "OHF: Drive Overheat";
        default: return "Unknown Fault";
    }
}
