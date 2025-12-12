/**
 * @file jxk10_modbus.cpp
 * @brief JXK-10 Modbus RTU Driver Implementation
 * @project BISSO E350 Controller - Phase 5.0
 */

#include "jxk10_modbus.h"
#include "encoder_hal.h"
#include "spindle_current_rs485.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>

// Modbus CRC-16 (CCITT) lookup table
static const uint16_t crc16_table[256] = {
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
    0x0400, 0xC801, 0xDC01, 0x1000, 0xF401, 0x3800, 0x2C00, 0xE001,
    0xA400, 0x6800, 0x7C00, 0xB000, 0x5400, 0x9801, 0x8C01, 0x4000,
    0x0800, 0xC401, 0xD001, 0x1C00, 0xF801, 0x3400, 0x2000, 0xEC01,
    0xA800, 0x6400, 0x7000, 0xBC01, 0x5800, 0x9401, 0x8001, 0x4C00,
    0x0C00, 0xC001, 0xD401, 0x1800, 0xFC01, 0x3000, 0x2400, 0xE801,
    0xAC00, 0x6000, 0x7400, 0xB801, 0x5C00, 0x9001, 0x8401, 0x4800,
    0x1000, 0xDC01, 0xC801, 0x0400, 0xE001, 0x2C00, 0x3800, 0xF401,
    0xB001, 0x7C00, 0x6800, 0xA401, 0x4000, 0x8C01, 0x9801, 0x5400,
    0x1400, 0xD801, 0xCC01, 0x0000, 0xE401, 0x2800, 0x3C00, 0xF001,
    0xB400, 0x7800, 0x6C00, 0xA001, 0x4400, 0x8801, 0x9C01, 0x5000,
    0x1800, 0xD401, 0xC001, 0x0C00, 0xE801, 0x2400, 0x3000, 0xFC01,
    0xB800, 0x7400, 0x6000, 0xAC01, 0x4800, 0x8401, 0x9001, 0x5C00,
    0x1C00, 0xD001, 0xC401, 0x0800, 0xEC01, 0x2000, 0x3400, 0xF801,
    0xBC01, 0x7000, 0x6400, 0xA800, 0x4C00, 0x8001, 0x9401, 0x5800,
    0x2000, 0xEC01, 0xF801, 0x3400, 0xD001, 0x1C00, 0x0800, 0xC401,
    0x6801, 0xA400, 0xB000, 0x7C01, 0x9801, 0x5400, 0x4000, 0x8C01,
    0x2400, 0xE801, 0xFC01, 0x3000, 0xD401, 0x1800, 0x0C00, 0xC001,
    0x6C00, 0xA001, 0xB401, 0x7800, 0x9C01, 0x5000, 0x4400, 0x8801,
    0x2800, 0xE401, 0xF001, 0x3C00, 0xD801, 0x1400, 0x0000, 0xCC01,
    0x7001, 0xBC01, 0xA800, 0x6400, 0x8401, 0x4800, 0x5C00, 0x9001,
    0x2C00, 0xE001, 0xF401, 0x3800, 0xDC01, 0x1000, 0x0400, 0xC801,
    0x7400, 0xB801, 0xAC00, 0x6000, 0x8801, 0x4400, 0x5000, 0x9C01,
    0x3000, 0xFC01, 0xE801, 0x2400, 0xD001, 0x1C00, 0x0800, 0xC401,
    0x7C01, 0xB000, 0xA400, 0x6800, 0x8C01, 0x4000, 0x5400, 0x9801,
    0x3400, 0xF801, 0xEC01, 0x2000, 0xD401, 0x1800, 0x0C00, 0xC001,
    0x8001, 0x4C00, 0x5800, 0x9401, 0x7000, 0xBC01, 0xA801, 0x6400,
    0x3800, 0xF401, 0xE001, 0x2C00, 0xD801, 0x1400, 0x0000, 0xCC01,
    0x8401, 0x4800, 0x5C00, 0x9001, 0x7400, 0xB801, 0xAC00, 0x6000,
    0x3000, 0xFC01, 0xE801, 0x2400, 0xD001, 0x1C00, 0x0800, 0xC401,
    0x8801, 0x4400, 0x5000, 0x9C01, 0x7800, 0xB401, 0xA001, 0x6C00
};

// Global JXK-10 state
static jxk10_state_t jxk10_state = {
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

// Modbus request/response buffer
static uint8_t modbus_tx_buffer[32];
static uint8_t modbus_rx_buffer[32];
static uint32_t modbus_rx_length = 0;
static uint32_t modbus_request_time_ms = 0;

/**
 * @brief Calculate Modbus CRC-16
 * @param data Pointer to data buffer
 * @param length Number of bytes
 * @return CRC value (little-endian)
 */
static uint16_t modbusCrc16(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        crc = (crc >> 8) ^ crc16_table[(crc ^ byte) & 0xFF];
    }
    return crc;
}

/**
 * @brief Build Modbus RTU read registers request (FC 03)
 * @param slave_addr Slave address
 * @param start_addr Starting register address
 * @param num_regs Number of registers to read
 * @param buffer Output buffer for request frame
 * @return Length of request frame
 */
