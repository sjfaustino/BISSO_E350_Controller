# BISSO E350 Motion Controller Firmware: Gemini v1.0.0

The BISSO E350 Motion Controller Firmware is a custom embedded solution designed to upgrade and replace obsolete control systems in industrial bridge saw equipment. It leverages modern, high-speed microcontrollers to manage motion control, safety, and human-machine interfaces.

**Current Version:** Gemini v1.0.0 (Base functionality complete)
**Project Codename:** Gemini

***

## 1. Project Overview and Core Function

The primary purpose of this firmware is to manage the **single VFD (Variable Frequency Drive) motor** responsible for all primary motion axes (X, Y, Z, and rotational A). The system achieves closed-loop control by integrating high-speed communication with the machine's external I/O.

### 1.1 Motion Control Strategy: PLC I/O Emulation

This system operates as a **"smart I/O bridge"**, translating high-level digital commands into the low-level binary signals that the machine's legacy PLC (Programmable Logic Controller) expects.

1.  **High-Level Command:** The system receives commands (e.g., `move X 100.5 50.0`) via the Command Line Interface (CLI) or Web interface.
2.  **Actuation (Output):** The ESP32-S3 communicates with two **PCF8574 I²C I/O expanders** (at addresses `0x20` and `0x21`). These expanders output the specific binary signals (Axis Select, Direction, Speed Profile) to the industrial machine's original PLC input panel, effectively emulating the legacy PLC's control signals.
3.  **Feedback (Input/Positional Truth):** Motion position is tracked in real-time by querying the **Wayjun WJ66 Digital Readout (DRO) reader** over a dedicated serial port. This WJ66 unit consolidates positional data from **four separate external optical encoders** (one for each axis), serving as the authoritative source of truth for the closed-loop system.

***

## 2. Hardware and I/O Specification

The firmware is strictly optimized for the following components:

### 2.1 Control Board

| Specification | Detail | Note |
| :--- | :--- | :--- |
| **Model** | **KC868-A16 Industrial Controller** | This is the specific ESP32-S3 board used. The pin database and I/O count are based on this model. |
| **Microcontroller** | ESP32-S3 | Dual-core, highly capable for concurrent tasks. |
| **I/O Count** | 16 Opto-isolated Inputs (X1-X16), 16 Relay Outputs (Y1-Y16) | The core safety and consensus signals are read/written through these pins or the I²C bus. |

### 2.2 Communication Interfaces

| Interface | Component/Protocol | Purpose | Firmware Modules |
| :--- | :--- | :--- | :--- |
| **I²C Bus** | PCF8574 I/O Expanders | Output of single-bit control signals: Axis Select, Direction (+/-), and Speed Profiles (SLOW/MED/FAST). | `plc_iface.cpp`, `i2c_bus_recovery.cpp` |
| **Serial (UART)** | Wayjun WJ66 DRO Reader | Input of real-time, consolidated 4-axis position data (pulses/counts) from the external optical encoders. | `encoder_wj66.cpp`, `tasks_encoder.cpp` |
| **Non-Volatile Storage (NVS)** | Internal Flash Memory | Persistence for all **Calibration Data** (PPM, speeds) and **Fault History**. | `config_unified.cpp`, `fault_logging.cpp` |

***

## 3. Software Architecture (Developer Deep Dive)

The system utilizes FreeRTOS for multi-tasking, enforced by a structured Task Manager.

### 3.1 FreeRTOS Task Structure

| Task Name | Execution Period | Priority | Core | Primary Function |
| :--- | :--- | :--- | :--- | :--- |
| **Safety** (`tasks_safety.cpp`) | 5 ms | P24 (CRITICAL) | Core 1 | Enforces E-Stop, monitors motion stalls, and processes safety alarms. |
| **Motion** (`tasks_motion.cpp`) | 10 ms | P22 (HIGH) | Core 1 | Executes moves, manages the internal axis state machine, and translates commands to PLC I/O. |
| **Encoder** (`tasks_encoder.cpp`) | 20 ms | P20 | Core 1 | High-speed polling of the **WJ66 DRO** and updates the encoder-motion integration layer. |
| **PLC_Comm** (`tasks_plc.cpp`) | 50 ms | P18 | Core 1 | Low-frequency I²C writes/reads to external I/O expanders for stability. |
| **Monitor** (`tasks_monitor.cpp`) | 1000 ms | P12 | Core 1 | Background check of memory usage, task execution times, and periodic config saving. |

