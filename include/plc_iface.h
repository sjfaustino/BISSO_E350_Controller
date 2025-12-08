/**
 * @file plc_iface.h
 * @brief ELBO PLC I2C Interface Definitions (v3.5.10)
 * @details synchronized with plc_iface.cpp
 */

#ifndef PLC_IFACE_H
#define PLC_IFACE_H

#include <stdint.h>
#include <Arduino.h>

// ============================================================================
// I2C ADDRESS CONFIGURATION
// ============================================================================
// User defined addresses (Match your board jumpers!)
#define ADDR_I73_INPUT  0x21  // Limit Switches & Sensors (PCF8574)
#define ADDR_Q73_OUTPUT 0x22  // Relays & VFD Control   (PCF8574)

// ============================================================================
// I73 INPUT MAP (Read-Only)
// ============================================================================
#define ELBO_I73_AXIS_X     0
#define ELBO_I73_AXIS_Y     1
#define ELBO_I73_AXIS_Z     2
#define ELBO_I73_AXIS_A     3
#define ELBO_I73_CONSENSO_X 4
#define ELBO_I73_CONSENSO_Y 5
#define ELBO_I73_CONSENSO_Z 6
#define ELBO_I73_CONSENSO_A 7

// ============================================================================
// Q73 OUTPUT MAP (Write-Only)
// ============================================================================
// Direction Relays
#define ELBO_Q73_AXIS_X_DIR 0
#define ELBO_Q73_AXIS_Y_DIR 1
#define ELBO_Q73_AXIS_Z_DIR 2
#define ELBO_Q73_AXIS_A_DIR 3

// Speed & Control
#define ELBO_Q73_SPEED_1    4 
#define ELBO_Q73_SPEED_2    5 
#define ELBO_Q73_SPEED_3    6 
#define ELBO_Q73_ENABLE     7 

// ============================================================================
// PUBLIC API
// ============================================================================

void elboInit(); 
bool elboI73GetInput(uint8_t bit);
void elboQ73SetRelay(uint8_t bit, bool state);
void elboSetSpeedProfile(uint8_t profile_idx);
void elboSetDirection(uint8_t axis, bool forward);
void elboDiagnostics(); 

#endif // PLC_IFACE_H