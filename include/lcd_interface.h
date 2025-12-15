#ifndef LCD_INTERFACE_H
#define LCD_INTERFACE_H

#include <Arduino.h>

#define LCD_COLS 20
#define LCD_ROWS 4
#define LCD_I2C_ADDR 0x27
// PHASE 3.1: Reduced from 100ms to 20ms to match task period and encoder update frequency
// Synchronizes LCD updates with position data from encoder task
#define LCD_UPDATE_INTERVAL_MS 20 

typedef enum {
  LCD_MODE_I2C = 0,
  LCD_MODE_SERIAL = 1,
  LCD_MODE_NONE = 2
} lcd_mode_t;

void lcdInterfaceInit();
void lcdInterfaceCleanup();  // Clean up allocated resources
void lcdInterfaceUpdate();
void lcdInterfacePrintLine(uint8_t line, const char* text);
void lcdInterfaceSetMode(lcd_mode_t mode);
lcd_mode_t lcdInterfaceGetMode();
void lcdInterfaceClear();
void lcdInterfaceBacklight(bool on);
void lcdInterfaceDiagnostics();

// FIX: New function for 10Hz axial position updates
void lcdInterfacePrintAxes(int32_t x_counts, int32_t y_counts, int32_t z_counts, int32_t a_counts);

#endif