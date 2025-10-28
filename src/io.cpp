#include "io.h"

#define ADDR_OUT1 0x24
#define ADDR_OUT2 0x25
#define ADDR_IN1  0x21
#define ADDR_IN2  0x22

static uint8_t out1=0, out2=0, in1=0, in2=0; static bool dirty=false;

static void pcfWrite(uint8_t addr,uint8_t v){
  if(!i2cTryLock(10)) return;
  Wire.beginTransmission(addr);
  Wire.write(v);
  Wire.endTransmission();
  i2cUnlock();
}

void pushOutputs(){ if(!dirty) return; pcfWrite(ADDR_OUT1,out1); pcfWrite(ADDR_OUT2,out2); dirty=false; }
void outputsIdle(){ out1=0; out2=0; dirty=true; pushOutputs(); }

void ioInit(){
  Wire.begin(); i2cLockInit();
  outputsIdle();
}

void readInputs(){
  if(i2cTryLock(10)){
    Wire.requestFrom(ADDR_IN1,(uint8_t)1); if(Wire.available()) in1=Wire.read();
    Wire.requestFrom(ADDR_IN2,(uint8_t)1); if(Wire.available()) in2=Wire.read();
    i2cUnlock();
  }
}

bool X_SEL_X (){ return in1 & (1<<0); }
bool X_SEL_Y (){ return in1 & (1<<1); }
bool X_SEL_XY(){ return in1 & (1<<2); }
bool X_AUTO  (){ return in1 & (1<<3); }

#define BIT_Y(n) (1<<(n))
void Y_FAST(bool on){ if(on) out1|=BIT_Y(0); else out1&=~BIT_Y(0); dirty=true; }
void Y_MED (bool on){ if(on) out1|=BIT_Y(1); else out1&=~BIT_Y(1); dirty=true; }
void Y_AX_X(bool on){ if(on) out1|=BIT_Y(2); else out1&=~BIT_Y(2); dirty=true; }
void Y_AX_Y(bool on){ if(on) out1|=BIT_Y(3); else out1&=~BIT_Y(3); dirty=true; }
void Y_AX_Z(bool on){ if(on) out1|=BIT_Y(4); else out1&=~BIT_Y(4); dirty=true; }
void Y_AX_A(bool on){ if(on) out1|=BIT_Y(5); else out1&=~BIT_Y(5); dirty=true; }
void Y_DIR_POS(bool on){ if(on) out1|=BIT_Y(6); else out1&=~BIT_Y(6); dirty=true; }
void Y_DIR_NEG(bool on){ if(on) out1|=BIT_Y(7); else out1&=~BIT_Y(7); dirty=true; }
void Y_VS    (bool on){ if(on) out2|=BIT_Y(0); else out2&=~BIT_Y(0); dirty=true; }

bool Y_DIR_POS_STATE(){ return out1 & BIT_Y(6); }
bool Y_DIR_NEG_STATE(){ return out1 & BIT_Y(7); }

void setYIndex(uint8_t idx,bool on){
  outputsIdle();
  switch(idx){
    case 0: Y_FAST(on); break;
    case 1: Y_MED(on);  break;
    case 2: Y_AX_X(on); break;
    case 3: Y_AX_Y(on); break;
    case 4: Y_AX_Z(on); break;
    case 5: Y_AX_A(on); break;
    case 6: Y_DIR_POS(on); break;
    case 7: Y_DIR_NEG(on); break;
    case 8: Y_VS(on); break;
  }
}