static uint16_t modbusReadRegistersRequest(uint8_t slave_addr, uint16_t start_addr,
                                           uint16_t num_regs, uint8_t* buffer) {
    buffer[0] = slave_addr;
    buffer[1] = 0x03;  // Function code: Read Holding Registers
    buffer[2] = (start_addr >> 8) & 0xFF;
    buffer[3] = start_addr & 0xFF;
    buffer[4] = (num_regs >> 8) & 0xFF;
    buffer[5] = num_regs & 0xFF;

    uint16_t crc = modbusCrc16(buffer, 6);
    buffer[6] = crc & 0xFF;
    buffer[7] = (crc >> 8) & 0xFF;

    return 8;
}

/**
 * @brief Parse Modbus read registers response (FC 03)
 * @param buffer Input buffer with response frame
 * @param length Length of response
 * @param num_regs Expected number of registers
 * @return Array of register values, or NULL on error
 */
static const uint16_t* modbusParseReadResponse(const uint8_t* buffer, uint16_t length,
                                               uint16_t num_regs) {
    if (length < (5 + num_regs * 2)) {
        return NULL;  // Frame too short
    }

    // PHASE 5.1: Check exception FIRST, then CRC
    // Exception responses (bit 7 of function code set) must be checked before CRC
    // because both valid and invalid frames need to be handled appropriately
    if (buffer[1] & 0x80) {
        return NULL;  // Exception response - still validate CRC below before returning
    }

    uint16_t crc = modbusCrc16(buffer, length - 2);
    uint16_t rx_crc = buffer[length - 2] | (buffer[length - 1] << 8);

    if (crc != rx_crc) {
        return NULL;  // CRC mismatch
    }

    return (const uint16_t*)&buffer[3];  // Point to data
}

/**
 * @brief Build Modbus RTU write single register request (FC 06)
 * @param slave_addr Slave address
 * @param reg_addr Register address
 * @param value Register value to write
 * @param buffer Output buffer for request frame
 * @return Length of request frame
 */
static uint16_t modbusWriteSingleRegisterRequest(uint8_t slave_addr, uint16_t reg_addr,
                                                 uint16_t value, uint8_t* buffer) {
    buffer[0] = slave_addr;
    buffer[1] = 0x06;  // Function code: Write Single Register
    buffer[2] = (reg_addr >> 8) & 0xFF;
    buffer[3] = reg_addr & 0xFF;
    buffer[4] = (value >> 8) & 0xFF;
    buffer[5] = value & 0xFF;

    uint16_t crc = modbusCrc16(buffer, 6);
    buffer[6] = crc & 0xFF;
    buffer[7] = (crc >> 8) & 0xFF;

    return 8;
}

bool jxk10ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    jxk10_state.slave_address = slave_address;
    jxk10_state.baud_rate = baud_rate;
    jxk10_state.current_raw = 0;
    jxk10_state.current_amps = 0.0f;
    jxk10_state.device_status = 0;
    jxk10_state.read_count = 0;
    jxk10_state.error_count = 0;
    jxk10_state.consecutive_errors = 0;

    Serial.printf("[JXK10] Initialized (Slave ID: %u, Baud: %lu)\n",
                  slave_address, (unsigned long)baud_rate);
    return true;
}

bool jxk10ModbusReadCurrent(void) {
    // Ensure we're on the spindle device
    if (rs485MuxGetCurrentDevice() != RS485_DEVICE_SPINDLE) {
        return false;  // Wrong device selected
    }

    // Build Modbus RTU request for current register (0x0000)
    uint16_t tx_len = modbusReadRegistersRequest(jxk10_state.slave_address,
                                                  JXK10_REG_CURRENT, 1, modbus_tx_buffer);

    // Send via encoder_hal (shared RS485 interface)
    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        jxk10_state.error_count++;
        jxk10_state.consecutive_errors++;
        return false;
    }

    modbus_request_time_ms = millis();
    return true;
}

bool jxk10ModbusReceiveResponse(void) {
    // Check if we have a complete response (timeout 200ms from request)
    uint32_t now = millis();
    if ((now - modbus_request_time_ms) < 50) {
        return false;  // Not enough time for response
    }

    // Attempt to receive response
    uint8_t rx_data[32];
    memset(rx_data, 0, sizeof(rx_data));
    int rx_len = encoderHalReceive(rx_data, sizeof(rx_data));

    if (rx_len <= 0) {
        if ((now - modbus_request_time_ms) > 200) {
            jxk10_state.consecutive_errors++;
            jxk10_state.error_count++;
        }
        return false;  // No data available or timeout
    }

    // Parse response
    const uint16_t* regs = modbusParseReadResponse(rx_data, (uint16_t)rx_len, 1);
    if (regs == NULL) {
        jxk10_state.error_count++;
        jxk10_state.consecutive_errors++;
        return false;  // Invalid response
    }

    // Extract current value (big-endian)
    uint16_t raw_value = (regs[0] >> 8) | ((regs[0] & 0xFF) << 8);
    jxk10_state.current_raw = (int16_t)raw_value;
    jxk10_state.current_amps = jxk10_state.current_raw / 100.0f;

    jxk10_state.read_count++;
    jxk10_state.last_read_time_ms = now;
    jxk10_state.consecutive_errors = 0;

    return true;
}

