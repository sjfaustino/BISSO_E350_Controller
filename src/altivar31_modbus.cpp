/**
 * @file altivar31_modbus.cpp
 * @brief Altivar 31 VFD Modbus RTU Driver Implementation
 * @project BISSO E350 Controller - Phase 5.5
 * @details Modbus RTU interface for Altivar 31 VFD diagnostics
 *          Verified register addresses from ATV312 Programming Manual BBV51701
 */

#include "altivar31_modbus.h"
#include "encoder_hal.h"
#include "spindle_current_rs485.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <string.h>

// Modbus CRC-16 (CCITT) lookup table - shared with JXK-10
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

// Global Altivar 31 VFD state
static altivar31_state_t altivar31_state = {
    .slave_address = 1,
    .baud_rate = 19200,
    .frequency_raw = 0,
    .frequency_hz = 0.0f,
    .current_raw = 0,
    .current_amps = 0.0f,
    .status_word = 0,
    .fault_code = 0,
    .thermal_state = 0,
    .last_read_time_ms = 0,
    .last_error_time_ms = 0,
    .read_count = 0,
    .error_count = 0,
    .consecutive_errors = 0
};

// Modbus request/response buffer
static uint8_t modbus_tx_buffer[32];
static uint32_t modbus_request_time_ms = 0;
static uint16_t modbus_pending_register = 0;

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

    // Check exception response (bit 7 of function code set)
    if (buffer[1] & 0x80) {
        return NULL;  // Exception response
    }

    uint16_t crc = modbusCrc16(buffer, length - 2);
    uint16_t rx_crc = buffer[length - 2] | (buffer[length - 1] << 8);

    if (crc != rx_crc) {
        return NULL;  // CRC mismatch
    }

    return (const uint16_t*)&buffer[3];  // Point to data
}

bool altivar31ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    altivar31_state.slave_address = slave_address;
    altivar31_state.baud_rate = baud_rate;
    altivar31_state.frequency_raw = 0;
    altivar31_state.frequency_hz = 0.0f;
    altivar31_state.current_raw = 0;
    altivar31_state.current_amps = 0.0f;
    altivar31_state.status_word = 0;
    altivar31_state.fault_code = 0;
    altivar31_state.thermal_state = 0;
    altivar31_state.read_count = 0;
    altivar31_state.error_count = 0;
    altivar31_state.consecutive_errors = 0;

    Serial.printf("[ALTIVAR31] Initialized (Slave ID: %u, Baud: %lu)\n",
                  slave_address, (unsigned long)baud_rate);
    return true;
}

bool altivar31ModbusReadCurrent(void) {
    // Ensure we're on the PLC device (VFD is connected to PLC via Modbus)
    // For now, we'll attempt the read regardless (extension point for multiplexing)

    // Build Modbus RTU request for motor current register (3204)
    uint16_t tx_len = modbusReadRegistersRequest(altivar31_state.slave_address,
                                                  ALTIVAR31_REG_DRIVE_CURRENT, 1, modbus_tx_buffer);

    // Send via encoder_hal (shared RS485 interface)
    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        altivar31_state.error_count++;
        altivar31_state.consecutive_errors++;
        return false;
    }

    modbus_request_time_ms = millis();
    modbus_pending_register = ALTIVAR31_REG_DRIVE_CURRENT;
    return true;
}

bool altivar31ModbusReadFrequency(void) {
    // Build Modbus RTU request for output frequency register (3202)
    uint16_t tx_len = modbusReadRegistersRequest(altivar31_state.slave_address,
                                                  ALTIVAR31_REG_OUTPUT_FREQ, 1, modbus_tx_buffer);

    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        altivar31_state.error_count++;
        altivar31_state.consecutive_errors++;
        return false;
    }

    modbus_request_time_ms = millis();
    modbus_pending_register = ALTIVAR31_REG_OUTPUT_FREQ;
    return true;
}

bool altivar31ModbusReadStatus(void) {
    // Build Modbus RTU request for status word register (3201)
    uint16_t tx_len = modbusReadRegistersRequest(altivar31_state.slave_address,
                                                  ALTIVAR31_REG_DRIVE_STATUS, 1, modbus_tx_buffer);

    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        altivar31_state.error_count++;
        altivar31_state.consecutive_errors++;
        return false;
    }

    modbus_request_time_ms = millis();
    modbus_pending_register = ALTIVAR31_REG_DRIVE_STATUS;
    return true;
}

