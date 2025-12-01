#ifndef ENCODER_WJ66_H
#define ENCODER_WJ66_H

#include <Arduino.h>

#define WJ66_BAUD 9600
#define WJ66_AXES 4
#define WJ66_TIMEOUT_MS 1000
#define WJ66_READ_INTERVAL_MS 50

typedef enum {
  ENCODER_OK = 0,
  ENCODER_TIMEOUT = 1,
  ENCODER_CRC_ERROR = 2,
  ENCODER_NOT_FOUND = 3
} encoder_status_t;

void wj66Init();
void wj66Update();
int32_t wj66GetPosition(uint8_t axis);
uint32_t wj66GetAxisAge(uint8_t axis);
bool wj66IsStale(uint8_t axis);
encoder_status_t wj66GetStatus();
void wj66Reset();
void wj66Diagnostics();
void wj66PrintStatus();

#endif