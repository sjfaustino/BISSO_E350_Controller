#ifndef PLC_IFACE_H
#define PLC_IFACE_H

#include <Arduino.h>

#define PLC_I2C_ADDR 0x20
#define PLC_I2C_SPEED 100000
#define PLC_SDA_PIN 4
#define PLC_SCL_PIN 5
#define PLC_READ_INTERVAL_MS 50
#define PLC_READ_TIMEOUT_MS 1000

typedef enum {
  PLC_OK = 0,
  PLC_TIMEOUT = 1,
  PLC_NOT_FOUND = 2,
  PLC_CRC_ERROR = 3,
  PLC_INVALID_DATA = 4
} plc_status_t;

void plcIfaceInit();
void plcIfaceUpdate();
bool plcGetBit(uint8_t bit);
void plcSetBit(uint8_t bit, bool value);
uint8_t plcGetByte(uint8_t offset);
void plcSetByte(uint8_t offset, uint8_t value);
uint16_t plcGetWord(uint8_t offset);
void plcSetWord(uint8_t offset, uint16_t value);
plc_status_t plcGetStatus();
uint32_t plcGetLastReadTime();
uint32_t plcGetErrorCount();
void plcDiagnostics();

#endif