bool altivar31ModbusReadFaultCode(void) {
    // Build Modbus RTU request for fault code register (8606)
    uint16_t tx_len = modbusReadRegistersRequest(altivar31_state.slave_address,
                                                  ALTIVAR31_REG_FAULT_CODE, 1, modbus_tx_buffer);

    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        altivar31_state.error_count++;
        altivar31_state.consecutive_errors++;
        return false;
    }

    modbus_request_time_ms = millis();
    modbus_pending_register = ALTIVAR31_REG_FAULT_CODE;
    return true;
}

bool altivar31ModbusReadThermalState(void) {
    // Build Modbus RTU request for thermal state register (3209)
    uint16_t tx_len = modbusReadRegistersRequest(altivar31_state.slave_address,
                                                  ALTIVAR31_REG_THERMAL_STATE, 1, modbus_tx_buffer);

    if (!encoderHalSend(modbus_tx_buffer, tx_len)) {
        altivar31_state.error_count++;
        altivar31_state.consecutive_errors++;
        return false;
    }

    modbus_request_time_ms = millis();
    modbus_pending_register = ALTIVAR31_REG_THERMAL_STATE;
    return true;
}

bool altivar31ModbusReceiveResponse(void) {
    // Check if we have a complete response (timeout 200ms from request)
    uint32_t now = millis();
    if ((now - modbus_request_time_ms) < 50) {
        return false;  // Not enough time for response
    }

    // Attempt to receive response
    uint8_t rx_data[32];
    uint8_t rx_len = sizeof(rx_data);
    memset(rx_data, 0, sizeof(rx_data));

    if (!encoderHalReceive(rx_data, &rx_len)) {
        if ((now - modbus_request_time_ms) > 200) {
            altivar31_state.consecutive_errors++;
            altivar31_state.error_count++;
            altivar31_state.last_error_time_ms = now;
        }
        return false;  // No data available or timeout
    }

    if (rx_len == 0) {
        if ((now - modbus_request_time_ms) > 200) {
            altivar31_state.consecutive_errors++;
            altivar31_state.error_count++;
            altivar31_state.last_error_time_ms = now;
        }
        return false;  // No data available or timeout
    }

    // Parse response
    const uint16_t* regs = modbusParseReadResponse(rx_data, (uint16_t)rx_len, 1);
    if (regs == NULL) {
        altivar31_state.error_count++;
        altivar31_state.consecutive_errors++;
        altivar31_state.last_error_time_ms = now;
        return false;  // Invalid response
    }

    // Extract value based on pending register
    uint16_t raw_value = (regs[0] >> 8) | ((regs[0] & 0xFF) << 8);  // Big-endian conversion

    switch (modbus_pending_register) {
        case ALTIVAR31_REG_DRIVE_CURRENT:
            altivar31_state.current_raw = (int16_t)raw_value;
            altivar31_state.current_amps = altivar31_state.current_raw * 0.1f;  // 0.1 A units
            break;

        case ALTIVAR31_REG_OUTPUT_FREQ:
            altivar31_state.frequency_raw = (int16_t)raw_value;
            altivar31_state.frequency_hz = altivar31_state.frequency_raw * 0.1f;  // 0.1 Hz units
            break;

        case ALTIVAR31_REG_DRIVE_STATUS:
            altivar31_state.status_word = raw_value;
            break;

        case ALTIVAR31_REG_FAULT_CODE:
            altivar31_state.fault_code = raw_value;
            break;

        case ALTIVAR31_REG_THERMAL_STATE:
            altivar31_state.thermal_state = (int16_t)raw_value;  // Percentage (100% = nominal)
            break;

        default:
            break;
    }

    altivar31_state.read_count++;
    altivar31_state.last_read_time_ms = now;
    altivar31_state.consecutive_errors = 0;
    modbus_pending_register = 0;

    return true;
}

float altivar31GetCurrentAmps(void) {
    return altivar31_state.current_amps;
}

