/**
 * @file plc_iface.h
 * @brief ELBO PLC I2C Interface Definitions (v3.5.21)
 * @details Updated input reading API to support error detection.
 */

#ifndef PLC_IFACE_H
#define PLC_IFACE_H

#include <Arduino.h>
#include <stdint.h>


// ============================================================================
// I2C ADDRESS CONFIGURATION
// ============================================================================
#define ADDR_I73_INPUT 0x21  // Limit Switches & Sensors (PCF8574)
#define ADDR_Q73_OUTPUT 0x24 // Relays & VFD Control   (PCF8574)

// ============================================================================
// I73 INPUT MAP (Read-Only)
// ============================================================================
#define ELBO_I73_AXIS_X 0
#define ELBO_I73_AXIS_Y 1
#define ELBO_I73_AXIS_Z 2
#define ELBO_I73_AXIS_A 3
#define ELBO_I73_CONSENSO_X 4
#define ELBO_I73_CONSENSO_Y 5
#define ELBO_I73_CONSENSO_Z 6
#define ELBO_I73_CONSENSO_A 7

// ============================================================================
// PLC OUTPUT MAP (ESP32 → PLC via PCF8574 @ 0x24)
// Matches actual hardware wiring: Y1-Y8 on KC868-A16
// ============================================================================

// Axis Select (only ONE active at a time)
#define PLC_OUT_AXIS_X_SELECT 0  // Y1 - Select X axis
#define PLC_OUT_AXIS_Y_SELECT 1  // Y2 - Select Y axis
#define PLC_OUT_AXIS_Z_SELECT 2  // Y3 - Select Z axis

// Direction (mutually exclusive: one OR the other, not both)
#define PLC_OUT_DIR_POSITIVE 3   // Y4 - Direction +
#define PLC_OUT_DIR_NEGATIVE 4   // Y5 - Direction -

// Speed (only ONE active at a time)
#define PLC_OUT_SPEED_FAST 5     // Y6 - Fast speed
#define PLC_OUT_SPEED_MEDIUM 6   // Y7 - Medium speed
#define PLC_OUT_SPEED_SLOW 7     // Y8 - Slow speed (V/S)

// Legacy aliases for backward compatibility during transition
#define ELBO_Q73_SPEED_1 PLC_OUT_SPEED_FAST
#define ELBO_Q73_SPEED_2 PLC_OUT_SPEED_MEDIUM
#define ELBO_Q73_SPEED_3 PLC_OUT_SPEED_SLOW
#define ELBO_Q73_ENABLE 255  // No longer used (set axis instead)

// ============================================================================
// PUBLIC API
// ============================================================================

void elboInit();

/**
 * @brief Reads a specific bit from the I73 input board.
 * @param bit Bit index (0-7).
 * @param success [Optional] Pointer to bool. Set to true if I2C read succeeded,
 * false if failed.
 * @return State of the bit (true/false). Returns stale cache if I2C fails.
 */
bool elboI73GetInput(uint8_t bit, bool *success = nullptr);

// New API - correct signal control
void plcSetAxisSelect(uint8_t axis);     // 0=X, 1=Y, 2=Z, 255=none
void plcSetDirection(bool positive);     // true=+, false=-
void plcSetSpeed(uint8_t speed_profile); // 0=fast, 1=medium, 2=slow
void plcClearAllOutputs();               // Clear all outputs (stop)
void plcCommitOutputs();                 // Write shadow register to I2C

// Legacy API (redirects to new API internally)
void elboQ73SetRelay(uint8_t bit, bool state);
void elboSetSpeedProfile(uint8_t profile_idx);
uint8_t elboGetSpeedProfile();
void elboSetDirection(uint8_t axis, bool forward);
void elboDiagnostics();

// Health monitoring
uint32_t elboGetMutexTimeoutCount();
bool elboIsShadowRegisterDirty();
bool plcIsHardwarePresent();  // Returns false if PLC I2C board not detected at boot
uint8_t elboI73GetRawState(); // Returns current input byte
uint8_t elboQ73GetRawState(); // Returns current output byte shadow

#endif // PLC_IFACE_H