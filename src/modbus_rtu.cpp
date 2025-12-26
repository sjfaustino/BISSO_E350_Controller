/**
 * @file modbus_rtu.cpp
 * @brief Shared Modbus RTU Protocol Implementation
 * @project BISSO E350 Controller
 * @details Centralized Modbus RTU framing for all RS-485 devices.
 *          Single CRC-16 lookup table shared across all device drivers.
 */

#include "modbus_rtu.h"
#include <string.h>

// ============================================================================
// CRC-16 LOOKUP TABLE (Single shared copy - saves ~512 bytes flash)
// ============================================================================
// Modbus CRC-16 polynomial 0xA001 (reversed 0x8005)

static const uint16_t crc16_table[256] = {
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
    0x0400, 0xC801, 0xDC01, 0x1000, 0xF401, 0x3800, 0x2C00, 0xE001,
    0xA401, 0x6800, 0x7C00, 0xB001, 0x5400, 0x9801, 0x8C01, 0x4000,
    0x8C00, 0x4001, 0x5401, 0x9800, 0x7C01, 0xB000, 0xA400, 0x6801,
    0x2C01, 0xE000, 0xF400, 0x3801, 0xDC00, 0x1001, 0x0401, 0xC800,
    0x8800, 0x4401, 0x5001, 0x9C00, 0x7801, 0xB400, 0xA000, 0x6C01,
    0x2801, 0xE400, 0xF000, 0x3C01, 0xD800, 0x1401, 0x0001, 0xCC00,
    0x0C01, 0xC000, 0xD400, 0x1801, 0xFC00, 0x3001, 0x2401, 0xE800,
    0xAC00, 0x6001, 0x7401, 0xB800, 0x5C01, 0x9000, 0x8400, 0x4801,
    0x0801, 0xC400, 0xD000, 0x1C01, 0xF800, 0x3401, 0x2001, 0xEC00,
    0xA800, 0x6401, 0x7001, 0xBC00, 0x5801, 0x9400, 0x8001, 0x4C00,
    0x8401, 0x4800, 0x5C00, 0x9001, 0x7400, 0xB801, 0xAC00, 0x6000,
    0x2400, 0xE801, 0xFC01, 0x3000, 0xD401, 0x1800, 0x0C00, 0xC001,
    0x8001, 0x4C00, 0x5800, 0x9401, 0x7000, 0xBC01, 0xA801, 0x6400,
    0x2000, 0xEC01, 0xF801, 0x3400, 0xD001, 0x1C00, 0x0800, 0xC401,
    0xC401, 0x0800, 0x1C00, 0xD001, 0x3400, 0xF801, 0xEC01, 0x2000,
    0x6400, 0xA801, 0xBC01, 0x7000, 0x9401, 0x5800, 0x4C00, 0x8001,
    0xC001, 0x0C00, 0x1800, 0xD401, 0x3000, 0xFC01, 0xE801, 0x2400,
    0x6000, 0xAC00, 0xB801, 0x7400, 0x9001, 0x5C00, 0x4800, 0x8401,
    0x4C00, 0x8001, 0x9401, 0x5800, 0xBC01, 0x7000, 0x6400, 0xA801,
    0xEC01, 0x2000, 0x3400, 0xF801, 0x1C00, 0xD001, 0xC401, 0x0800,
    0x4800, 0x8401, 0x9001, 0x5C00, 0xB801, 0x7400, 0x6000, 0xAC00,
    0xE801, 0x2400, 0x3000, 0xFC01, 0x1800, 0xD401, 0xC001, 0x0C00,
    0xCC00, 0x0001, 0x1401, 0xD800, 0x3C01, 0xF000, 0xE400, 0x2801,
    0x6C01, 0xA000, 0xB400, 0x7801, 0x9C00, 0x5001, 0x4401, 0x8800,
    0xC800, 0x0401, 0x1001, 0xDC00, 0x3801, 0xF400, 0xE000, 0x2C01,
    0x6801, 0xA400, 0xB000, 0x7C01, 0x9800, 0x5401, 0x4001, 0x8C00,
    0x4001, 0x8C00, 0x9800, 0x5401, 0xB000, 0x7C01, 0x6801, 0xA400,
    0xE000, 0x2C01, 0x3801, 0xF400, 0x1001, 0xDC00, 0xC800, 0x0401,
    0x4401, 0x8800, 0x9C00, 0x5001, 0xB400, 0x7801, 0x6C01, 0xA000,
    0xE400, 0x2801, 0x3C01, 0xF000, 0x1401, 0xD800, 0xCC00, 0x0001
};

// ============================================================================
// CRC-16 CALCULATION
// ============================================================================

