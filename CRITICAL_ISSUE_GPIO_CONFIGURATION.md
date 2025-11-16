# CRITICAL ISSUE: GPIO PINS ON KC868-A16 I2C EXPANSION

**Date:** November 15, 2025  
**Status:** ⚠️ CRITICAL - GPIO configuration incorrect  
**Severity:** HIGH - Speed profile pins won't work

---

## THE PROBLEM

The firmware specifies GPIO pins 14 and 15 for speed profile notification:
```cpp
#define GPIO_SPEED_PROFILE_BIT_0 14   // GPIO14: Profile bit 0
#define GPIO_SPEED_PROFILE_BIT_1 15   // GPIO15: Profile bit 1
```

**But:** The baseboard is KC868-A16, which uses **I2C expanders**, not direct GPIO pins.

---

## KC868-A16 ARCHITECTURE

### Physical Layout
```
ESP32-S3 (Main Controller)
    ↓ I2C (SDA=GPIO4, SCL=GPIO5)
    ↓
KC868-A16 (I/O Expansion Board)
    ├─ MCP23017 Chip (I2C address 0x20)
    └─ 16 Channels (CH1-CH16) via I2C
        ├─ CH1-CH4: GPIO expansion outputs
        ├─ CH5-CH8: More outputs
        └─ CH9-CH16: More outputs or inputs
```

### Available I/O (From memory notes)
- **KC868-A16 Silkscreen Names:**
  - CH1-CH4: GPIO expansion channels
  - IIC_SDA (GPIO4), IIC_SCL (GPIO5): I2C bus
  - RS485_RXD (GPIO16), RS485_TXD (GPIO13): Serial
  - HT1 (GPIO14), HT2 (GPIO33): WJ66 Encoder UART

**Wait!** GPIO14 and GPIO33 ARE used - HT1 and HT2 are the WJ66 encoder UART pins!

---

## CRITICAL CONFLICT (Verified from Wiring Diagram)

| Pin | Firmware Use | Actual Hardware | Conflict |
|-----|-------------|-----------------|----------|
| GPIO14 | Speed Profile Bit 0 ❌ | UART1 RX (WJ66 Encoder) | **CRITICAL** |
| GPIO15 | Speed Profile Bit 1 ❌ | Not available | **UNUSABLE** |

**From Hardware Wiring Diagram (02_HARDWARE_WIRING_DIAGRAM.md):**
- GPIO14 = UART1 RX (WJ66 encoder @ 9600 baud)
- GPIO33 = UART1 TX (WJ66 encoder @ 9600 baud)
- GPIO21 = I2C SDA (PCF8574 I2C expander)
- GPIO22 = I2C SCL (PCF8574 I2C expander)
- GPIO1/3 = UART0 TX/RX (USB debug serial)

---

## SOLUTIONS

### Option 1: Use I2C Expanded I/O (Recommended)
Use KC868-A16 I2C expansion channels instead of direct GPIO:

```cpp
// Use I2C expansion board channels for speed profile
#define SPEED_PROFILE_BIT_0_CHANNEL 1   // CH1 on KC868-A16
#define SPEED_PROFILE_BIT_1_CHANNEL 2   // CH2 on KC868-A16

void motionSetPLCSpeedProfile(speed_profile_t profile) {
  uint8_t bit0 = (profile) & 0x01;
  uint8_t bit1 = (profile >> 1) & 0x01;
  
  // Set via I2C expander instead of GPIO
  i2cSetBit(SPEED_PROFILE_BIT_0_CHANNEL, bit0);
  i2cSetBit(SPEED_PROFILE_BIT_1_CHANNEL, bit1);
  
  logInfo("[MOTION] Speed profile %d via I2C (CH%d:%d, CH%d:%d)", 
          profile + 1, 
          SPEED_PROFILE_BIT_0_CHANNEL, bit0,
          SPEED_PROFILE_BIT_1_CHANNEL, bit1);
}
```

### Option 2: Use Different ESP32 Pins
Find unused ESP32 pins not used by KC868-A16:

