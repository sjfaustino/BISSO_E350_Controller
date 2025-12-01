#ifndef BOARD_INPUTS_H
#define BOARD_INPUTS_H

#include <Arduino.h>

// KC868-A16 Digital Input Expander (PCF8574)
// Inputs X1-X8 are on I2C address 0x24
#define BOARD_INPUT_I2C_ADDR  0x24

// Bit definitions for X1-X8 (PCF8574 P0-P7)
// Mapping: X1=P0, X2=P1, X3=P2, X4=P3, X5=P4, X6=P5, X7=P6, X8=P7
// Specific Wiring for Gemini v1.0.0:
// E-Stop (NC Button): X4 (P3)
// Pause (NO Button):  X5 (P4)
// Resume (NO Button): X6 (P5)

#define INPUT_BIT_ESTOP       3 // P3 (X4)
#define INPUT_BIT_PAUSE       4 // P4 (X5)
#define INPUT_BIT_RESUME      5 // P5 (X6)

// Return structure for button states
typedef struct {
    bool estop_active;    // True if E-Stop is ACTIVE (Circuit OPEN/Pressed)
    bool pause_pressed;   // True if Pause button is PRESSED
    bool resume_pressed;  // True if Resume button is PRESSED
    bool connection_ok;   // True if I2C communication succeeded
} button_state_t;

// Initialization function
void boardInputsInit();

// Poll inputs and return parsed state
button_state_t boardInputsUpdate();

// Print diagnostic status to Serial
void boardInputsDiagnostics();

#endif // BOARD_INPUTS_H