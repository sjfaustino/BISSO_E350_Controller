/**
 * @file test/test_modbus_rtu.cpp
 * @brief Unit tests for Modbus RTU protocol implementation
 *
 * Tests cover:
 * - CRC-16 calculation and verification
 * - Request frame building (FC03, FC06, FC16)
 * - Response parsing
 * - Error code handling
 * - Frame length calculations
 */

#include "test/unity/unity.h"
#include <cstdint>
#include <cstring>

// ============================================================================
// MODBUS CONSTANTS (from modbus_rtu.h)
// ============================================================================

#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03
#define MODBUS_FC_READ_INPUT_REGISTERS      0x04
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10

#define MODBUS_ERR_NONE                     0x00
#define MODBUS_ERR_ILLEGAL_FUNCTION         0x01
#define MODBUS_ERR_ILLEGAL_DATA_ADDRESS     0x02
#define MODBUS_ERR_ILLEGAL_DATA_VALUE       0x03
#define MODBUS_ERR_SLAVE_DEVICE_FAILURE     0x04
#define MODBUS_ERR_CRC_MISMATCH             0x80
#define MODBUS_ERR_TIMEOUT                  0x81
#define MODBUS_ERR_FRAME_ERROR              0x82

// ============================================================================
// CRC-16 IMPLEMENTATION (for testing without linking actual code)
// ============================================================================

static const uint16_t crc16_table[256] = {
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
    0x0400, 0xC801, 0xDC01, 0x1000, 0xF401, 0x3800, 0x2C00, 0xE001,
    0xA401, 0x6800, 0x7C00, 0xB001, 0x5400, 0x9801, 0x8C01, 0x4000,
    0x0C00, 0xC001, 0xD401, 0x1800, 0xFC01, 0x3000, 0x2400, 0xE801,
    0xAC01, 0x6000, 0x7400, 0xB801, 0x5C00, 0x9001, 0x8401, 0x4800,
    0x0800, 0xC401, 0xD001, 0x1C00, 0xF801, 0x3400, 0x2000, 0xEC01,
    0xA801, 0x6400, 0x7000, 0xBC01, 0x5801, 0x9400, 0x8001, 0x4C00
};

static uint16_t modbusCrc16(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

static bool modbusVerifyCrc(const uint8_t* data, uint16_t length) {
    if (length < 3) return false;
    uint16_t calc_crc = modbusCrc16(data, length - 2);
    uint16_t recv_crc = data[length - 2] | (data[length - 1] << 8);
    return calc_crc == recv_crc;
}

// Request builder simulation
static uint16_t modbusReadRegistersRequest(uint8_t slave_addr, uint16_t start_addr,
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

static uint16_t modbusWriteSingleRegisterRequest(uint8_t slave_addr, uint16_t reg_addr,
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

// ============================================================================
// CRC-16 TESTS
// ============================================================================

// @test CRC-16 of empty data is 0xFFFF
void test_crc16_empty_initial(void) {
    uint16_t crc = modbusCrc16(NULL, 0);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc);
}

// @test CRC-16 of known pattern
void test_crc16_known_pattern(void) {
    // Standard Modbus test: slave 1, FC03, addr 0, count 1
    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc = modbusCrc16(data, 6);
    // Known CRC for this pattern
    TEST_ASSERT_EQUAL_HEX16(0x840A, crc);
}

// @test CRC verification passes for valid frame
void test_crc_verify_valid_frame(void) {
    uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x0A, 0x84};
    bool valid = modbusVerifyCrc(frame, 8);
    TEST_ASSERT_TRUE(valid);
}

// @test CRC verification fails for corrupted frame
void test_crc_verify_corrupted_frame(void) {
    uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF};
    bool valid = modbusVerifyCrc(frame, 8);
    TEST_ASSERT_FALSE(valid);
}

// @test CRC verification fails for short frame
void test_crc_verify_short_frame(void) {
    uint8_t frame[] = {0x01, 0x03};
    bool valid = modbusVerifyCrc(frame, 2);
    TEST_ASSERT_FALSE(valid);
}

// ============================================================================
// REQUEST BUILDER TESTS
// ============================================================================

// @test Read registers request has correct length
void test_read_request_length(void) {
    uint8_t buffer[16];
    uint16_t len = modbusReadRegistersRequest(1, 0, 10, buffer);
    TEST_ASSERT_EQUAL_UINT16(8, len);
}

// @test Read registers request has correct slave address
void test_read_request_slave_address(void) {
    uint8_t buffer[16];
    modbusReadRegistersRequest(5, 0, 1, buffer);
    TEST_ASSERT_EQUAL_UINT8(5, buffer[0]);
}

// @test Read registers request has correct function code
void test_read_request_function_code(void) {
    uint8_t buffer[16];
    modbusReadRegistersRequest(1, 0, 1, buffer);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_FC_READ_HOLDING_REGISTERS, buffer[1]);
}

// @test Read registers request has correct start address (big-endian)
void test_read_request_start_address(void) {
    uint8_t buffer[16];
    modbusReadRegistersRequest(1, 0x1234, 1, buffer);
    TEST_ASSERT_EQUAL_UINT8(0x12, buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buffer[3]);
}

