# ESP32-S3 Main Firmware Debug Guide

This guide explains how to debug the **BISSO E350 Main Controller** firmware on a bare ESP32-S3 DevKit (N16R8) without the full KC868 hardware.

## 1. Hardware Mismatch: Bare Board vs. Full Setup

When running the production firmware on a bare DevKit, you will encounter I2C errors because the firmware expects the following components:

```text
    [ BARE S3 DEVKIT ]             [ KC868-A16 v3.1 BOARD ]
    ┌────────────────┐             ┌───────────────────────────┐
    │                │             │  ESP32-S3 (Main)          │
    │  ESP32-S3 (S3) │             │     ├── I2C (Pins 9,10)   │
    │                │             │     │    ├── PCF8574 (Relays)
    └────────────────┘             │     │    ├── PCF8574 (Inputs)
           ▲                       │     │    ├── DS3231 (RTC)
           │                       │     │    └── SSD1306 (OLED)
       DEBUG MODE                  └─────┴─────────────────────┘
```

### Expected Initialization Errors (-1)
If you see the following in your Serial Monitor, specialized hardware is missing:
- `requestFrom(): i2cRead returned Error -1`: The firmware tried to query a PCF8574 or RTC that isn't there.
- `[PLC] [CRITICAL] Q73 Output Board Missing!`: Failure to find the relay expander.

**Is it safe?** Yes. The firmware is designed to detect these failures (`g_plc_hardware_present = false`) and will continue to boot in a "Safe/Degraded" mode for CLI and Web UI debugging.

---

## 2. Resolving WiFi Error 0x300a

The error `WiFiSTA.cpp:317] begin(): connect failed! 0x300a` means **NO_AP_FOUND**.

### The Cause
```text
    [ ESP32-S3 ]                               [ ACCESS POINT ]
    WiFi.begin() ──── ssid: "" (Empty) ─────┐       (None)
                 ──── pass: "" (Empty) ─────┤
                                            ▼
                                     [ ERROR 0x300a ]
```
On a fresh board, the NVS (Non-Volatile Storage) is empty. `WiFi.begin()` attempts to use last-saved credentials and fails.

### The Solution: Use CLI to set Station credentials
1. Connect via Serial (115200 baud).
2. Type the following commands:
   ```bash
   wifi_setsta "Your_SSID" "Your_Password"
   config set ota_chk_en 1  # Optional: Enable OTA check
   save
   reboot
   ```
3. After reboot, the board will connect to your WiFi and the error will disappear.

---

## 3. Advanced S3 N16R8 Mapping (I2C)

Standard S3 DevKits often use different I2C pins than the KC868 board.

| Signal | KC868-A16 v3.1 | Standard S3 DevKit |
| :--- | :--- | :--- |
| **SDA** | GPIO 9 | GPIO 8 |
| **SCL** | GPIO 10 | GPIO 9 |

### Overriding Pins for Debug
If you have matched external sensors to your S3 DevKit, use the CLI to remap them:
```bash
config set pin_i2c_sda 8
config set pin_i2c_scl 9
save
reboot
```

---

## 4. Troubleshooting Flash Modes

If your board bootloops **BEFORE** reaching the app (Serial Monitor shows ROM logs only):

- **Error**: `Octal Flash option selected, but EFUSE not configured!`
- **Reason**: Your board isn't pre-configured for OPI Flash.
- **Fix**: Use `memory_type = qio_opi` in `platformio.ini`.

### Memory/Flash Reference (N16R8)
- **N16**: 16MB Flash (usually needs `qio` or `dio`).
- **R8**: 8MB PSRAM (usually needs `opi`).

---
*Reference Document: BISSO E350 S3 Debug 1.0*
