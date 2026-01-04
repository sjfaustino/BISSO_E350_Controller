#ifndef BOARD_INPUTS_H
#define BOARD_INPUTS_H

#include <Arduino.h>

// KC868-A16 Digital Input Expander 2 (PCF8574)
// E-Stop, Pause, Resume buttons are on I2C address 0x22
#define BOARD_INPUT_I2C_ADDR 0x22

// Note: Specific Pin Definitions (P3/P4/P5) have been moved to
// the Hardware Abstraction Layer (hardware_config.cpp/h)
// and are resolved dynamically at runtime.

// Return structure for button states
typedef struct {
  bool estop_active;   // True if E-Stop is ACTIVE (Circuit OPEN/Pressed)
  bool pause_pressed;  // True if Pause button is PRESSED
  bool resume_pressed; // True if Resume button is PRESSED
  bool connection_ok;  // True if I2C communication succeeded
} button_state_t;

// Initialization function (Resolves HAL mappings)
void boardInputsInit();

// Poll inputs and return parsed state (Fast Path)
button_state_t boardInputsUpdate();

// Print diagnostic status to Serial
void boardInputsDiagnostics();

// Raw state getter
uint8_t boardInputsGetRawState();

#endif // BOARD_INPUTS_H
