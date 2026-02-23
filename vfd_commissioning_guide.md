# Altivar 31 VFD Commissioning Guide (Digital Override)

This guide outlines the keypad settings required to support the "Digital Override" architecture using RS485/Modbus speed control on the BISSO E350 Controller.

## 1. Safety Prerequisites
- Ensure the motor is disconnected from the load if possible during initial testing.
- Verify motor nameplate data (Voltage, Frequency, Current).
- Ensure E-Stop is functional.

## 2. Menu Settings (Keypad)

### Modbus Addressing
- **VFD 1 (X Axis)**: Address `2` (Default)
- **VFD 2 (Y/Z/A Axis)**: Address `4` (Default)

### CTL (Control) Menu
- **LAC**: `L3` (Advanced Function Level)
- **FR1**: `nnd` (Modbus control)
- **CHCF**: `SEP` (Separate control/frequency reference)
- **CD1**: `nnd` (Modbus command)
- **CCS**: `CD1` (Command channel 1)
- **Fr1**: `nnd` (Modbus - Frequency reference comes from RS485)
- **CHCF**: `SEP` (Separate - allows Modbus control for speed but PLC/Hardware for Run/Stop)
- **Cd1**: `tEr` (Terminal - Start/Stop commands via PLC terminal logic)

### SET (Settings) Menu
- **ACC**: `2.0` (Acceleration - 2 seconds)
- **dEC**: `1.5` (Deceleration - 1.5 seconds)
- **LSP**: `0.0` (Low Speed Frequency)
- **HSP**: `105.0` (High Speed Frequency - **CAUTION**: Verify motor/bearing rating for >60Hz)

### COM (Communication) Menu
- **Add**: `1` (for X Axis) or `2` (for YZA Axis bundle)
- **tbr**: `19.2` (19200 Baud)
- **tFp**: `8E1` (8 data bits, Even parity, 1 stop bit)
- **tCt**: `0.1` (Modbus timeout in seconds)

## 3. Modbus Priority Behavior
In **AUTO** mode, the VFD ignores the analog potentiometer and PLC 3-speed profiles, strictly following the Modbus frequency sent by PosiPro.
In **MANUAL** mode, the PLC hardware logic takes precedence for safety and jogging.

## 4. Verification Check
From the controller CLI, run:
`vfd status`
Ensure both VFDs show "Alive" and no Modbus timeouts are occurring.
