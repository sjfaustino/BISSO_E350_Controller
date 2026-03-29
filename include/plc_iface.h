/**
 * @file plc_iface.h
 * @brief ELBO PLC I2C Interface Definitions (v3.5.21)
 * @details Updated input reading API to support error detection.
 */

#ifndef PLC_IFACE_H
#define PLC_IFACE_H

#include <Arduino.h>
#include <stdint.h>
#include "system_constants.h"


// ============================================================================
// I2C ADDRESS CONFIGURATION
// ============================================================================
#define ADDR_I73_INPUT 0x21  // Reads S5 Q73 (PLC Status/Limits)
#define ADDR_Q73_OUTPUT 0x24 // Writes S5 I73 (Bank 1: Enable/Locks)
#define ADDR_Q73_AUX    0x25 // Writes S5 I72 (Bank 2: Axis/Dir/Ready)

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

// Output Map for S5 I 72 Bus (Bank 2 / 0x25 / Y9-Y16)
// VERIFIED BY S5 PLC LOGIC (PB10, PB20, PB30)
// This byte is the primary Axis selection and Direction command byte.
#define PLC_OUT_AXIS_X_SELECT 0  // Y9  -> I 72.0 (PB20 entry)
#define PLC_OUT_AXIS_Y_SELECT 1  // Y10 -> I 72.1 (PB40 entry)
#define PLC_OUT_AXIS_Z_SELECT 2  // Y11 -> I 72.2 (PB30 entry)
#define PLC_OUT_AXIS_A_SELECT 3  // Y12 -> I 72.3 (PB50 entry)
#define PLC_OUT_AXIS_DISK_ROT 4  // Y13 -> I 72.4 (PB52 entry)
#define PLC_OUT_DIR_POSITIVE  5  // Y14 -> I 72.5 (Direction bit)
#define PLC_OUT_DIR_NEGATIVE  6  // Y15 -> I 72.6 (Direction bit)
#define PLC_OUT_SYSTEM_READY  7  // Y16 -> I 72.7 (PB30: Handshake bit)

// Output Map for S5 I 73 Bus (Bank 1 / 0x24 / Y1-Y8)
// This byte contains secondary status and velocity enable signals.
#define PLC_OUT_MASTER_ENABLE 7  // Y8  -> I 73.7 (OB1: Velocity Enable)

#define PLC_OUT_SPEED_FAST    5
#define PLC_OUT_SPEED_MEDIUM  6
#define PLC_OUT_SPEED_SLOW    7

// Legacy aliases for backward compatibility 
#define ELBO_Q73_SPEED_1      0  // Dummy for now
#define ELBO_Q73_SPEED_2      1
#define ELBO_Q73_SPEED_3      2
#define ELBO_Q73_ENABLE       255

// ============================================================================
// PERFORMANCE TRACKING
// ============================================================================
typedef struct {
    uint32_t min_us;
    uint32_t max_us;
    uint32_t avg_us;
    uint32_t std_dev_us;
    uint32_t samples;
} bus_latency_stats_t;

// ============================================================================
// PUBLIC API
// ============================================================================

result_t elboInit();

/**
 * @brief Reads a specific bit from the I73 input board.
 * @param bit Bit index (0-7).
 * @param success [Optional] Pointer to bool. Set to true if I2C read succeeded,
 * false if failed.
 * @return State of the bit (true/false). Returns stale cache if I2C fails.
 */
bool elboGetInput(uint8_t bit);          // Read bit (0-7: Bus 73, 8-15: Bus 72)
bool elboI73GetInput(uint8_t bit, bool *success = nullptr); // Legacy

// New API - correct signal control
void elboI73Refresh();   // Public periodic refresh
void plcSetAxisSelect(uint8_t axis);     // 0=X, 1=Y, 2=Z, 255=none
void plcSetDirection(bool positive);     // true=+, false=-
void plcSetSpeed(uint8_t speed_profile); // 0=fast, 1=medium, 2=slow
uint8_t plcGetSpeedProfile();            // NEW
void plcClearAllOutputs();               // Clear all outputs (stop)
void plcCommitOutputs();                 // Write shadow register to I2C
void plcSetOutput(uint16_t pin, bool state); // Dynamic output control (Virtual/Legacy)
void plcSetAuxRelay(uint8_t bit, bool state); // Control Bank 2 (Y9-Y16)
void plcPrintDiagnostics();               // NEW - detailed IO diagnostics

// Transaction API for batching I2C writes
void plcBeginTransaction();              // Delay I2C writes until EndTransaction
void plcEndTransaction();                // Commit changes and resume immediate writes

// --- LEGACY API (still in use) ---
void elboQ73SetRelay(uint8_t bit, bool state);

// Health monitoring
uint32_t elboGetMutexTimeoutCount();
bool elboIsShadowRegisterDirty();
bool plcIsHardwarePresent();  // Returns false if PLC I2C board not detected at boot
uint8_t elboI73GetRawState();            // Read Bus 73 shadow
uint8_t elboI72GetRawState();            // Read Bus 72 shadow
uint16_t plcGetInputRawState();          // Read 16-bit combined input
uint8_t elboQ73GetRawState();            // Read output shadow (Bank 1 / I 73)
uint8_t elboQ73GetAuxRawState();         // Read output shadow (Bank 2 / I 72)

// Performance Analytics
void plcGetInputLatency(bus_latency_stats_t* stats);
void plcGetOutputLatency(bus_latency_stats_t* stats);
void plcResetLatencyStats();
uint32_t plcGetRecoveryCount();           // NEW: Returns count of I2C bus recoveries

#endif // PLC_IFACE_H
