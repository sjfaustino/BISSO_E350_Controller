#ifndef ENCODER_WJ66_H
#define ENCODER_WJ66_H

#include <Arduino.h>
#include <stdint.h>

// --- CONFIGURATION ---
#define WJ66_AXES 4             // <--- FIX: Defined here
#define WJ66_BAUD 9600
#define WJ66_READ_INTERVAL_MS 50
#define WJ66_TIMEOUT_MS 500

typedef enum {
    ENCODER_OK = 0,
    ENCODER_TIMEOUT = 1,
    ENCODER_CRC_ERROR = 2,
    ENCODER_ERROR = 3
} encoder_status_t;

void wj66Init();
// Note: wj66Update() removed - polling is now handled by RS-485 registry

// Accessors
int32_t wj66GetPosition(uint8_t axis);
encoder_status_t wj66GetStatus();
uint32_t wj66GetAxisAge(uint8_t axis);
bool wj66IsStale(uint8_t axis);

// Commands
void wj66Reset(); 
void wj66SetZero(uint8_t axis); 
bool wj66SetBaud(uint32_t baud);
uint32_t wj66Autodetect(); 

void wj66Diagnostics();

#endif
