# BISSO E350 Motion Controller Firmware

**Version:** See `include/firmware_version.h`  
**Status:** Production Ready

---

## Overview

The BISSO E350 Motion Controller Firmware is a custom embedded solution designed to **replace a broken ELBO positioning controller** on the BISSO E350 stone bridge saw. It uses an ESP32-based controller (KC868-A16) to manage position control, user interface, and monitoring while interfacing with the original Siemens S5 PLC.

### What is the BISSO E350?

The BISSO E350 is an industrial **stone bridge saw** used for cutting:
- Marble
- Granite
- Limestone
- Artificial stone (engineered quartz)

### Key Features

- ✅ **3-axis position control** (X, Y, Z) via PLC interface
- ✅ **Web-based dashboard** for real-time monitoring
- ✅ **Command Line Interface (CLI)** for diagnostics
- ✅ **Encoder feedback** via WJ66 serial converters (RS-232)
- ✅ **Modbus monitoring** of Altivar 31 VFD (read-only)
- ✅ **Spindle current monitoring** via JXK-10 sensor
- ✅ **Fault logging** with NVS persistence
- ✅ **GitHub-Based OTA Updates** (auto-check, one-click install)
- ✅ **RS485 Auto-detection** for VFD and current monitor

---

## Documentation

| Guide | Audience | Description |
|:------|:---------|:------------|
| 📘 **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)** | Developers | Complete technical reference: architecture, APIs, code style, testing |
| 📗 **[OPERATOR_GUIDE.md](OPERATOR_GUIDE.md)** | Operators | Step-by-step operation manual: startup, calibration, troubleshooting |
| 📙 **[docs/HARDWARE_REFERENCE.md](docs/HARDWARE_REFERENCE.md)** | Technicians | Complete hardware documentation: signals, wiring, VFDs, sensors |
| 📕 **[docs/CLI_REFERENCE.md](docs/CLI_REFERENCE.md)** | All | CLI command reference |

---

## Quick Start

### For Developers

```bash
# Clone and build
git clone <repository-url>
cd BISSO_E350_Controller
pio run

# Upload firmware
pio run -t upload

# Upload web assets
pio run -t uploadfs

# Monitor serial
pio device monitor -b 115200
```

### For Operators

1. Power on the controller
2. Wait 30 seconds for boot
3. Navigate to `http://192.168.1.100/` in browser
4. See [OPERATOR_GUIDE.md](OPERATOR_GUIDE.md) for detailed instructions

---

## Hardware Requirements

| Component | Model | Purpose |
|:----------|:------|:--------|
| **Controller** | KC868-A16 v1.6 | ESP32-WROOM-32E industrial controller |
| **Axis VFD** | Schneider Altivar 31 | Shared VFD for X/Y/Z axis motors |
| **Spindle VFD** | Control Techniques Unidrive SP | 22kW saw blade motor |
| **Encoders** | WJ66 DRO Reader (RS-232) | 4-axis position feedback |
| **Current Sensor** | JXK-10 (RS-485) | Spindle current monitoring |
| **I/O Expanders** | PCF8574 (x4) | ESP32 ↔ PLC interface |
| **PLC** | Siemens S5 (original) | Contactor control, safety interlocks |

---

## Safety Warning

⚠️ **CRITICAL**: This controller manages industrial machinery with moving parts.

- **Primary safety**: Hardware E-Stop button (red mushroom switch)
- **Software E-Stop**: Secondary safety layer only
- **Network**: Local network only - never expose to internet
- **Change default passwords immediately after deployment**

See [OPERATOR_GUIDE.md](OPERATOR_GUIDE.md) for complete safety procedures.

---

## License

Proprietary - BISSO Industrial Controls

---

**Last Updated:** January 11, 2026
