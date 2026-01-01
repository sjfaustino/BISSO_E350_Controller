# BISSO E350 Controller - Hardware Reference

**Version:** 1.1.0  
**Last Updated:** 2026-01-01  
**Machine:** BISSO E350 Stone Bridge Saw

---

## Table of Contents

1. [Machine Overview](#machine-overview)
2. [Control System Architecture](#control-system-architecture)
3. [ESP32 ↔ PLC Interface](#esp32--plc-interface)
4. [Axis Configuration](#axis-configuration)
5. [Spindle System](#spindle-system)
6. [VFD Configuration](#vfd-configuration)
7. [Sensors and Feedback](#sensors-and-feedback)
8. [Safety Systems](#safety-systems)
9. [I/O Configuration](#io-configuration)
10. [Communication Interfaces](#communication-interfaces)

---

## Machine Overview

The **BISSO E350** is an industrial stone bridge saw used for cutting:
- Marble
- Granite
- Limestone
- Artificial stone (engineered quartz, etc.)

### Physical Layout

The saw blade and spindle motor are mounted on a **carriage** that moves via three motorized axes:
- **X Axis**: Cross travel (left/right)
- **Y Axis**: Bridge travel (forward/back along rails)
- **Z Axis**: Vertical travel (blade up/down)
- **A Axis**: Manual angle adjustment (not motorized)

The **spindle motor** (22kW) only rotates the saw blade. All positioning is done by the X, Y, Z axis motors.

---

## Control System Architecture

### Overview

The ESP32 controller **replaces a broken ELBO positioning controller**. It interfaces with the original **Siemens S5 PLC** which remains in the system to handle:
- Contactor switching (axis motor selection)
- VFD control (speed reference)
- Safety interlocks

```
┌────────────────────────────────────────────────────────────────────────┐
│                         BISSO E350 Control System                       │
├────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────┐          ┌─────────────────────┐             │
│  │   ESP32 Controller  │◄────────►│   Siemens S5 PLC    │             │
│  │   (KC868-A16 v1.5)  │  PCF8574 │   (Original)        │             │
│  │                     │  I/O     │                     │             │
│  │  • Position control │          │  • Contactor control│             │
│  │  • User interface   │          │  • VFD speed ref    │             │
│  │  • Encoder reading  │          │  • Safety interlocks│             │
│  └──────────┬──────────┘          └──────────┬──────────┘             │
│             │                                 │                        │
│     ┌───────┴───────┐              ┌─────────┴─────────┐              │
│     │   RS-232      │              │   Contactors      │              │
│     │   WJ66 x4     │              │   (X/Y/Z Select)  │              │
│     │   (Encoders)  │              └─────────┬─────────┘              │
│     └───────────────┘                        │                        │
│                                      ┌───────▼───────┐                │
│  ┌─────────────────────┐            │  Altivar 31   │                │
│  │   RS-485 Bus        │            │  (Shared VFD) │                │
│  │  • JXK-10 (current) │            └───────┬───────┘                │
│  │  • Altivar 31 (mon) │                    │                        │
│  └─────────────────────┘         ┌──────────┼──────────┐             │
│                                  ▼          ▼          ▼             │
│                            ┌─────────┐ ┌─────────┐ ┌─────────┐      │
│                            │ X Motor │ │ Y Motor │ │ Z Motor │      │
│                            └─────────┘ └─────────┘ └─────────┘      │
│                                                                       │
│  ┌──────────────────────────────────────────────────────────┐       │
│  │                    Spindle System                          │       │
│  │  Unidrive SP (VFD) ──► FIMET 22kW Motor ──► Saw Blade     │       │
│  │  JXK-10 (current monitor)                                  │       │
│  └──────────────────────────────────────────────────────────┘       │
│                                                                       │
└────────────────────────────────────────────────────────────────────────┘
```

### ESP32 Controller Board

| Parameter | Value |
|-----------|-------|
| Board | KC868-A16 v1.5 |
| MCU | ESP32-WROOM-32E |
| Role | Replaces broken ELBO positioning controller |
| Communication | RS-232 (encoders), RS-485 (Modbus), I2C, WiFi |

---

## ESP32 ↔ PLC Interface

The ESP32 communicates with the Siemens S5 PLC via **PCF8574 I/O expanders** (I2C), emulating the original ELBO controller signals.

> 📖 **See Also:** [ELBO_PLC_HANDSHAKE.md](ELBO_PLC_HANDSHAKE.md) for complete protocol details, timing diagrams, and safety notes.

### ESP32 → PLC Signals (Commands to PLC Inputs I72.x / I73.x)

| ESP32 Signal | PLC Input | Function |
|--------------|-----------|----------|
| AXIS_X_SELECT | I 73.1 | Request X axis (translation) |
| AXIS_Y_SELECT | I 73.0 | Request Y axis (feed/avanço) |
| AXIS_Z_SELECT | I 73.2 | Request Z axis (vertical) |
| AXIS_T_SELECT | I 73.3 | Request table rotation |
| DIR_PLUS | I 73.5 | Direction positive (forward/up/CW) |
| DIR_MINUS | I 73.6 | Direction negative (back/down/CCW) |
| SPEED_FAST | I 72.0 | Fast feed preset |
| SPEED_MEDIUM | I 72.1 | Medium feed preset |
| SPEED_SLOW | I 73.7 | Slow/fine feed (V/S - velocità/spianatura) |
| RUN | I 72.2 | Motion block active |
| LOCK | I 72.3 | Table unlock/clamp release |
| RESET | I 72.4 | PLC reset/handshake resync |

**Speed Modes:**
- **Fast** (I 72.0): Rapid positioning
- **Medium** (I 72.1): Normal cutting speed
- **Slow/V/S** (I 73.7): Very slow for leveling table or precision work

### PLC → ESP32 Signals (Status from PLC Outputs Q8.x)

| PLC Output | ESP32 Signal | Function |
|------------|--------------|----------|
| Q 8.0 | PLC_READY | System enabled, may command moves |
| Q 8.1 | PLC_ACK | Acknowledge pulse when PLC accepts move |
| Q 8.2 | PLC_INHIBIT | Safety inhibit (PLC refuses motion) |

**Handshake Cycle:**
1. ESP32 waits for `PLC_READY` (Q 8.0 = 1)
2. ESP32 asserts axis, direction, speed, and `RUN`
3. PLC pulses `PLC_ACK` (Q 8.1) when it energizes the contactor
4. ESP32 monitors encoder until target reached
5. ESP32 drops axis request and `RUN`
6. PLC drops contactor, remains in READY state

---

## Axis Configuration

### Summary

| Axis | Type | Motorized | Drive | Position Feedback | Purpose |
|------|------|-----------|-------|-------------------|----------|
| **X** | Linear | ✅ Yes | Shared VFD | Encoder + WJ66 | Material feed |
| **Y** | Linear | ✅ Yes | Shared VFD | Encoder + WJ66 | Cross travel |
| **Z** | Linear | ✅ Yes | Shared VFD | Encoder + WJ66 | Blade height |
| **A** | Rotary | ❌ No | Manual | Encoder + WJ66 | Angle adjustment |

### Axis Drive Architecture

The X, Y, Z axes share a **single Altivar 31 VFD** through a contactor-based switching system controlled by the **Siemens S5 PLC**.

```
┌─────────────────────────────────────────────────────────┐
│                                                          │
│   ┌──────────┐     ┌───────────────┐                    │
│   │ Siemens  │────▶│  Contactors   │                    │
│   │  S5 PLC  │     │  (Selector)   │                    │
│   └──────────┘     └───────┬───────┘                    │
│                            │                            │
│                    ┌───────▼───────┐                    │
│                    │  Altivar 31   │                    │
│                    │   (1x VFD)    │                    │
│                    └───────┬───────┘                    │
│                            │                            │
│         ┌──────────────────┼──────────────────┐        │
│         ▼                  ▼                  ▼        │
│   ┌──────────┐      ┌──────────┐      ┌──────────┐    │
│   │ X Motor  │      │ Y Motor  │      │ Z Motor  │    │
│   └──────────┘      └──────────┘      └──────────┘    │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

**Key Characteristics:**
- **Single VFD** shared by all 3 motorized axes
- **Sequential motion only** - one axis moves at a time
- **Contactor switching** selects which motor receives power
- **Siemens S5 PLC** controls axis selection logic

### Altivar 31 VFD

| Parameter | Value |
|-----------|-------|
| VFD Model | Altivar 31 (ATV31) |
| Quantity | 1 (shared) |
| Control | Via Siemens S5 PLC |
| Communication | Modbus RTU @ 9600 baud (if enabled) |

### A Axis (Non-Motorized)

The A axis is a **manual rotary axis** for angle adjustment:
- Position tracked via WJ66 encoder
- No motor drive connected
- Used for miter angle cutting setup

---

## Spindle System

### Saw Blade Motor

| Parameter | Value |
|-----------|-------|
| **Manufacturer** | FIMET |
| **Model** | M 180L4 |
| **Power** | 22 kW |
| **Speed** | 1460 RPM |
| **Poles** | 4 (50 Hz) |
| **Power Factor** | cos φ = 0.85 |
| **Current (Star/400V)** | 24.5 A |
| **Current (Delta/230V)** | 42 A |

### Spindle VFD

| Parameter | Value |
|-----------|-------|
| **Manufacturer** | Control Techniques |
| **Model** | Unidrive SP (UNI3403VTC) |
| **Power Rating** | 22 kW |
| **Input Voltage** | 380/480V 3-Phase |
| **Input Current** | 46.0 A |
| **Output Current** | 46.0 A |
| **Overload Rating** | 55.2 A for 60 seconds |
| **Software Version** | 03.01.07 |
| **Serial Number** | 3786340002 |
| **Control Interface** | Digital I/O (Start/Stop/Speed) |
| **Modbus** | Not currently used |

### Current Monitoring Thresholds

Based on the motor specifications, the following protection thresholds are configured:

| Threshold | Value | Rationale |
|-----------|-------|-----------|
| **Overcurrent** | 30.0 A | 122% of rated (24.5A), allows startup surge |
| **Stall Detection** | 25.0 A | ~102% of rated current |
| **Auto-Pause** | 25.0 A | Pause cutting if load exceeds threshold |
| **Tool Breakage** | 5.0 A drop | Sudden current drop detection |

---

## VFD Configuration

### Altivar 31 (Axis Motors)

The single Altivar 31 VFD is controlled by the **Siemens S5 PLC** via standard analog/digital connections. The **ESP32 monitors status via separate RS-485 Modbus RTU**.

| Parameter | Value |
|-----------|-------|
| VFD Model | Altivar 31 (ATV31) |
| Quantity | 1 (shared via contactors) |
| PLC Control | Standard I/O (analog speed ref, digital run/stop) |
| PLC → Motor Selection | Contactors |
| ESP32 Monitoring | RS-485 Modbus RTU (read-only) |
| Modbus Baud Rate | 9600 |

**PLC → VFD Connections (Standard):**
- Analog speed reference (0-10V or 4-20mA)
- Digital run/stop command
- Contactor control for X/Y/Z motor selection

**ESP32 Readable Registers (RS-485):**
| Register | Function |
|----------|----------|
| 0x0003 | Status Word |
| 0x0004 | Output Frequency |
| 0x0005 | Motor Current |
| 0x0006 | Motor Torque |

**Note:** PLC and ESP32 use separate interfaces to the VFD. No RS-485 between PLC and VFD.

### Unidrive SP (Spindle)

The Unidrive SP is controlled via digital I/O:
- **Start/Stop**: Digital output (via PLC or ESP32)
- **Speed Control**: Fixed speed (configured on VFD keypad)
- **Fault Input**: Digital input to ESP32

---

## Sensors and Feedback

### Encoder Interface (WJ66)

The WJ66 is an **encoder-to-serial converter** that reads encoder signals and outputs position data via serial interface.

| Parameter | Value |
|-----------|-------|
| Model | Wayjun WJ66 |
| Function | Encoder signal → Serial converter |
| Encoder Input | Quadrature (A/B/Z) or SSI |
| **Serial Output** | **RS-232 (in use)** |
| Available Variants | RS-232 only, RS-485 only |
| Quantity | 4 (one per axis) |
| Purpose | Position feedback for X, Y, Z, A axes |

**Note:** The WJ66 is available in RS-232 and RS-485 variants. This installation uses the **RS-232 only** version.

### Current Sensor (JXK-10)

Monitors spindle motor current via Hall effect sensor.

| Parameter | Value |
|-----------|-------|
| Model | JXK-10 |
| Type | Hall Effect Current Transducer |
| Range | AC 0-50A |
| Interface | RS-485 Modbus RTU |
| Baud Rate | 9600 |
| Default Address | 1 |
| PV Register | 0x000E |
| Address Register | 0x0004 |
| Baud Register | 0x0005 |

**Scaling:**
- Raw value ≤ 3000 → Divide by 100 (2 decimal places)
- Raw value > 3000 → Divide by 10 (1 decimal place)

**Example Commands:**
```
Read Current:  01 03 00 0E 00 01 E5 C9
Read Config:   01 03 00 04 00 02 85 CA
```

---

## Safety Systems

### Hardware E-Stop (Primary)

The **red mushroom button** on the operator panel is the primary safety device:
- **Cuts ALL motor power** immediately
- Independent of ESP32 and PLC
- Hardwired safety circuit

### Software E-Stop (Secondary)

The software E-Stop button connects to **ESP32 only**:
- Does NOT cut motor power directly
- Triggers software stop commands
- Secondary safety layer only

> ⚠️ **WARNING**: The software E-Stop is NOT a substitute for the hardware E-Stop. Always use the red mushroom button for emergencies.

---

## I/O Configuration

### PCF8574 I/O Expanders

The ESP32 uses PCF8574 I/O expanders to interface with the PLC and other I/O:

| Device | Address | Direction | Function |
|--------|---------|-----------|----------|
| PCF8574 OUT | 0x24 | ESP32 → PLC | Axis/Direction/Speed commands (Y1-Y8) |
| PCF8574 IN | 0x21 | PLC → ESP32 | Consenso signals, limits |
| PCF8574 IN2 | 0x20 | External → ESP32 | Software E-Stop, Door interlock |

### PCF8574 OUT Pin Mapping (ESP32 → PLC @ 0x24)

| Bit | Pin | Signal | Function |
|-----|-----|--------|----------|
| 0 | Y1 | AXIS_X_SELECT | Select X axis motor |
| 1 | Y2 | AXIS_Y_SELECT | Select Y axis motor |
| 2 | Y3 | AXIS_Z_SELECT | Select Z axis motor |
| 3 | Y4 | DIR_POSITIVE | Move in positive direction |
| 4 | Y5 | DIR_NEGATIVE | Move in negative direction |
| 5 | Y6 | SPEED_FAST | Fast/rapid speed |
| 6 | Y7 | SPEED_MEDIUM | Medium speed |
| 7 | Y8 | SPEED_SLOW | Very slow (V/S - leveling mode) |

> ⚠️ **Active-Low Logic**: Bit cleared (0) = signal ON, bit set (1) = signal OFF

### PCF8574 IN Pin Mapping (PLC → ESP32 @ 0x21)

| Bit | Pin | Signal | Function |
|-----|-----|--------|----------|
| 4 | X1 | X_AXIS_READY | X axis "consenso" (permission to move) |
| 5 | X2 | Y_AXIS_READY | Y axis "consenso" |
| 6 | X3 | Z_AXIS_READY | Z axis "consenso" |

### LCD Display

| Parameter | Value |
|-----------|-------|
| Type | Character LCD 20x4 |
| Interface | I2C |
| Address | 0x27 |
| Controller | HD44780 compatible |

---

## Communication Interfaces

### RS-232 (Encoder Interfaces)

The WJ66 encoder interfaces output position data via RS-232:

| Device | Function | Notes |
|--------|----------|-------|
| WJ66 #1 | X Axis Position | Encoder → Serial |
| WJ66 #2 | Y Axis Position | Encoder → Serial |
| WJ66 #3 | Z Axis Position | Encoder → Serial |
| WJ66 #4 | A Axis Position | Encoder → Serial |

### RS-485 Bus (Modbus RTU)

Shared RS-485 bus for Modbus devices:

| Parameter | Value |
|-----------|-------|
| RX Pin | GPIO 16 |
| TX Pin | GPIO 13 |

| Device | Address | Priority | Notes |
|--------|---------|----------|-------|
| JXK-10 Current | 1 | Medium | Spindle current sensor |
| Altivar 31 | TBD | Low | Read-only (status, current, freq) |

**Note:** Altivar 31 is controlled by Siemens S5 PLC via analog/digital I/O; ESP32 only reads status via RS-485.

### I2C Bus

| Parameter | Value |
|-----------|-------|
| SDA Pin | GPIO 4 |
| SCL Pin | GPIO 5 |
| Frequency | 100 kHz |

| Device | Address | Function |
|--------|---------|----------|
| PCF8574 IN | 0x21 | PLC Consenso Signals |
| PCF8574 IN2 | 0x22 | Digital Inputs (available) |
| PCF8574 OUT | 0x24 | PLC Control (axis/dir/speed) |
| PCF8574 OUT2 | 0x25 | Digital Outputs (available) |
| LCD Display | 0x27 | User Interface |

### Network

| Interface | Configuration |
|-----------|---------------|
| WiFi Mode | AP + Station |
| AP SSID | BISSO-E350-XXXX |
| Web Interface | Port 80 |
| Telnet CLI | Port 23 |
| OTA Updates | Enabled |

---

## Revision History

| Date | Version | Changes |
|------|---------|---------|
| 2026-01-01 | 1.0.0 | Initial documentation |
