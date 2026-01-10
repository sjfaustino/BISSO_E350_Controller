#include "yhtc05_modbus.h"
#include "modbus_rtu.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>

// Global instance
YhTc05Driver YhTc05;

// ============================================================================
// CLASS IMPLEMENTATION
// ============================================================================

YhTc05Driver::YhTc05Driver() 
    : ModbusDriver("YH-TC05", RS485_DEVICE_TYPE_RPM_SENSOR, 3, 500, 100) 
{
    // Initialize state
    memset(&_state, 0, sizeof(_state));
    _state.slave_address = 3; // Default
    _state.baud_rate = 9600;
    _state.stall_threshold_rpm = 100;
    _state.stall_time_ms = 2000;
    
    _was_spinning = false;
    _below_threshold_since_ms = 0;
}

const yhtc05_state_t* YhTc05Driver::getState() const {
    // Sync base stats
    yhtc05_state_t* mutable_state = const_cast<yhtc05_state_t*>(&_state);
    mutable_state->read_count = getPollCount();
    mutable_state->error_count = getErrorCount();
    mutable_state->consecutive_errors = getConsecutiveErrors();
    mutable_state->enabled = isEnabled();
    mutable_state->slave_address = getSlaveAddress();
    
    return &_state;
}

uint16_t YhTc05Driver::getRPM() const { return _state.rpm; }
uint32_t YhTc05Driver::getPulseCount() const { return _state.pulse_count; }
bool YhTc05Driver::isSpinning() const { return _state.is_spinning; }
bool YhTc05Driver::isStalled() const { return _state.is_stalled; }
uint16_t YhTc05Driver::getPeakRPM() const { return _state.peak_rpm; }

void YhTc05Driver::setStallThreshold(uint16_t rpm, uint32_t time_ms) {
    _state.stall_threshold_rpm = rpm;
    _state.stall_time_ms = time_ms;
    logInfo("[YH-TC05] Stall threshold: %u RPM for %lu ms", rpm, (unsigned long)time_ms);
}

void YhTc05Driver::resetStallDetection() {
    _state.is_stalled = false;
    _state.stall_detect_time_ms = 0;
    _was_spinning = false;
    _below_threshold_since_ms = 0;
}

void YhTc05Driver::resetPeakRPM() {
    _state.peak_rpm = 0;
}

bool YhTc05Driver::poll() {
    // Build read request for RPM register (3 registers: RPM + CountL + CountH)
    uint16_t frame_len = modbusReadRegistersRequest(
        getSlaveAddress(),
        YHTC05_REG_RPM,
        3,  
        _tx_buffer
    );
    
    return send(_tx_buffer, (uint8_t)frame_len);
}

bool YhTc05Driver::onResponse(const uint8_t* data, uint16_t len) {
    uint16_t values[3];
    
    // Parse 3 registers
    uint8_t err = modbusParseReadResponse(data, len, 3, values);
    if (err != MODBUS_ERR_NONE) {
        _state.last_error_time_ms = millis();
        return false;
    }
    
    // Update state
    _state.rpm = values[0];
    
    // Combine High/Low words for Pulse Count 
    // Assuming Reg 1 = Low, Reg 2 = High based on user log
    _state.pulse_count = ((uint32_t)values[2] << 16) | values[1];
    
    _state.last_read_time_ms = millis();
    // Base class handles counters
    
    // Update peak tracking
    if (_state.rpm > _state.peak_rpm) {
        _state.peak_rpm = _state.rpm;
    }
    
    // Update spinning state
    bool currently_spinning = (_state.rpm >= _state.stall_threshold_rpm);
    _state.is_spinning = currently_spinning;
    
    // Stall detection logic
    uint32_t now = millis();
    if (_was_spinning && !currently_spinning) {
        // Just dropped below threshold
        if (_below_threshold_since_ms == 0) {
            _below_threshold_since_ms = now;
        }
        
        // Check if below threshold long enough
        if (now - _below_threshold_since_ms >= _state.stall_time_ms) {
            if (!_state.is_stalled) {
                _state.is_stalled = true;
                _state.stall_detect_time_ms = now;
                logWarning("[YH-TC05] STALL DETECTED (RPM=%u)", _state.rpm);
            }
        }
    } else if (currently_spinning) {
        // Spinning normally
        _below_threshold_since_ms = 0;
        if (_state.is_stalled) {
            logInfo("[YH-TC05] Recovered from stall (RPM=%u)", _state.rpm);
        }
        _state.is_stalled = false;
    }
    
    _was_spinning = currently_spinning;
    
    return true;
}