int16_t altivar31GetCurrentRaw(void) {
    return altivar31_state.current_raw;
}

float altivar31GetFrequencyHz(void) {
    return altivar31_state.frequency_hz;
}

int16_t altivar31GetFrequencyRaw(void) {
    return altivar31_state.frequency_raw;
}

uint16_t altivar31GetStatusWord(void) {
    return altivar31_state.status_word;
}

uint16_t altivar31GetFaultCode(void) {
    return altivar31_state.fault_code;
}

int16_t altivar31GetThermalState(void) {
    return altivar31_state.thermal_state;
}

bool altivar31IsFaulted(void) {
    return altivar31_state.fault_code != 0;
}

bool altivar31IsRunning(void) {
    // Bit 3 of status word indicates running state
    return (altivar31_state.status_word & 0x0008) != 0;
}

const altivar31_state_t* altivar31GetState(void) {
    return &altivar31_state;
}

void altivar31ResetErrorCounters(void) {
    altivar31_state.error_count = 0;
    altivar31_state.consecutive_errors = 0;
}

// ============================================================================
// MOTION VALIDATION (PHASE 5.5)
// ============================================================================

bool altivar31IsMotorRunning(void) {
    return altivar31_state.frequency_hz > 0.5f;  // Threshold: >0.5 Hz
}

bool altivar31DetectFrequencyLoss(float previous_freq_hz) {
    // CRITICAL FIX: Only use fresh data for stall detection
    // If Modbus read failed, data might be stale (retains old value) or invalid (reset to 0)
    // Using stale data masks real stalls or triggers false alarms
    uint32_t now = millis();
    uint32_t data_age_ms = now - altivar31_state.last_read_time_ms;

    // Data must be recent (< 1 second old) to be valid for stall detection
    // If Modbus communication is failing, data is unreliable
    if (data_age_ms > 1000) {
        // Stale data - Modbus read failures
        // Cannot reliably detect stall without fresh data
        return false;
    }

    float current_freq = altivar31_state.frequency_hz;

    // Detect frequency drop >80% in one sample (potential stall)
    // Only meaningful if previous frequency was significant
    if (previous_freq_hz > 1.0f && current_freq < (previous_freq_hz * 0.2f)) {
        return true;  // Frequency collapsed - motor likely stalled
    }

    return false;
}

void altivar31PrintDiagnostics(void) {
    Serial.println("\n[ALTIVAR31] === VFD Diagnostics ===");
    Serial.printf("Slave Address:       %u\n", altivar31_state.slave_address);
    Serial.printf("Baud Rate:           %lu bps\n", (unsigned long)altivar31_state.baud_rate);
    Serial.printf("Output Frequency:    %.1f Hz (raw: %d)\n", altivar31_state.frequency_hz, altivar31_state.frequency_raw);
    Serial.printf("Motor Current:       %.1f A (raw: %d)\n", altivar31_state.current_amps, altivar31_state.current_raw);
    Serial.printf("Status Word:         0x%04X (Running: %s)\n",
                  altivar31_state.status_word,
                  altivar31IsRunning() ? "YES" : "NO");
    Serial.printf("Fault Code:          0x%04X %s\n",
                  altivar31_state.fault_code,
                  altivar31IsFaulted() ? "(FAULT)" : "(OK)");
    Serial.printf("Thermal State:       %d%% (Nominal: 100%%)\n", altivar31_state.thermal_state);
    Serial.printf("Read Count:          %lu\n", (unsigned long)altivar31_state.read_count);
    Serial.printf("Error Count:         %lu\n", (unsigned long)altivar31_state.error_count);
    Serial.printf("Consecutive Errors:  %lu\n", (unsigned long)altivar31_state.consecutive_errors);
    if (altivar31_state.last_read_time_ms > 0) {
        Serial.printf("Last Read:           %lu ms ago\n", (unsigned long)(millis() - altivar31_state.last_read_time_ms));
    }
    if (altivar31_state.last_error_time_ms > 0) {
        Serial.printf("Last Error:          %lu ms ago\n", (unsigned long)(millis() - altivar31_state.last_error_time_ms));
    }
}
