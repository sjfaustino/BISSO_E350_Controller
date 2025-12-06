/**
 * @file plc_iface.h
 * @brief Interface for KC868-A16 PCF8574 I/O Expanders
 * @project Gemini v1.0.0
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

#ifndef PLC_IFACE_H
#define PLC_IFACE_H

#include <Arduino.h>
#include "system_constants.h" // Required for Pin Definitions

// ============================================================================
// KC868-A16 PCF8574 I2C Expanders (ELBO Bit Mapping)
// ============================================================================

// PCF8574 @ 0x20 - ELBO I72 OUTPUT (Speed Selection to PLC)
#define PCF8574_I72_ADDR 0x20      
#define ELBO_I72_FAST 0            
#define ELBO_I72_MED 1             

// PCF8574 @ 0x21 - ELBO I73 OUTPUT (Axis/Direction/Mode to PLC)
#define PCF8574_I73_ADDR 0x21      
#define ELBO_I73_AXIS_Y 0          
#define ELBO_I73_AXIS_X 1          
#define ELBO_I73_AXIS_Z 2          
#define ELBO_I73_RESERVED_3 3      
#define ELBO_I73_RESERVED_4 4      
#define ELBO_I73_DIRECTION_PLUS 5  
#define ELBO_I73_DIRECTION_MINUS 6 
#define ELBO_I73_V_S_MODE 7        

// PCF8574 @ 0x22 - ELBO Q73 INPUT (Consenso / Mode from PLC)
#define PCF8574_Q73_ADDR 0x22      
#define ELBO_Q73_CONSENSO_Y 0      
#define ELBO_Q73_CONSENSO_X 1      
#define ELBO_Q73_CONSENSO_Z 2      
#define ELBO_Q73_AUTO_MANUAL 3     

// I2C Configuration
// OPTIMIZATION: Increased to 400kHz (Fast Mode) to reduce bus latency
#define PLC_I2C_SPEED 400000 
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

// Generic bit/byte operations (Deprecated, kept for compatibility)
bool plcGetBit(uint8_t bit);
void plcSetBit(uint8_t bit, bool value);
uint8_t plcGetByte(uint8_t offset);
void plcSetByte(uint8_t offset, uint8_t value);
uint16_t plcGetWord(uint8_t offset);
void plcSetWord(uint8_t offset, uint16_t value);

// ELBO-specific bit operations
bool elboI72GetSpeed(uint8_t speed_bit);        
bool elboI72SetSpeed(uint8_t speed_bit, bool value);  
bool elboI73GetAxis(uint8_t axis_bit);          
bool elboI73SetAxis(uint8_t axis_bit, bool value);    
bool elboI73GetDirection(uint8_t dir_bit);      
bool elboI73SetDirection(uint8_t dir_bit, bool value); 
bool elboI73GetVSMode();                        
bool elboI73SetVSMode(bool value);              
bool elboQ73GetConsenso(uint8_t axis);          
bool elboQ73GetAutoManual();                    

// --- NEW: Atomic Batch Operations ---
// Allows modifying multiple bits in one I2C transaction
bool elboI72WriteBatch(uint8_t clear_mask, uint8_t set_bits);
bool elboI73WriteBatch(uint8_t clear_mask, uint8_t set_bits);

plc_status_t plcGetStatus();
uint32_t plcGetLastReadTime();
uint32_t plcGetErrorCount();
void plcDiagnostics();

#endif