### 3.2 Contextual Fault Logging API

All system errors are routed through the central, thread-safe `faultLogEntry` function. This provides **critical diagnostic context** that is persisted in NVS:

```c
void faultLogEntry(fault_severity_t severity, fault_code_t code, int32_t axis, int32_t value, const char* format, ...);
````

  * **`severity`**: Determines the required system action (e.g., `FAULT_CRITICAL` triggers an E-Stop).
  * **`axis`**: The axis index affected (0-3) or -1 for system-wide faults.
  * **`value`**: The raw metric associated with the fault (e.g., memory threshold, I²C address, encoder count deviation).
  * **`format, ...`**: A `printf`-style message string for detailed, human-readable description.

-----

## 4\. Command Line Interface (CLI) Reference

The CLI is the primary debugging, configuration, and maintenance interface.

### 4.1 Global and Utility Commands

| Command | Usage | Description | Example Output |
| :--- | :--- | :--- | :--- |
| **`help`** | `help` | Displays a summary of all registered CLI commands. | `...move - Move axes...` |
| **`info`** | `info` | Displays critical system and software status. | `Firmware: Gemini v1.0.0` |
| **`reset`** | `reset` | Executes a software-triggered reboot of the entire system. | |
| **`faults`** | `faults` | Displays the history of all logged Warnings, Errors, and Critical faults persisted in NVS. | `FAULT [2]: I2C transaction failed...` |
| **`faults_clear`**| `faults_clear` | Clears the NVS-stored fault history. | |
| **`debug`** | `debug` | Runs a comprehensive system diagnostic dump (Boot, Motion, Encoder, PLC, Memory). | |

### 4.2 Motion Control Commands

| Command | Usage | Description | Example Usage |
| :--- | :--- | :--- | :--- |
| **`motion`** | `motion` | Displays the current motion subsystem status, active axis, and state. | `motion` |
| **`move`** | `move [AXIS] [POS_MM] [SPEED_MM/S]` | Executes an absolute move command for a single axis. | `move X 100.5 50.0` |
| **`stop`** | `stop` | Commands a controlled stop (deceleration) of any active motion. | `stop` |
| **`limits`** | `limits [AXIS] [MIN_MM] [MAX_MM]` | Sets the soft limits (safety travel boundaries) for a specified axis. | `limits Y -10.0 500.0` |

### 4.3 Calibration Commands

Calibration is **critical** for accurate motion. These commands interact with the NVS-stored `MachineCalibration` struct.

| Command | Usage | Description | Example Usage |
| :--- | :--- | :--- | :--- |
| **`calibrate ppmm`**| `calibrate ppmm [AXIS] [DISTANCE_MM]` | **Manual PPM start.** Initiates a manual Pulses-Per-Millimeter (PPM) measurement. Prompts the operator to move the axis physically. | `calibrate ppmm Z 50.0` |
| **`calibrate ppmm end`**| `calibrate ppmm end` | **Manual PPM finish.** Calculates and saves the new PPM value based on the encoder difference detected since the start command. | `calibrate ppmm end` |
| **`calibrate ppmm X reset`**| `calibrate ppmm [AXIS] reset` | Resets the PPM/PPD calibration for the specified axis to the factory default scale factor (`1000` pulses/unit). | `calibrate ppmm X reset` |
| **`calibrate speed`**| `calibrate speed [AXIS] [PROFILE] [DISTANCE_MM]` | **Auto Speed Calibration.** Measures the true speed achieved by the VFD at a specific profile setting (SLOW, MEDIUM, or FAST) and saves the result in NVS. | `calibrate speed Y FAST 500.0` |
| **`calibrate speed X reset`**| `calibrate speed [AXIS] reset` | Resets the speed profiles (SLOW, MEDIUM, FAST) for the specified axis to machine defaults. | `calibrate speed X reset` |
