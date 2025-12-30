# BISSO E350 Motion Controller Firmware

**Version:** Gemini v1.0.0  
**Status:** Production Ready

---

## Overview

The BISSO E350 Motion Controller Firmware is a custom embedded solution designed to upgrade and replace obsolete control systems in industrial bridge saw equipment. It leverages ESP32 microcontrollers to manage motion control, safety, and human-machine interfaces.

### Key Features

- ✅ **Multi-axis motion control** (X, Y, Z, A) via single VFD
- ✅ **Web-based dashboard** for real-time monitoring
- ✅ **Command Line Interface (CLI)** for diagnostics
- ✅ **Encoder feedback** via WJ66 DRO reader
- ✅ **Modbus integration** with Altivar 31 VFD
- ✅ **Fault logging** with NVS persistence
- ✅ **OTA firmware updates**

---

## Documentation

| Guide | Audience | Description |
|:------|:---------|:------------|
| 📘 **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)** | Developers | Complete technical reference: architecture, APIs, code style, testing |
| 📗 **[OPERATOR_GUIDE.md](OPERATOR_GUIDE.md)** | Operators | Step-by-step operation manual: startup, calibration, troubleshooting |

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
| **Controller** | KC868-A16 | ESP32 industrial controller |
| **VFD** | Schneider Altivar 31 | Motor frequency control |
| **Encoders** | WJ66 DRO Reader | 4-axis position feedback |
| **I/O Expanders** | PCF8574 (x4) | 2 input + 2 output expanders |

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

**Last Updated:** December 2025
