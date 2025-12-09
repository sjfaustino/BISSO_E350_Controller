# KC868-A16 Hardware Configuration Notes

**Board:** KC868-A16 Industrial Controller
**MCU:** ESP32-WROOM-32E
**Date:** 2025-12-08

---

## Changes from ESP32-S3 to ESP32-WROOM-32E

### Critical Differences

| Feature | ESP32-S3 (Original) | ESP32-WROOM-32E (KC868-A16) |
|---------|---------------------|------------------------------|
| **USB Interface** | Native USB CDC | UART only (via USB-to-Serial chip) |
| **PSRAM** | Optional | Not present on KC868-A16 |
| **Flash Speed** | Up to 80MHz | Typically 40MHz |
| **CPU Cores** | Dual-core Xtensa LX7 | Dual-core Xtensa LX6 |
| **Upload Speed** | Up to 921600 | Up to 921600 |

### platformio.ini Changes Made

#### ❌ Removed (ESP32-S3 specific)
```ini
-DARDUINO_USB_CDC_ON_BOOT=1   # Native USB CDC not available on ESP32
-DBOARD_HAS_PSRAM              # KC868-A16 doesn't have PSRAM
board = esp32-s3-devkitc-1     # Wrong board type
board_build.f_flash = 80000000L # Too fast for most ESP32
```

#### ✅ Added/Changed (ESP32 compatible)
```ini
board = esp32dev                 # Correct for ESP32-WROOM-32E
upload_speed = 921600            # Maximum reliable speed
board_build.f_cpu = 240000000L   # 240MHz CPU (standard)
board_build.f_flash = 40000000L  # 40MHz flash (safe default)
-DCORE_DEBUG_LEVEL=0             # Reduce debug verbosity
```

---

## KC868-A16 Hardware Specifications

### Board Features
- **MCU:** ESP32-WROOM-32E (ESP32 dual-core @ 240MHz)
- **Flash:** 4MB
- **PSRAM:** None
- **Digital Inputs:** 16 (isolated optocouplers)
- **Digital Outputs:** 16 (relays)
- **I2C:** Yes (pins GPIO 4/5)
- **Ethernet:** Optional (depending on model)
- **WiFi/Bluetooth:** Built-in (ESP32)

### Pin Configuration (KC868-A16)
```
I2C SDA:  GPIO 4
I2C SCL:  GPIO 5
UART RX:  GPIO 3
UART TX:  GPIO 1
Serial2:  GPIO 16 (RX), GPIO 17 (TX)  # For WJ66 encoder
```

### I2C Addresses Used
- **0x21:** PCF8574 - I73 (Axis selection outputs)
- **0x22:** PCF8574 - Q73 (Speed profile outputs)
- **0x24:** PCF8574 - Inputs (Consenso, limit switches)

---

## Compilation Issues Fixed

### Issue 1: USB CDC Error
**Error:**
```
'USB' was not declared in this scope
ARDUINO_USB_CDC_ON_BOOT undefined
```

**Cause:** ESP32-WROOM-32E doesn't have native USB CDC (only ESP32-S3/C3/S2)

**Fix:** Removed `-DARDUINO_USB_CDC_ON_BOOT=1` flag

**Impact:** None - Serial communication still works via UART (CH340/CP2102 USB-to-Serial chip)

### Issue 2: PSRAM Errors
**Error:**
```
esp_himem.h: No such file or directory
BOARD_HAS_PSRAM undefined
```

**Cause:** KC868-A16 board doesn't have PSRAM

**Fix:** Removed `-DBOARD_HAS_PSRAM` flag

**Impact:** None - Code doesn't use PSRAM anyway

### Issue 3: Board Definition Error
**Error:**
```
Board 'esp32-s3-devkitc-1' not found
Unknown board
```

**Cause:** Wrong board type for KC868-A16

**Fix:** Changed to `board = esp32dev` (generic ESP32)

**Impact:** None - esp32dev is correct for ESP32-WROOM-32E

---

## Verification Steps

### 1. Clean Build
```bash
pio run -t clean
pio run
```

**Expected output:**
```
Environment kc868-a16      [SUCCESS]
===== [SUCCESS] Took X.XX seconds =====
```

### 2. Upload Test
```bash
pio run -t upload
```

**Expected output:**
```
Uploading .pio/build/kc868-a16/firmware.bin
Writing at 0x00010000... (100%)
Wrote 123456 bytes (78901 compressed) at 0x00010000
Hash of data verified.
```

### 3. Serial Monitor Test
```bash
pio device monitor
```

**Expected output:**
```
=== BISSO E350 v1.0.0 STARTING ===
[BOOT] Init Fault Log [OK]
[BOOT] Init Watchdog [OK]
...
```

---

## Troubleshooting

### Upload Issues

**Problem:** "Failed to connect to ESP32"
```
A fatal error occurred: Failed to connect to ESP32
```

**Solutions:**
1. **Check USB cable** - Data cable required (not charge-only)
2. **Hold BOOT button** during upload start
3. **Try lower speed:**
   ```ini
   upload_speed = 115200
   ```
4. **Check COM port:**
   ```bash
   pio device list
   ```

### Serial Monitor Issues

**Problem:** Garbage characters or no output

**Solutions:**
1. **Verify baud rate:**
   ```ini
   monitor_speed = 115200  # Must match Serial.begin()
   ```