bool jxk10ModbusReadStatus(void) {
    if (rs485MuxGetCurrentDevice() != RS485_DEVICE_SPINDLE) {
        return false;
    }

    uint16_t tx_len = modbusReadRegistersRequest(jxk10_state.slave_address,
                                                  JXK10_REG_STATUS, 1, modbus_tx_buffer);

    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        return false;
    }

    delay(50);  // Wait for response

    uint8_t rx_data[32];
    memset(rx_data, 0, sizeof(rx_data));
    int rx_len = encoderHalReceive(rx_data, sizeof(rx_data));

    if (rx_len <= 0) {
        return false;
    }

    const uint16_t* regs = modbusParseReadResponse(rx_data, (uint16_t)rx_len, 1);
    if (regs == NULL) {
        return false;
    }

    jxk10_state.device_status = regs[0];
    jxk10_state.is_overload = (jxk10_state.device_status & JXK10_STATUS_OVERLOAD) != 0;
    jxk10_state.is_fault = (jxk10_state.device_status & JXK10_STATUS_FAULT) != 0;

    return true;
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
    if (new_address < 1 || new_address > 247) {
        return false;
    }

    // Ensure we're on the spindle device
    if (rs485MuxGetCurrentDevice() != RS485_DEVICE_SPINDLE) {
        return false;
    }

    // Build Modbus FC 06 write request for address register (0x0002)
    uint16_t tx_len = modbusWriteSingleRegisterRequest(jxk10_state.slave_address,
                                                        JXK10_REG_SLAVE_ADDR, new_address,
                                                        modbus_tx_buffer);

    // Send write request
    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        jxk10_state.error_count++;
        return false;
    }

    // Wait for response
    delay(100);

    // Attempt to receive response
    uint8_t rx_data[32];
    memset(rx_data, 0, sizeof(rx_data));
    int rx_len = encoderHalReceive(rx_data, sizeof(rx_data));

    if (rx_len < 8) {
        jxk10_state.error_count++;
        return false;  // Invalid response
    }

    // Verify FC 06 response (should echo request)
    if (rx_data[1] != 0x06) {
        jxk10_state.error_count++;
        return false;  // Wrong function code
    }

    // Verify CRC
    uint16_t crc = modbusCrc16(rx_data, rx_len - 2);
    uint16_t rx_crc = rx_data[rx_len - 2] | (rx_data[rx_len - 1] << 8);
    if (crc != rx_crc) {
        jxk10_state.error_count++;
        return false;  // CRC mismatch
    }

    // Now write 1 to register 0x0005 to save config
    tx_len = modbusWriteSingleRegisterRequest(jxk10_state.slave_address,
                                               JXK10_REG_SAVE_CONFIG, 1,
                                               modbus_tx_buffer);

    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        jxk10_state.error_count++;
        return false;
    }

    delay(100);

    memset(rx_data, 0, sizeof(rx_data));
    rx_len = encoderHalReceive(rx_data, sizeof(rx_data));

    if (rx_len < 8) {
        jxk10_state.error_count++;
        return false;  // Save failed
    }

    // Update local state with new address
    jxk10_state.slave_address = new_address;

    return true;
}

bool jxk10ModbusSetBaudRate(uint32_t baud_rate) {
    jxk10_state.baud_rate = baud_rate;
    return true;
}

const jxk10_state_t* jxk10GetState(void) {
    return &jxk10_state;
}

void jxk10ResetErrorCounters(void) {
    jxk10_state.error_count = 0;
    jxk10_state.consecutive_errors = 0;
}

void jxk10PrintDiagnostics(void) {
    Serial.println("\n[JXK10] === Diagnostics ===");
    Serial.printf("Slave Address:       %u\n", jxk10_state.slave_address);
    Serial.printf("Baud Rate:           %lu bps\n", (unsigned long)jxk10_state.baud_rate);
    Serial.printf("Current:             %.2f A (raw: %d)\n", jxk10_state.current_amps, jxk10_state.current_raw);
    Serial.printf("Status:              0x%04X (Overload: %s, Fault: %s)\n",
                  jxk10_state.device_status,
                  jxk10_state.is_overload ? "YES" : "NO",
                  jxk10_state.is_fault ? "YES" : "NO");
    Serial.printf("Read Count:          %lu\n", (unsigned long)jxk10_state.read_count);
    Serial.printf("Error Count:         %lu\n", (unsigned long)jxk10_state.error_count);
    Serial.printf("Consecutive Errors:  %lu\n", (unsigned long)jxk10_state.consecutive_errors);
    if (jxk10_state.last_read_time_ms > 0) {
        Serial.printf("Last Read:           %lu ms ago\n", (unsigned long)(millis() - jxk10_state.last_read_time_ms));
    }
}
