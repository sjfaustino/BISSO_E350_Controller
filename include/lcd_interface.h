#ifndef LCD_INTERFACE_H
#define LCD_INTERFACE_H

#include <Arduino.h>

#define LCD_COLS 20
#define LCD_ROWS 4
#define LCD_I2C_ADDR 0x27
// FIX: Use the constant from system_constants.h if available, otherwise 100ms.
#define LCD_UPDATE_INTERVAL_MS 100 

typedef enum {
  LCD_MODE_I2C = 0,
  LCD_MODE_SERIAL = 1,
  LCD_MODE_NONE = 2
} lcd_mode_t;

void lcdInterfaceInit();
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