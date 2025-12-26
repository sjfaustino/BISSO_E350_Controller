/**
 * @file modbus_rtu.h
 * @brief Shared Modbus RTU Protocol Functions
 * @project BISSO E350 Controller
 * @details Centralized Modbus RTU framing for all RS-485 devices.
 *          Supports FC03 (Read Holding Registers), FC06 (Write Single Register),
 *          FC16 (Write Multiple Registers).
 */

#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MODBUS FUNCTION CODES
// ============================================================================

#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03
#define MODBUS_FC_READ_INPUT_REGISTERS      0x04
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10

// ============================================================================
// MODBUS ERROR CODES
// ============================================================================

#define MODBUS_ERR_NONE                     0x00
#define MODBUS_ERR_ILLEGAL_FUNCTION         0x01
#define MODBUS_ERR_ILLEGAL_DATA_ADDRESS     0x02
#define MODBUS_ERR_ILLEGAL_DATA_VALUE       0x03
#define MODBUS_ERR_SLAVE_DEVICE_FAILURE     0x04
#define MODBUS_ERR_CRC_MISMATCH             0x80  // Custom: CRC validation failed
#define MODBUS_ERR_TIMEOUT                  0x81  // Custom: No response
#define MODBUS_ERR_FRAME_ERROR              0x82  // Custom: Invalid frame

// ============================================================================
// CRC-16 CALCULATION
// ============================================================================

/**
 * @brief Calculate Modbus CRC-16 (polynomial 0xA001)
 * @param data Pointer to data buffer
 * @param length Number of bytes
 * @return CRC value (little-endian, LSB first)
 */
uint16_t modbusCrc16(const uint8_t* data, uint16_t length);

/**
 * @brief Verify CRC on received Modbus frame
 * @param data Pointer to complete frame (including CRC)
 * @param length Total frame length
 * @return true if CRC is valid
 */
bool modbusVerifyCrc(const uint8_t* data, uint16_t length);

// ============================================================================
// REQUEST BUILDERS
// ============================================================================

/**
 * @brief Build Modbus RTU Read Holding Registers request (FC 03)
 * @param slave_addr Slave address (1-247)
 * @param start_addr Starting register address
 * @param num_regs Number of registers to read (1-125)
 * @param buffer Output buffer (must be at least 8 bytes)
 * @return Length of request frame (8 bytes)
 */
uint16_t modbusReadRegistersRequest(uint8_t slave_addr, uint16_t start_addr,
                                    uint16_t num_regs, uint8_t* buffer);

/**
 * @brief Build Modbus RTU Write Single Register request (FC 06)
 * @param slave_addr Slave address (1-247)
 * @param reg_addr Register address
 * @param value Value to write
 * @param buffer Output buffer (must be at least 8 bytes)
 * @return Length of request frame (8 bytes)
 */
uint16_t modbusWriteSingleRegisterRequest(uint8_t slave_addr, uint16_t reg_addr,
                                          uint16_t value, uint8_t* buffer);

/**
 * @brief Build Modbus RTU Write Multiple Registers request (FC 16)
 * @param slave_addr Slave address (1-247)
 * @param start_addr Starting register address
 * @param num_regs Number of registers to write
 * @param values Array of register values
 * @param buffer Output buffer (must be large enough)
 * @return Length of request frame
 */
uint16_t modbusWriteMultipleRegistersRequest(uint8_t slave_addr, uint16_t start_addr,
                                             uint16_t num_regs, const uint16_t* values,
                                             uint8_t* buffer);

// ============================================================================
// RESPONSE PARSERS
// ============================================================================

/**
 * @brief Parse Modbus Read Registers response (FC 03/04)
 * @param buffer Response buffer
 * @param length Response length
 * @param expected_regs Expected number of registers
 * @param out_values Output array for register values (caller allocates)
 * @return 0 on success, Modbus error code on failure
 */
uint8_t modbusParseReadResponse(const uint8_t* buffer, uint16_t length,
                                uint16_t expected_regs, uint16_t* out_values);

/**
 * @brief Parse Modbus Write Single Register response (FC 06)
 * @param buffer Response buffer
 * @param length Response length
 * @param expected_addr Expected register address
 * @param expected_value Expected written value
 * @return 0 on success, Modbus error code on failure
 */
uint8_t modbusParseWriteResponse(const uint8_t* buffer, uint16_t length,
                                 uint16_t expected_addr, uint16_t expected_value);

/**
 * @brief Check if response is a Modbus exception
 * @param buffer Response buffer
 * @param length Response length
 * @return Exception code if present, 0 if normal response
 */
uint8_t modbusCheckException(const uint8_t* buffer, uint16_t length);

// ============================================================================
// UTILITY
// ============================================================================

/**
 * @brief Get expected response length for a read request
 * @param num_regs Number of registers in request
 * @return Expected response length in bytes
 */
uint16_t modbusGetExpectedReadResponseLength(uint16_t num_regs);

/**
 * @brief Get human-readable error string
 * @param error_code Modbus error code
 * @return Error description string
 */
const char* modbusGetErrorString(uint8_t error_code);

#ifdef __cplusplus
}
#endif

#endif // MODBUS_RTU_H
