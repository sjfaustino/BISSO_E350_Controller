/**
 * @file plc_iface.h
 * @brief ELBO PLC I2C Interface Definitions (v3.5.21)
 * @details Updated input reading API to support error detection.
 */

#ifndef PLC_IFACE_H
#define PLC_IFACE_H

#include <stdint.h>
#include <Arduino.h>

// ============================================================================
// I2C ADDRESS CONFIGURATION
// ============================================================================
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

/**
 * @brief Reads a specific bit from the I73 input board.
 * @param bit Bit index (0-7).
 * @param success [Optional] Pointer to bool. Set to true if I2C read succeeded, false if failed.
 * @return State of the bit (true/false). Returns stale cache if I2C fails.
 */
bool elboI73GetInput(uint8_t bit, bool* success = nullptr);

void elboQ73SetRelay(uint8_t bit, bool state);
void elboSetSpeedProfile(uint8_t profile_idx);
// PHASE 3.1: Added getter to allow LCD and diagnostics to display active speed profile
uint8_t elboGetSpeedProfile();  // Returns current speed profile (1-3)
void elboSetDirection(uint8_t axis, bool forward);
void elboDiagnostics();

// PHASE 5.7: Gemini Fix - Shadow register health monitoring
uint32_t elboGetMutexTimeoutCount();  // Returns count of shadow mutex timeouts
bool elboIsShadowRegisterDirty();     // Returns true if shadow register is out of sync 

#endif // PLC_IFACE_H