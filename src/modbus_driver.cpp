#include "modbus_driver.h"
#include <string.h>

ModbusDriver::ModbusDriver(const char* name, rs485_device_type_t type, 
                           uint8_t slave_address, uint16_t poll_interval_ms, uint8_t priority) {
    // Initialize device descriptor
    memset(&_device, 0, sizeof(_device));
    _device.name = name;
    _device.type = type;
    _device.slave_address = slave_address;
    _device.poll_interval_ms = poll_interval_ms;
    _device.priority = priority;
    
    // Set callbacks to static trampolines
    _device.poll = staticPoll;
    _device.on_response = staticOnResponse;
    _device.user_data = this; // Store 'this' pointer for context
    
    _device.enabled = false;
    _baud_rate = 9600;
}

ModbusDriver::~ModbusDriver() {
    rs485UnregisterDevice(&_device);
}

bool ModbusDriver::begin(uint32_t baud_rate) {
    _baud_rate = baud_rate;
    _device.enabled = true;
    
    // Register with the centralized manager
    return rs485RegisterDevice(&_device);
}

void ModbusDriver::setEnabled(bool enabled) {
    _device.enabled = enabled;
    // Registry checks this flag directly, but we can also use the helper for logging
    rs485SetDeviceEnabled(&_device, enabled);
}

bool ModbusDriver::isEnabled() const {
    return _device.enabled;
}

void ModbusDriver::setSlaveAddress(uint8_t address) {
    _device.slave_address = address;
}

uint8_t ModbusDriver::getSlaveAddress() const {
    return _device.slave_address;
}

void ModbusDriver::setPollInterval(uint16_t interval_ms) {
    _device.poll_interval_ms = interval_ms;
}

uint32_t ModbusDriver::getPollCount() const {
    return _device.poll_count;
}

uint32_t ModbusDriver::getErrorCount() const {
    return _device.error_count;
}

uint32_t ModbusDriver::getConsecutiveErrors() const {
    return _device.consecutive_errors;
}

void ModbusDriver::resetErrorCounters() {
    _device.poll_count = 0;
    _device.error_count = 0;
    _device.consecutive_errors = 0;
}

const rs485_device_t* ModbusDriver::getDeviceDescriptor() const {
    return &_device;
}

rs485_device_t* ModbusDriver::getMutableDeviceDescriptor() {
    return &_device;
}

bool ModbusDriver::send(const uint8_t* data, uint8_t len) {
    return rs485Send(data, len);
}

// Static Trampolines

bool ModbusDriver::staticPoll(void* ctx) {
    ModbusDriver* driver = static_cast<ModbusDriver*>(ctx);
    if (driver) {
        return driver->poll();
    }
    return false;
}

bool ModbusDriver::staticOnResponse(void* ctx, const uint8_t* data, uint16_t len) {
    ModbusDriver* driver = static_cast<ModbusDriver*>(ctx);
    if (driver) {
        return driver->onResponse(data, len);
    }
    return false;
}