```cpp
// Check available pins on ESP32-S3 not used by KC868-A16
#define GPIO_SPEED_PROFILE_BIT_0 12   // Available pin
#define GPIO_SPEED_PROFILE_BIT_1 11   // Available pin
```

**Issue:** Must verify these pins are actually available and not conflicting

### Option 3: Use Existing I2C Commands
Use PLC communication via I2C instead of dedicated GPIO:

```cpp
// Set speed profile via PLC I2C register instead of GPIO
void motionSetPLCSpeedProfile(speed_profile_t profile) {
  plcSetByte(PLC_SPEED_PROFILE_REGISTER, profile);
  logInfo("[MOTION] Speed profile via PLC register: %d", profile + 1);
}
```

---

## CORRECT KC868-A16 PIN MAPPING

From system notes:
```
ESP32-S3 to KC868-A16:
- GPIO4  = IIC_SDA (I2C Data)
- GPIO5  = IIC_SCL (I2C Clock)
- GPIO14 = HT1 (WJ66 Encoder RX) ← CONFLICT!
- GPIO33 = HT2 (WJ66 Encoder TX) ← CONFLICT!
- GPIO16 = RS485_RXD
- GPIO13 = RS485_TXD

I2C Expander Channels (via I2C):
- CH1-CH4: Available for expansion
- Configurable as input or output
```

---

## RECOMMENDED SOLUTION - Use PCF8574 I2C Expansion

**The KC868-A16 has PCF8574 I2C expander with 8 available output channels.**

```cpp
// Use PCF8574 I2C expander channels (only available I/O on KC868-A16)
#define SPEED_PROFILE_BIT_0_PCF_PIN 0   // P0 on PCF8574 (available)
#define SPEED_PROFILE_BIT_1_PCF_PIN 1   // P1 on PCF8574 (available)

void motionSetPLCSpeedProfile(speed_profile_t profile) {
  uint8_t bit0 = (profile) & 0x01;
  uint8_t bit1 = (profile >> 1) & 0x01;
  
  // Set via PCF8574 I2C expander (ONLY option on KC868-A16)
  plcSetBit(SPEED_PROFILE_BIT_0_PCF_PIN, bit0);
  plcSetBit(SPEED_PROFILE_BIT_1_PCF_PIN, bit1);
  
  logInfo("[MOTION] Speed profile %d set (P%d:%d, P%d:%d)", 
          profile + 1, 
          SPEED_PROFILE_BIT_0_PCF_PIN, bit0,
          SPEED_PROFILE_BIT_1_PCF_PIN, bit1);
}
```

**This is the ONLY viable solution** because:
- ✅ GPIO14 is committed to WJ66 encoder
- ✅ GPIO15 is not available on KC868-A16
- ✅ PCF8574 has 8 available I/O pins on I2C bus
- ✅ Uses existing I2C infrastructure
- ✅ Direct GPIO pins are not available

---

## VERIFICATION CHECKLIST

Before production:
- [ ] Verify GPIO14, GPIO15 are available (check WJ66 conflict)
- [ ] OR switch to I2C expansion approach
- [ ] OR identify alternative GPIO pins
- [ ] Test speed profile GPIO with PLC
- [ ] Verify PLC reads correct profile
- [ ] Update documentation with correct pins

---

## FILES TO FIX

| File | Change |
|------|--------|
| system_constants.h | Remove GPIO14/15 OR use I2C channels |
| motion.cpp | Update motionSetPLCSpeedProfile() |
| motion.h | Update function if needed |
| Documentation | Correct GPIO/I2C mapping |

---

## IMPACT

**Current State:** ❌ GPIO pins won't work on KC868-A16  
**After Fix:** ✅ Speed profile GPIO will work correctly

---

## CRITICAL ACTION REQUIRED

This must be resolved before production deployment. The speed profile GPIO communication won't function as currently configured.

**Recommended:** Implement using I2C expansion approach (Option 1) as it's most aligned with KC868-A16 architecture.

