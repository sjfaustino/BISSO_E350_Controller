/**
 * @file plc_iface.h
 * @brief Concrete Hardware Interface for KC868 I/O Expansion Boards
 * @project Gemini v3.3.1
 */

#ifndef PLC_IFACE_H
#define PLC_IFACE_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================================
// I2C ADDRESS CONFIGURATION (CRITICAL: Defines ADDR_I73_INPUT)
// ============================================================================
#define ADDR_I73_INPUT  0x21  // Limit Switches & Sensors
#define ADDR_Q73_OUTPUT 0x22  // Relays & VFD Control

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
#define ELBO_Q73_DIR_X      0
#define ELBO_Q73_DIR_Y      1
#define ELBO_Q73_DIR_Z      2
#define ELBO_Q73_DIR_A      3
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
bool elboQ73GetConsenso(uint8_t bit);
void elboSetSpeedProfile(uint8_t profile_idx);
void elboSetDirection(uint8_t axis, bool forward);

// Diagnostics (Used by CLI)
void elboDiagnostics(); 

#endif // PLC_IFACE_H