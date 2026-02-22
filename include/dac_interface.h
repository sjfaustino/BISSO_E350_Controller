/**
 * @file dac_interface.h
 * @brief Hardware Abstraction for PLC Analog Output Card (DAC)
 * @details Provides 0-10V control signals for VFD speed control.
 *          Assumes an I2C-controlled DAC (e.g. MCP4728) on the PLC bus.
 */

#ifndef DAC_INTERFACE_H
#define DAC_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

// DAC Channels
#define DAC_CH_VFD1_X      0   // VFD1 (X-Axis) Speed Control
#define DAC_CH_VFD2_YZA    1   // VFD2 (Y, Z, A) Speed Control
#define DAC_CH_AUX1        2   // Spare
#define DAC_CH_AUX2        3   // Spare

// Voltage Scaling (Assumes 10V = 50Hz or 60Hz depending on VFD config)
#define DAC_MAX_VOLTAGE    10.0f
#define DAC_MAX_RAW_VALUE  4095    // 12-bit DAC

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * @brief Initialize the DAC interface
 * @return true if DAC detected and initialized
 */
bool dacInit(void);

/**
 * @brief Set the output frequency for a specific VFD
 * @param vfd_id 0=VFD1(X), 1=VFD2(YZA)
 * @param frequency_hz Target frequency in Hz (typically 0-50.0 or 0-60.0)
 * @return true if successful
 */
bool dacSetFrequency(uint8_t vfd_id, float frequency_hz);

/**
 * @brief Set the output voltage directly on a DAC channel
 * @param channel DAC channel index (0-3)
 * @param voltage Target voltage (0.0 to 10.0V)
 * @return true if successful
 */
bool dacSetVoltage(uint8_t channel, float voltage);

/**
 * @brief Emergency stop for analog outputs (force all to 0V)
 */
void dacEmergencyStop(void);

/**
 * @brief Check if analog speed control is enabled in configuration
 */
bool dacIsAnalogEnabled(void);

#ifdef __cplusplus
}
#endif

#endif // DAC_INTERFACE_H