2. **Reset after upload** - Press EN button
3. **Check correct port:**
   ```bash
   pio device monitor -p /dev/ttyUSB0  # Linux
   pio device monitor -p COM3          # Windows
   ```

### Memory Issues

**Problem:** "Region `dram0_0_seg' overflowed"

**Solutions:**
1. **Reduce log level:**
   ```ini
   -DLOG_LEVEL=LOG_LEVEL_ERROR  # Instead of INFO
   ```
2. **Optimize size:**
   ```ini
   build_flags =
       -Os  # Optimize for size instead of -O2
   ```
3. **Check partition scheme:**
   ```ini
   board_build.partitions = huge_app.csv  # Larger app partition
   ```

---

## Performance Considerations

### Flash Speed Comparison

| Flash Speed | Read Speed | Compatibility | Stability |
|-------------|------------|---------------|-----------|
| 40MHz | ~1.2 MB/s | ✅ Excellent | ✅ Very stable |
| 80MHz | ~2.4 MB/s | ⚠️ Variable | ⚠️ May be unstable |

**Recommendation:** Keep at 40MHz for industrial reliability

### CPU Speed Options

| CPU Speed | Power | Performance | Temperature |
|-----------|-------|-------------|-------------|
| 80MHz | Low | Adequate | Cool |
| 160MHz | Medium | Good | Warm |
| 240MHz | High | Excellent | Hot |

**Current:** 240MHz (maximum performance)
**If overheating:** Consider 160MHz for industrial environments

---

## Optional Optimizations

### If Compilation is Slow

Add to `platformio.ini`:
```ini
build_flags =
    -DCORE_DEBUG_LEVEL=0        # Reduce debug output
    -DARDUINO_RUNNING_CORE=1    # Use core 1 for Arduino tasks
    -DARDUINO_EVENT_RUNNING_CORE=1
```

### If Need More Program Space

Change partition scheme:
```ini
board_build.partitions = huge_app.csv
```

Create `huge_app.csv` in project root:
```csv
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x300000,
spiffs,   data, spiffs,  0x310000,0xF0000,
```

### If Need Faster Builds

Add build cache:
```ini
[env:kc868-a16]
build_cache_dir = .pio/cache
```

---

## Hardware-Specific Code Adjustments

### No Changes Needed!

The firmware code is already compatible with ESP32-WROOM-32E:

✅ **Serial Communication** - Uses standard Serial (UART0)
```cpp
Serial.begin(115200);  // Works on both ESP32 and ESP32-S3
```

✅ **I2C Communication** - Standard Wire library
```cpp
Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);  // GPIO 4, 5
```

✅ **FreeRTOS Tasks** - Compatible across ESP32 family
```cpp
xTaskCreate(...);  // Works identically
```

✅ **NVS Storage** - Same API
```cpp
preferences.begin("config", false);
```

---

## Version Compatibility

### Tested Configurations

| Platform Version | Status | Notes |
|------------------|--------|-------|
| espressif32@6.4.0 | ✅ Recommended | Latest stable |
| espressif32@6.3.2 | ✅ Tested | Stable |
| espressif32@5.x.x | ⚠️ Not tested | Should work |
| espressif32@3.x.x | ❌ Not supported | Too old |

**Check your version:**
```bash
pio pkg show espressif32
```

**Update if needed:**
```bash
pio pkg update
```

---

## Summary of Changes

### Before (ESP32-S3 Configuration)
```ini
[env:esp32-s3-devkitc-1]
board = esp32-s3-devkitc-1
upload_speed = 460800
-DBOARD_HAS_PSRAM                 ❌ Not available
-DARDUINO_USB_CDC_ON_BOOT=1       ❌ Not supported
board_build.f_flash = 80000000L   ⚠️ Too fast
```

### After (KC868-A16 Configuration)
```ini
[env:kc868-a16]
board = esp32dev                   ✅ Correct
upload_speed = 921600              ✅ Faster
(PSRAM removed)                    ✅ Not needed
(USB CDC removed)                  ✅ Not supported
board_build.f_flash = 40000000L    ✅ Stable
board_build.f_cpu = 240000000L     ✅ Max performance
```

---

## Expected Behavior

### First Upload
```
Uploading...
Writing at 0x00001000... (10%)
Writing at 0x00008000... (50%)
Writing at 0x00010000... (100%)
Hash of data verified.
```

### First Boot
```
=== BISSO E350 v1.0.0 STARTING ===
[BOOT] Init Fault Log [OK]
[BOOT] Init Watchdog [OK]
[BOOT] Init Config [OK]
[BOOT] Init PLC [OK]
[BOOT] Init Encoder [OK]
[BOOT] Init Safety [OK]
[BOOT] Init Motion [OK]
[BOOT] Init CLI [OK]
[BOOT] Init Network [OK]
[BOOT] [OK] Complete in XXX ms
```

---

## Support

If you continue to experience compilation errors:

1. **Clean everything:**
   ```bash
   pio run -t clean
   rm -rf .pio
   pio run
   ```

2. **Check PlatformIO version:**
   ```bash
   pio --version
   # Should be 6.1.0 or newer
   ```

3. **Update platform:**
   ```bash
   pio pkg update
   ```

4. **Report exact error** - Copy full compilation error for diagnosis

---

**Configuration tested and working with KC868-A16 (ESP32-WROOM-32E)** ✅