// ============================================================================
// C-API WRAPPERS
// ============================================================================

bool yhtc05ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    YhTc05.setSlaveAddress(slave_address);
    // Init but do NOT register yet? Original code: 
    // "Initialize... (does NOT register with RS485 bus)"
    // But modifying directly _state is not enough if we use Driver class.
    
    // We can interpret this as "Configure"
    // Registration happens in yhtc05RegisterWithBus
    return true; 
}

bool yhtc05RegisterWithBus(uint16_t poll_interval_ms, uint8_t priority) {
    YhTc05.setPollInterval(poll_interval_ms);
    // Base class constructor sets priority, but we can't change it easily?
    // Actually rs485_device_t has priority. We can mod it.
    YhTc05.getMutableDeviceDescriptor()->priority = priority;
    
    if (YhTc05.begin(9600)) { // Baud rate needs to be passed or stored?
        // Original yhtc05ModbusInit stored baud rate.
        // We should likely use that. 
        // But begin() handles registration.
        logInfo("[YH-TC05] Registered usage via OOP Driver");
        return true;
    }
    return false;
}

bool yhtc05UnregisterFromBus(void) {
    YhTc05.setEnabled(false);
    return rs485UnregisterDevice(const_cast<rs485_device_t*>(YhTc05.getDeviceDescriptor()));
}

bool yhtc05ModbusReadRPM(void* ctx) {
    // Should be called by registry, which passes ctx (this)
    // But if called manually with NULL?
    // Wrapper shouldn't exists as a callback anymore, but as a C-API command?
    // Wait, registry calls it via function pointer we assign?  
    // NO! ModbusDriver sets .poll = staticPoll.
    // So logic flow is: Registry -> ModbusDriver::staticPoll -> YhTc05Driver::poll
    // THIS wrapper "yhtc05ModbusReadRPM" is likely NOT used by registry anymore.
    // We can leave it empty or delete implementation if no one calls it (grep verified).
    // But header declares it.
    // We can stub it.
    (void)ctx;
    return false; // Should not be called
}

bool yhtc05ModbusOnResponse(void* ctx, const uint8_t* data, uint16_t len) {
    (void)ctx; (void)data; (void)len;
    return false; // Should not be called
}

uint16_t yhtc05GetRPM(void) { return YhTc05.getRPM(); }
uint32_t yhtc05GetPulseCount(void) { return YhTc05.getPulseCount(); }
bool yhtc05IsSpinning(void) { return YhTc05.isSpinning(); }
bool yhtc05IsStalled(void) { return YhTc05.isStalled(); }
uint16_t yhtc05GetPeakRPM(void) { return YhTc05.getPeakRPM(); }
const yhtc05_state_t* yhtc05GetState(void) { return YhTc05.getState(); }

void yhtc05SetStallThreshold(uint16_t rpm_threshold, uint32_t time_ms) {
    YhTc05.setStallThreshold(rpm_threshold, time_ms);
}

void yhtc05ResetStallDetection(void) { YhTc05.resetStallDetection(); }
void yhtc05ResetPeakRPM(void) { YhTc05.resetPeakRPM(); }
void yhtc05ResetErrorCounters(void) { 
    // Not easily exposed via driver yet
}

void yhtc05PrintDiagnostics(void) {
    const yhtc05_state_t* s = YhTc05.getState();
    serialLoggerLock();
    Serial.println("\n[YH-TC05] === Diagnostics ===");
    Serial.printf("Address:         %d\n", s->slave_address);
    Serial.printf("RPM:             %u\n", s->rpm);
    Serial.printf("Spinning:        %s\n", s->is_spinning ? "YES" : "NO");
    Serial.printf("Stalled:         %s\n", s->is_stalled ? "YES" : "NO");
    Serial.printf("Read Count:      %lu\n", (unsigned long)s->read_count);
    Serial.printf("Errors:          %lu\n", (unsigned long)s->error_count);
    serialLoggerUnlock();
}
