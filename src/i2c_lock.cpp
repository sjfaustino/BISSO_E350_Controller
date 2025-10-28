#include "globals.h"
static volatile bool g_i2c_locked=false;
static uint32_t g_i2c_owner=0;
void i2cLockInit(){ g_i2c_locked=false; g_i2c_owner=0; }
bool i2cTryLock(uint32_t timeoutMs){
  uint32_t me=(uint32_t)xTaskGetCurrentTaskHandle();
  uint32_t t0=millis();
  while(true){
    noInterrupts();
    if(!g_i2c_locked){ g_i2c_locked=true; g_i2c_owner=me; interrupts(); return true; }
    interrupts();
    if(millis()-t0>=timeoutMs) return false;
    delay(1);
  }
}
void i2cUnlock(){
  uint32_t me=(uint32_t)xTaskGetCurrentTaskHandle();
  noInterrupts();
  if(g_i2c_locked && g_i2c_owner==me){ g_i2c_locked=false; g_i2c_owner=0; }
  interrupts();
}
