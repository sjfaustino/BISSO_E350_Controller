#include "altivar31_modbus.h"
#include "modbus_rtu.h"
#include "encoder_hal.h"
#include "rs485_device_registry.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_keys.h"
#include <Arduino.h>
#include <string.h>

// ============================================================================
// MODULE STATE
// ============================================================================

static altivar31_state_t altivar31_state = {
    .enabled = false,
    .slave_address = 2,  // Default differs from JXK-10 (1)
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

// Modbus request buffer
static uint8_t modbus_tx_buffer[16];
static uint16_t modbus_pending_register = 0;

// Polling sequence state
static uint8_t poll_step = 0;
static const uint16_t poll_registers[] = {
    ALTIVAR31_REG_DRIVE_STATUS,
    ALTIVAR31_REG_OUTPUT_FREQ,
    ALTIVAR31_REG_DRIVE_CURRENT,
    ALTIVAR31_REG_FAULT_CODE,
    ALTIVAR31_REG_THERMAL_STATE
};
#define POLL_STEP_COUNT (sizeof(poll_registers) / sizeof(poll_registers[0]))

// Forward declarations for registry callbacks
static bool altivar31Poll(void);
static bool altivar31OnResponse(const uint8_t* data, uint16_t len);

// Registry descriptor
static rs485_device_t altivar31_device = {
    .name = "Altivar31",
    .type = RS485_DEVICE_TYPE_VFD,
    .slave_address = 2,  // Default differs from JXK-10 (1) to prevent conflict
    .poll_interval_ms = 50,  // Fast polling for VFD
    .priority = 5,
    .enabled = false,
    .poll = altivar31Poll,
    .on_response = altivar31OnResponse,
    .last_poll_time_ms = 0,
    .poll_count = 0,
    .error_count = 0,
    .consecutive_errors = 0,
    .pending_response = false
};

// ============================================================================
// REGISTRY CALLBACKS
// ============================================================================

static bool altivar31Poll(void) {
    modbus_pending_register = poll_registers[poll_step];
    
    uint16_t tx_len = modbusReadRegistersRequest(altivar31_state.slave_address,
                                                  modbus_pending_register, 1, modbus_tx_buffer);

    return encoderHalSend(modbus_tx_buffer, tx_len);
}

static bool altivar31OnResponse(const uint8_t* data, uint16_t len) {
    uint16_t regs[1];
    uint8_t err = modbusParseReadResponse(data, len, 1, regs);
    
    if (err != MODBUS_ERR_NONE) {
        altivar31_state.last_error_time_ms = millis();
        return false;
    }

    uint16_t raw_value = regs[0];

    switch (modbus_pending_register) {
        case ALTIVAR31_REG_DRIVE_CURRENT:
            altivar31_state.current_raw = (int16_t)raw_value;
            altivar31_state.current_amps = altivar31_state.current_raw * 0.1f;
            break;

        case ALTIVAR31_REG_OUTPUT_FREQ:
            altivar31_state.frequency_raw = (int16_t)raw_value;
            altivar31_state.frequency_hz = altivar31_state.frequency_raw * 0.1f;
            break;

        case ALTIVAR31_REG_DRIVE_STATUS:
            altivar31_state.status_word = raw_value;
            break;

        case ALTIVAR31_REG_FAULT_CODE:
            altivar31_state.fault_code = raw_value;
            break;

        case ALTIVAR31_REG_THERMAL_STATE:
            altivar31_state.thermal_state = (int16_t)raw_value;
            break;
    }

    altivar31_state.read_count++;
    altivar31_state.last_read_time_ms = millis();
    altivar31_state.consecutive_errors = 0;

    // Advance to next register in sequence
    poll_step = (poll_step + 1) % POLL_STEP_COUNT;
    
    return true;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool altivar31ModbusInit(uint8_t slave_address, uint32_t baud_rate) {
    // Check if VFD is enabled in configuration
    if (configGetInt(KEY_VFD_EN, 1) == 0) {
        logInfo("[ALTIVAR31] VFD disabled in configuration");
        altivar31_state.enabled = false;
        altivar31_device.enabled = false;
        return true;  // Not an error, just disabled
    }
    
    // Use address from config if available
    uint8_t cfg_addr = (uint8_t)configGetInt(KEY_VFD_ADDR, slave_address);
    
    altivar31_state.slave_address = cfg_addr;
    altivar31_state.baud_rate = baud_rate;
    altivar31_state.enabled = true;  // Mark sensor as enabled
    
    // Update registry descriptor
    altivar31_device.slave_address = cfg_addr;
    altivar31_device.enabled = true;
    
    // Register with centralized bus manager
    if (!rs485RegisterDevice(&altivar31_device)) {
        logError("[ALTIVAR31] Failed to register device");
        return false;
    }

    logInfo("[ALTIVAR31] Initialized and registered (Addr: %u, Baud: %lu)",
            cfg_addr, (unsigned long)baud_rate);
    return true;
}

bool altivar31ModbusReadCurrent(void) {
    modbus_pending_register = ALTIVAR31_REG_DRIVE_CURRENT;
    return rs485RequestImmediatePoll(&altivar31_device);
}

bool altivar31ModbusReadFrequency(void) {
    modbus_pending_register = ALTIVAR31_REG_OUTPUT_FREQ;
    return rs485RequestImmediatePoll(&altivar31_device);
}

bool altivar31ModbusReadStatus(void) {
    modbus_pending_register = ALTIVAR31_REG_DRIVE_STATUS;
    return rs485RequestImmediatePoll(&altivar31_device);
}

bool altivar31ModbusReadFaultCode(void) {
    modbus_pending_register = ALTIVAR31_REG_FAULT_CODE;
    return rs485RequestImmediatePoll(&altivar31_device);
}

bool altivar31ModbusReadThermalState(void) {
    modbus_pending_register = ALTIVAR31_REG_THERMAL_STATE;
    return rs485RequestImmediatePoll(&altivar31_device);
}

bool altivar31ModbusReceiveResponse(void) {
    return true; // Managed by registry update loop
}

float altivar31GetCurrentAmps(void) { return altivar31_state.current_amps; }
int16_t altivar31GetCurrentRaw(void) { return altivar31_state.current_raw; }
float altivar31GetFrequencyHz(void) { return altivar31_state.frequency_hz; }
int16_t altivar31GetFrequencyRaw(void) { return altivar31_state.frequency_raw; }
uint16_t altivar31GetStatusWord(void) { return altivar31_state.status_word; }
uint16_t altivar31GetFaultCode(void) { return altivar31_state.fault_code; }
int16_t altivar31GetThermalState(void) { return altivar31_state.thermal_state; }

bool altivar31IsFaulted(void) { return altivar31_state.fault_code != 0; }
bool altivar31IsRunning(void) { return (altivar31_state.status_word & 0x0008) != 0; }

const altivar31_state_t* altivar31GetState(void) {
    altivar31_state.read_count = altivar31_device.poll_count;
    altivar31_state.error_count = altivar31_device.error_count;
    altivar31_state.consecutive_errors = altivar31_device.consecutive_errors;
    return &altivar31_state;
}

void altivar31ResetErrorCounters(void) {
    altivar31_device.poll_count = 0;
    altivar31_device.error_count = 0;
    altivar31_device.consecutive_errors = 0;
}

bool altivar31IsMotorRunning(void) {
    return altivar31_state.frequency_hz > 0.5f;
}

bool altivar31DetectFrequencyLoss(float previous_freq_hz) {
    uint32_t now = millis();
    if (now - altivar31_state.last_read_time_ms > 1000) return false;

    float current_freq = altivar31_state.frequency_hz;
    if (previous_freq_hz > 1.0f && current_freq < (previous_freq_hz * 0.2f)) return true;

    return false;
}

void altivar31PrintDiagnostics(void) {
    serialLoggerLock();
    Serial.println("\n[ALTIVAR31] === VFD Diagnostics ===");
    Serial.printf("Slave Address:       %u\n", altivar31_state.slave_address);
    Serial.printf("Output Frequency:    %.1f Hz\n", altivar31_state.frequency_hz);
    Serial.printf("Motor Current:       %.1f A\n", altivar31_state.current_amps);
    Serial.printf("Status Word:         0x%04X (Running: %s)\n", altivar31_state.status_word, altivar31IsRunning() ? "YES" : "NO");
    Serial.printf("Fault Code:          0x%04X %s\n", altivar31_state.fault_code, altivar31IsFaulted() ? "(FAULT)" : "(OK)");
    Serial.printf("Thermal State:       %d%%\n", altivar31_state.thermal_state);
    Serial.printf("Read Count:          %lu\n", (unsigned long)altivar31_device.poll_count);
    Serial.printf("Error Count:         %lu\n", (unsigned long)altivar31_device.error_count);
    Serial.printf("Consecutive Errors:  %lu\n", (unsigned long)altivar31_device.consecutive_errors);
    serialLoggerUnlock();
}
