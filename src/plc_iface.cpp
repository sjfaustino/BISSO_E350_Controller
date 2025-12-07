/**
 * @file plc_iface.cpp
 * @brief Implementation of I2C Communication for KC868
 */

#include "plc_iface.h"
#include <Wire.h>

static uint8_t q73_shadow_register = 0xFF;

void elboInit() {
    Wire.beginTransmission(ADDR_Q73_OUTPUT);
    Wire.write(0xFF);
    Wire.endTransmission();
    q73_shadow_register = 0xFF;

    Wire.beginTransmission(ADDR_I73_INPUT);
    Wire.write(0xFF);
    Wire.endTransmission();
}

bool elboI73GetInput(uint8_t bit) {
    if (bit > 7) return false;
    if (Wire.requestFrom(ADDR_I73_INPUT, 1) > 0) {
        uint8_t state = Wire.read();
        return (state & (1 << bit)) != 0; 
    }
    return false;
}

void elboQ73SetRelay(uint8_t bit, bool state) {
    if (bit > 7) return;
    if (state) q73_shadow_register &= ~(1 << bit); 
    else q73_shadow_register |= (1 << bit);
    Wire.beginTransmission(ADDR_Q73_OUTPUT);
    Wire.write(q73_shadow_register);
    Wire.endTransmission();
}

bool elboQ73GetConsenso(uint8_t bit) {
    return elboI73GetInput(bit);
}

// --- MISSING FUNCTIONS IMPLEMENTED HERE ---

void elboSetSpeedProfile(uint8_t profile_idx) {
    // Clear old speed bits (4,5,6) -> Set them HIGH (OFF)
    q73_shadow_register |= ((1<<ELBO_Q73_SPEED_1)|(1<<ELBO_Q73_SPEED_2)|(1<<ELBO_Q73_SPEED_3));
    
    // Enable new speed bit -> Set LOW (ON)
    switch (profile_idx) {
        case 0: q73_shadow_register &= ~(1 << ELBO_Q73_SPEED_1); break;
        case 1: q73_shadow_register &= ~(1 << ELBO_Q73_SPEED_2); break;
        case 2: q73_shadow_register &= ~(1 << ELBO_Q73_SPEED_3); break;
        default: q73_shadow_register &= ~(1 << ELBO_Q73_SPEED_1); break;
    }
    
    Wire.beginTransmission(ADDR_Q73_OUTPUT);
    Wire.write(q73_shadow_register);
    Wire.endTransmission();
}

void elboSetDirection(uint8_t axis, bool forward) {
    uint8_t relay_bit = 255;
    switch(axis) {
        case 0: relay_bit = ELBO_Q73_DIR_X; break;
        case 1: relay_bit = ELBO_Q73_DIR_Y; break;
        case 2: relay_bit = ELBO_Q73_DIR_Z; break;
        case 3: relay_bit = ELBO_Q73_DIR_A; break;
    }
    if (relay_bit > 7) return;

    if (forward) q73_shadow_register &= ~(1 << relay_bit);
    else q73_shadow_register |= (1 << relay_bit);

    // Ensure Enable is ON
    q73_shadow_register &= ~(1 << ELBO_Q73_ENABLE);

    Wire.beginTransmission(ADDR_Q73_OUTPUT);
    Wire.write(q73_shadow_register);
    Wire.endTransmission();
}

void elboDiagnostics() {
    Serial.println("\n[PLC] Diagnostics");
    Serial.printf("  Q73 Output: 0x%02X\n", q73_shadow_register);
    
    Wire.beginTransmission(ADDR_I73_INPUT);
    if (Wire.endTransmission() == 0) {
        Wire.requestFrom(ADDR_I73_INPUT, 1);
        Serial.printf("  I73 Input:  0x%02X\n", Wire.read());
    } else {
        Serial.println("  I73 Input:  ERROR");
    }
}