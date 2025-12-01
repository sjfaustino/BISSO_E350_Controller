#ifndef PLC_IFACE_H
#define PLC_IFACE_H

#include <Arduino.h>

// ============================================================================
// KC868-A16 PCF8574 I2C Expanders (ELBO Bit Mapping)
// ============================================================================

// PCF8574 @ 0x20 - ELBO I72 OUTPUT (Speed Selection to PLC)
#define PCF8574_I72_ADDR 0x20      // ELBO speed control register
#define ELBO_I72_FAST 0            // P0: FAST speed bit (Note: Pin mapping is now: 10 = Fast, 01 = Medium, 00 = Slow)
#define ELBO_I72_MED 1             // P1: MEDIUM speed bit
// P2-P7: Reserved

// PCF8574 @ 0x21 - ELBO I73 OUTPUT (Axis/Direction/Mode to PLC)
#define PCF8574_I73_ADDR 0x21      // ELBO axis + direction register
#define ELBO_I73_AXIS_Y 0          // P0: Axis Y select
#define ELBO_I73_AXIS_X 1          // P1: Axis X select
#define ELBO_I73_AXIS_Z 2          // P2: Axis Z select
#define ELBO_I73_RESERVED_3 3      // P3: Reserved (table rotate)
#define ELBO_I73_RESERVED_4 4      // P4: Reserved (tilt)
#define ELBO_I73_DIRECTION_PLUS 5  // P5: Direction + (forward)
#define ELBO_I73_DIRECTION_MINUS 6 // P6: Direction - (reverse)
#define ELBO_I73_V_S_MODE 7        // P7: V/S mode (slow override)

// PCF8574 @ 0x22 - ELBO Q73 INPUT (Consenso / Mode from PLC)
#define PCF8574_Q73_ADDR 0x22      // ELBO consenso input register
#define ELBO_Q73_CONSENSO_Y 0      // P0: PLC says Y axis OK
#define ELBO_Q73_CONSENSO_X 1      // P1: PLC says X axis OK
#define ELBO_Q73_CONSENSO_Z 2      // P2: PLC says Z axis OK
#define ELBO_Q73_AUTO_MANUAL 3     // P3: PLC auto/manual mode
// P4-P7: Reserved

// I2C Configuration
#define PLC_I2C_SPEED 100000
#define PLC_SDA_PIN 4              // GPIO4 = IIC_SDA on KC868-A16
#define PLC_SCL_PIN 5              // GPIO5 = IIC_SCL on KC868-A16
#define PLC_READ_INTERVAL_MS 50
#define PLC_READ_TIMEOUT_MS 1000

// NOTE: All PCF8574 outputs are ACTIVE LOW
// Writing 0 to a bit = ON (PLC sees 24V)
// Writing 1 to a bit = OFF (PLC sees 0V)

typedef enum {
  PLC_OK = 0,
  PLC_TIMEOUT = 1,
  PLC_NOT_FOUND = 2,
  PLC_CRC_ERROR = 3,
  PLC_INVALID_DATA = 4
} plc_status_t;

void plcIfaceInit();
void plcIfaceUpdate();

// Generic bit/byte operations (Deprecated, kept for compatibility)
bool plcGetBit(uint8_t bit);
void plcSetBit(uint8_t bit, bool value);
uint8_t plcGetByte(uint8_t offset);
void plcSetByte(uint8_t offset, uint8_t value);
uint16_t plcGetWord(uint8_t offset);
void plcSetWord(uint8_t offset, uint16_t value);

// ELBO-specific bit operations with error checking
bool elboI72GetSpeed(uint8_t speed_bit);        // Get FAST or MED bit
bool elboI72SetSpeed(uint8_t speed_bit, bool value);  // Set FAST or MED bit, returns success
bool elboI73GetAxis(uint8_t axis_bit);          // Get axis selection bit
bool elboI73SetAxis(uint8_t axis_bit, bool value);    // Set axis bit, returns success
bool elboI73GetDirection(uint8_t dir_bit);      // Get direction bit
bool elboI73SetDirection(uint8_t dir_bit, bool value); // Set direction bit, returns success
bool elboI73GetVSMode();                        // Get V/S mode
bool elboI73SetVSMode(bool value);              // Set V/S mode, returns success
bool elboQ73GetConsenso(uint8_t axis);          // Read axis consenso from PLC
bool elboQ73GetAutoManual();                    // Read auto/manual mode from PLC

plc_status_t plcGetStatus();
uint32_t plcGetLastReadTime();
uint32_t plcGetErrorCount();
void plcDiagnostics();

#endif