uint16_t modbusCrc16(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

bool modbusVerifyCrc(const uint8_t* data, uint16_t length) {
    if (length < 3) return false;
    
    uint16_t received_crc = data[length - 2] | (data[length - 1] << 8);
    uint16_t calculated_crc = modbusCrc16(data, length - 2);
    
    return (received_crc == calculated_crc);
}

// ============================================================================
// REQUEST BUILDERS
// ============================================================================

uint16_t modbusReadRegistersRequest(uint8_t slave_addr, uint16_t start_addr,
                                    uint16_t num_regs, uint8_t* buffer) {
    buffer[0] = slave_addr;
    buffer[1] = MODBUS_FC_READ_HOLDING_REGISTERS;
    buffer[2] = (start_addr >> 8) & 0xFF;
    buffer[3] = start_addr & 0xFF;
    buffer[4] = (num_regs >> 8) & 0xFF;
    buffer[5] = num_regs & 0xFF;

    uint16_t crc = modbusCrc16(buffer, 6);
    buffer[6] = crc & 0xFF;
    buffer[7] = (crc >> 8) & 0xFF;

    return 8;
}

uint16_t modbusWriteSingleRegisterRequest(uint8_t slave_addr, uint16_t reg_addr,
                                          uint16_t value, uint8_t* buffer) {
    buffer[0] = slave_addr;
    buffer[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    buffer[2] = (reg_addr >> 8) & 0xFF;
    buffer[3] = reg_addr & 0xFF;
    buffer[4] = (value >> 8) & 0xFF;
    buffer[5] = value & 0xFF;

    uint16_t crc = modbusCrc16(buffer, 6);
    buffer[6] = crc & 0xFF;
    buffer[7] = (crc >> 8) & 0xFF;

    return 8;
}

uint16_t modbusWriteMultipleRegistersRequest(uint8_t slave_addr, uint16_t start_addr,
                                             uint16_t num_regs, const uint16_t* values,
                                             uint8_t* buffer) {
    buffer[0] = slave_addr;
    buffer[1] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    buffer[2] = (start_addr >> 8) & 0xFF;
    buffer[3] = start_addr & 0xFF;
    buffer[4] = (num_regs >> 8) & 0xFF;
    buffer[5] = num_regs & 0xFF;
    buffer[6] = num_regs * 2;  // Byte count

    // Add register values
    for (uint16_t i = 0; i < num_regs; i++) {
        buffer[7 + i * 2] = (values[i] >> 8) & 0xFF;
        buffer[8 + i * 2] = values[i] & 0xFF;
    }

    uint16_t frame_len = 7 + num_regs * 2;
    uint16_t crc = modbusCrc16(buffer, frame_len);
    buffer[frame_len] = crc & 0xFF;
    buffer[frame_len + 1] = (crc >> 8) & 0xFF;

    return frame_len + 2;
}

// ============================================================================
// RESPONSE PARSERS
// ============================================================================

uint8_t modbusCheckException(const uint8_t* buffer, uint16_t length) {
    if (length < 5) return 0;
    
    // Exception response: FC is original | 0x80
    if (buffer[1] & 0x80) {
        return buffer[2];  // Exception code
    }
    return 0;
}

uint8_t modbusParseReadResponse(const uint8_t* buffer, uint16_t length,
                                uint16_t expected_regs, uint16_t* out_values) {
    
    if (length < 5) {
        return MODBUS_ERR_FRAME_ERROR;
    }
    
    // Check for exception
    uint8_t exception = modbusCheckException(buffer, length);
    if (exception) {
        return exception;
    }
    
    // Verify CRC
    if (!modbusVerifyCrc(buffer, length)) {
        return MODBUS_ERR_CRC_MISMATCH;
    }
    
    // Verify function code
    if (buffer[1] != MODBUS_FC_READ_HOLDING_REGISTERS && 
        buffer[1] != MODBUS_FC_READ_INPUT_REGISTERS) {
        return MODBUS_ERR_FRAME_ERROR;
    }
    
    // Verify byte count
    uint8_t byte_count = buffer[2];
    if (byte_count != expected_regs * 2) {
        return MODBUS_ERR_FRAME_ERROR;
    }
    
    // Extract register values
    for (uint16_t i = 0; i < expected_regs; i++) {
        out_values[i] = (buffer[3 + i * 2] << 8) | buffer[4 + i * 2];
    }
    
    return MODBUS_ERR_NONE;
}

uint8_t modbusParseWriteResponse(const uint8_t* buffer, uint16_t length,
                                 uint16_t expected_addr, uint16_t expected_value) {
    // FC06 response: addr(1) + fc(1) + reg_addr(2) + value(2) + crc(2) = 8 bytes
    if (length < 8) {
        return MODBUS_ERR_FRAME_ERROR;
    }
    
    // Check for exception
    uint8_t exception = modbusCheckException(buffer, length);
    if (exception) {
        return exception;
    }
    
    // Verify CRC
    if (!modbusVerifyCrc(buffer, length)) {
        return MODBUS_ERR_CRC_MISMATCH;
    }
    
    // Verify function code
    if (buffer[1] != MODBUS_FC_WRITE_SINGLE_REGISTER) {
        return MODBUS_ERR_FRAME_ERROR;
    }
    
    // Verify echoed address and value
    uint16_t echoed_addr = (buffer[2] << 8) | buffer[3];
    uint16_t echoed_value = (buffer[4] << 8) | buffer[5];
    
    if (echoed_addr != expected_addr || echoed_value != expected_value) {
        return MODBUS_ERR_ILLEGAL_DATA_VALUE;
    }
    
    return MODBUS_ERR_NONE;
}

// ============================================================================
// UTILITY
// ============================================================================

uint16_t modbusGetExpectedReadResponseLength(uint16_t num_regs) {
    // addr(1) + fc(1) + byte_count(1) + data(2*n) + crc(2)
    return 3 + num_regs * 2 + 2;
}

const char* modbusGetErrorString(uint8_t error_code) {
    switch (error_code) {
        case MODBUS_ERR_NONE:                   return "OK";
        case MODBUS_ERR_ILLEGAL_FUNCTION:       return "Illegal function";
        case MODBUS_ERR_ILLEGAL_DATA_ADDRESS:   return "Illegal data address";
        case MODBUS_ERR_ILLEGAL_DATA_VALUE:     return "Illegal data value";
        case MODBUS_ERR_SLAVE_DEVICE_FAILURE:   return "Slave device failure";
        case MODBUS_ERR_CRC_MISMATCH:           return "CRC mismatch";
        case MODBUS_ERR_TIMEOUT:                return "Timeout";
        case MODBUS_ERR_FRAME_ERROR:            return "Frame error";
        default:                                return "Unknown error";
    }
}
