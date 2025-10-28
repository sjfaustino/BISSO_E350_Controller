#include "inputs.h"
#include "io.h"

struct Debounce{
  int pin; bool state; bool last; uint32_t t; uint16_t d;
  void begin(int p,bool pullup,uint16_t dm){ pin=p; d=dm; t=0; pinMode(pin,pullup?INPUT_PULLUP:INPUT); state=last=digitalRead(pin); }
  void poll(){ bool r=digitalRead(pin); if(r!=state){ if(millis()-t>=d){ last=state; state=r; t=millis(); }} else t=millis(); }
  bool read()const{return state;} bool rose()const{return (!last && state);}
};

#define PIN_START_BTN  34
#define PIN_ONOFF_SW   35
#define PIN_ESTOP_IN   39
#define PIN_HEARTBEAT   2

static Debounce btnStart, swOnOff, inEstop;
static bool roseStart=false;

void inputsInit(uint16_t debounce_ms){
  pinMode(PIN_HEARTBEAT,OUTPUT);
  btnStart.begin(PIN_START_BTN,false,debounce_ms);
  swOnOff.begin(PIN_ONOFF_SW,false,debounce_ms);
  inEstop.begin(PIN_ESTOP_IN,false,debounce_ms);
}
void inputsPoll(){
  static uint32_t hbT=0;
  btnStart.poll(); swOnOff.poll(); inEstop.poll();
  roseStart = btnStart.rose();
  if(millis()-hbT>=500){ hbT=millis(); digitalWrite(PIN_HEARTBEAT,!digitalRead(PIN_HEARTBEAT)); }
}

bool btnStartRose(){ bool r=roseStart; roseStart=false; return r; }
bool btnStartRead(){ return btnStart.read(); }
bool swOnOffRead(){ return swOnOff.read(); }
bool inEstopRead(){ return inEstop.read(); }

bool X_AUTO(){ return ::X_AUTO(); }
bool X_SEL_X(){ return ::X_SEL_X(); }
bool X_SEL_Y(){ return ::X_SEL_Y(); }
bool X_SEL_XY(){ return ::X_SEL_XY(); }