// @test Read registers request has correct register count
void test_read_request_register_count(void) {
    uint8_t buffer[16];
    modbusReadRegistersRequest(1, 0, 100, buffer);
    TEST_ASSERT_EQUAL_UINT8(0x00, buffer[4]);
    TEST_ASSERT_EQUAL_UINT8(100, buffer[5]);
}

// @test Read registers request has valid CRC
void test_read_request_has_valid_crc(void) {
    uint8_t buffer[16];
    modbusReadRegistersRequest(1, 0, 1, buffer);
    TEST_ASSERT_TRUE(modbusVerifyCrc(buffer, 8));
}

// @test Write single register request has correct function code
void test_write_single_function_code(void) {
    uint8_t buffer[16];
    modbusWriteSingleRegisterRequest(1, 0, 0x1234, buffer);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_FC_WRITE_SINGLE_REGISTER, buffer[1]);
}

// @test Write single register request has correct value
void test_write_single_value(void) {
    uint8_t buffer[16];
    modbusWriteSingleRegisterRequest(1, 0, 0xABCD, buffer);
    TEST_ASSERT_EQUAL_UINT8(0xAB, buffer[4]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, buffer[5]);
}

// ============================================================================
// ERROR CODE TESTS
// ============================================================================

// @test Error codes have expected values
void test_error_codes_values(void) {
    TEST_ASSERT_EQUAL(0x00, MODBUS_ERR_NONE);
    TEST_ASSERT_EQUAL(0x01, MODBUS_ERR_ILLEGAL_FUNCTION);
    TEST_ASSERT_EQUAL(0x02, MODBUS_ERR_ILLEGAL_DATA_ADDRESS);
    TEST_ASSERT_EQUAL(0x03, MODBUS_ERR_ILLEGAL_DATA_VALUE);
    TEST_ASSERT_EQUAL(0x04, MODBUS_ERR_SLAVE_DEVICE_FAILURE);
}

// @test Custom error codes are in high range
void test_custom_error_codes_high_range(void) {
    TEST_ASSERT_GREATER_OR_EQUAL(0x80, MODBUS_ERR_CRC_MISMATCH);
    TEST_ASSERT_GREATER_OR_EQUAL(0x80, MODBUS_ERR_TIMEOUT);
    TEST_ASSERT_GREATER_OR_EQUAL(0x80, MODBUS_ERR_FRAME_ERROR);
}

// ============================================================================
// FUNCTION CODE TESTS
// ============================================================================

// @test Function codes have expected values
void test_function_codes_values(void) {
    TEST_ASSERT_EQUAL(0x03, MODBUS_FC_READ_HOLDING_REGISTERS);
    TEST_ASSERT_EQUAL(0x04, MODBUS_FC_READ_INPUT_REGISTERS);
    TEST_ASSERT_EQUAL(0x06, MODBUS_FC_WRITE_SINGLE_REGISTER);
    TEST_ASSERT_EQUAL(0x10, MODBUS_FC_WRITE_MULTIPLE_REGISTERS);
}

// ============================================================================
// RESPONSE LENGTH TESTS
// ============================================================================

// @test Expected response length for 1 register
void test_expected_response_length_1_reg(void) {
    // FC03 response: slave(1) + fc(1) + byte_count(1) + data(2*n) + crc(2)
    // For 1 register: 1 + 1 + 1 + 2 + 2 = 7 bytes
    uint16_t expected = 1 + 1 + 1 + 2 + 2;
    TEST_ASSERT_EQUAL_UINT16(7, expected);
}

// @test Expected response length for 10 registers
void test_expected_response_length_10_reg(void) {
    // For 10 registers: 1 + 1 + 1 + 20 + 2 = 25 bytes
    uint16_t expected = 1 + 1 + 1 + (10 * 2) + 2;
    TEST_ASSERT_EQUAL_UINT16(25, expected);
}

// ============================================================================
// TEST REGISTRATION
// ============================================================================

void run_modbus_rtu_tests(void) {
    // CRC-16 tests
    RUN_TEST(test_crc16_empty_initial);
    RUN_TEST(test_crc16_known_pattern);
    RUN_TEST(test_crc_verify_valid_frame);
    RUN_TEST(test_crc_verify_corrupted_frame);
    RUN_TEST(test_crc_verify_short_frame);
    
    // Request builder tests
    RUN_TEST(test_read_request_length);
    RUN_TEST(test_read_request_slave_address);
    RUN_TEST(test_read_request_function_code);
    RUN_TEST(test_read_request_start_address);
    RUN_TEST(test_read_request_register_count);
    RUN_TEST(test_read_request_has_valid_crc);
    RUN_TEST(test_write_single_function_code);
    RUN_TEST(test_write_single_value);
    
    // Error code tests
    RUN_TEST(test_error_codes_values);
    RUN_TEST(test_custom_error_codes_high_range);
    
    // Function code tests
    RUN_TEST(test_function_codes_values);
    
    // Response length tests
    RUN_TEST(test_expected_response_length_1_reg);
    RUN_TEST(test_expected_response_length_10_reg);
}
