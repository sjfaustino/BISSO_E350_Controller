/*
   ┌───────────────────────────────────────────────────────────────┐
   │   BISSO PROJECT — Industrial Motion & Process Controller      │
   │   © 2025 Sergio Faustino. All rights reserved.               │
   │   Firmware and documentation for the BISSO E350 Controller.  │
   │   Use, reproduction, and modification are permitted within   │
   │   the Bisso project ecosystem and associated hardware.       │
   │   Redistribution outside this context requires permission.   │
   └───────────────────────────────────────────────────────────────┘
*/

/*─────────────────────────────────────────────────────────────
  1) Version, Includes, LCD, SPIFFS, Type Declarations
─────────────────────────────────────────────────────────────*/
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>
#include <LiquidCrystal_I2C.h>

/*─────────────────────────────────────────────────────────────
  2) Core Type Definitions
─────────────────────────────────────────────────────────────*/
// --- Unified AlarmCode (placed FIRST to avoid scope errors)
enum class AlarmCode : uint16_t {
  NONE=0, ESTOP, TEMP_WARN, TEMP_TRIP, SENSOR_FAULT, SOFTLIMIT,
  ENC_MISMATCH, TASK_TIMEOUT, SAFE_RESTART_BLOCK, OUTPUT_INTERLOCK
};

// --- Axis Enumeration
enum Axis : uint8_t { AX_X=0, AX_Y=1, AX_Z=2, AX_A=3 };
const char* axisName[4] = {"X","Y","Z","A"};

// --- Main configuration struct (Preferences-backed)
struct AdcCal { float gain; float offset; };
#define ADC_NUM_CHANNELS 4
struct Config {
  uint16_t schema, reserved;
  uint32_t debounce_ms;
  float temp_warn_C, temp_trip_C;
  AdcCal cal[ADC_NUM_CHANNELS];
  float softMin[4], softMax[4];
  uint8_t out1_addr, out2_addr, in1_addr, in2_addr, lcd_addr;
  bool output_active_low;
};

// --- Motion move object
struct Move { Axis axis; float targetAbs; float feed; };

// --- Clamp without templates (Arduino preprocessor-safe)
#ifndef CLAMP
#define CLAMP(v, lo, hi) ( ((v) < (lo)) ? (lo) : ( ((v) > (hi)) ? (hi) : (v) ) )
#endif

/*─────────────────────────────────────────────────────────────
  3) Versioning, Constants, Pins
─────────────────────────────────────────────────────────────*/
static const char* FW_NAME    = "BISSO_E350_Controller";
static const char* FW_VERSION = "v0.4.5";
static const uint16_t CONFIG_SCHEMA_VERSION = 0x0450;

#define LCD_COLS 20
#define LCD_ROWS 4
#define TICK_MS 10
#define DIAG_LONGPRESS_MS 5000
#define SELFTEST_HOLD_MS  1500
#define ENC_MIN_DELTA     1

// KC868-A16 GPIO Map
const int PIN_START_BTN   = 36;  // VP  (debounced as digital)
const int PIN_ONOFF_SW    = 39;  // VN  (debounced as digital)
const int PIN_ESTOP_IN    = 34;  // ESTOP input (active-HIGH)
const int PIN_HEARTBEAT   = 2;   // onboard LED

// UART2 for WJ66
#define WJ66_UART_BAUD   9600
#define WJ66_UART_TX_PIN 14
#define WJ66_UART_RX_PIN 33
HardwareSerial& WJ66 = Serial2;

/*─────────────────────────────────────────────────────────────
  4) Forward Function Prototypes
─────────────────────────────────────────────────────────────*/
void lcdPrintLine(uint8_t row, const String& s);
void outputsIdle(void);
void journalLog(const char* level, const char* msg);
void alarmPush(AlarmCode code, int16_t detail);

/*─────────────────────────────────────────────────────────────
  5) Debounce Class
─────────────────────────────────────────────────────────────*/
class DebounceIn {
 public:
  void begin(uint8_t pin, bool pullup, uint32_t db_ms){
    _pin=pin; _db=db_ms; pinMode(pin, pullup?INPUT_PULLUP:INPUT);
    _stable = digitalRead(pin); _last=_stable; _t=millis();
  }
  void setDebounce(uint32_t ms){ _db=ms; }
  void poll(){
    bool raw=digitalRead(_pin);
    if (raw!=_last){ _last=raw; _t=millis(); }
    else if ((millis()-_t)>=_db && raw!=_stable){
      _rose = (!_stable) && raw;
      _fell = _stable && (!raw);
      _stable=raw;
    }
  }
  bool read() const { return _stable; }
  bool rose(){ bool r=_rose; _rose=false; return r; }
  bool fell(){ bool r=_fell; _fell=false; return r; }
 private:
  uint8_t _pin=0; uint32_t _db=30,_t=0;
  bool _stable=false,_last=false;
  bool _rose=false,_fell=false;
};

/*─────────────────────────────────────────────────────────────
  6) Preferences / Config Runtime Mirrors
─────────────────────────────────────────────────────────────*/
Preferences prefs;
Config cfg;

uint8_t ADDR_OUT1=0x24, ADDR_OUT2=0x25, ADDR_IN1=0x21, ADDR_IN2=0x22;
bool OUTPUT_ACTIVE_LOW=false;

/*─────────────────────────────────────────────────────────────
  7) ADC Helpers (smoothing + mock temperature)
─────────────────────────────────────────────────────────────*/
const int ADC_PINS[ADC_NUM_CHANNELS] = {36,39,34,35};
#define ADC_SMOOTHING_SAMPLES 8
struct AdcFilt { uint16_t buf[ADC_SMOOTHING_SAMPLES]; uint8_t head=0; bool primed=false; } adcFilt[ADC_NUM_CHANNELS];

uint16_t adcReadRaw(int i){
  int v = analogRead(ADC_PINS[i]);
  return (uint16_t)CLAMP(v, 0, 4095);
}

float adcReadLinearized(int i){
  uint16_t raw=adcReadRaw(i);
  AdcFilt& f=adcFilt[i];
  f.buf[f.head]=raw; f.head=(f.head+1)%ADC_SMOOTHING_SAMPLES; if (f.head==0) f.primed=true;
  uint32_t sum=0; uint8_t n=f.primed?ADC_SMOOTHING_SAMPLES:f.head; for(uint8_t k=0;k<n;k++) sum+=f.buf[k];
  float avg=(n? (float)sum/n : raw);
  return cfg.cal[i].gain * avg + cfg.cal[i].offset;
}
float mockTemperatureC(){ return (adcReadLinearized(0)/4095.0f)*100.0f; }

/*─────────────────────────────────────────────────────────────
  8) Config Load/Save & Defaults
─────────────────────────────────────────────────────────────*/
void loadDefaultConfig(Config& c){
  c.schema=CONFIG_SCHEMA_VERSION; c.reserved=0;
  c.debounce_ms=30; c.temp_warn_C=70.0f; c.temp_trip_C=85.0f;
  for(int i=0;i<ADC_NUM_CHANNELS;i++){ c.cal[i].gain=1.0f; c.cal[i].offset=0.0f; }
  for(int a=0;a<4;a++){ c.softMin[a]=-1000.0f; c.softMax[a]=+1000.0f; }
  c.out1_addr=0x24; c.out2_addr=0x25; c.in1_addr=0x21; c.in2_addr=0x22; c.lcd_addr=0x27;
  c.output_active_low=false;
}
bool loadConfig(){
  prefs.begin("bisso-e350", true);
  bool ok = prefs.getUShort("schema",0)==CONFIG_SCHEMA_VERSION;
  if(ok){
    cfg.schema=prefs.getUShort("schema",CONFIG_SCHEMA_VERSION);
    cfg.reserved=prefs.getUShort("resv",0);
    cfg.debounce_ms=prefs.getUInt("debounce",30);
    cfg.temp_warn_C=prefs.getFloat("twarn",70.0f);
    cfg.temp_trip_C=prefs.getFloat("ttrip",85.0f);
    for(int i=0;i<ADC_NUM_CHANNELS;i++){
      char gk[8],oky[8]; snprintf(gk,8,"g%02d",i); snprintf(oky,8,"o%02d",i);
      cfg.cal[i].gain=prefs.getFloat(gk,1.0f); cfg.cal[i].offset=prefs.getFloat(oky,0.0f);
    }
    for(int a=0;a<4;a++){
      char mn[8],mx[8]; snprintf(mn,8,"mn%1d",a); snprintf(mx,8,"mx%1d",a);
      cfg.softMin[a]=prefs.getFloat(mn,-1000.0f); cfg.softMax[a]=prefs.getFloat(mx,+1000.0f);
    }
    cfg.out1_addr=prefs.getUChar("o1",0x24); cfg.out2_addr=prefs.getUChar("o2",0x25);
    cfg.in1_addr=prefs.getUChar("i1",0x21); cfg.in2_addr=prefs.getUChar("i2",0x22);
    cfg.lcd_addr=prefs.getUChar("lcd",0x27);
    cfg.output_active_low=prefs.getBool("pol",false);
  }else{
    loadDefaultConfig(cfg);
  }
  prefs.end();
  ADDR_OUT1=cfg.out1_addr; ADDR_OUT2=cfg.out2_addr;
  ADDR_IN1=cfg.in1_addr;   ADDR_IN2=cfg.in2_addr;
  OUTPUT_ACTIVE_LOW=cfg.output_active_low;
  return ok;
}
void saveConfig(const Config& c){
  prefs.begin("bisso-e350", false);
  prefs.putUShort("schema", c.schema); prefs.putUShort("resv", c.reserved);
  prefs.putUInt("debounce", c.debounce_ms);
  prefs.putFloat("twarn", c.temp_warn_C); prefs.putFloat("ttrip", c.temp_trip_C);
  for(int i=0;i<ADC_NUM_CHANNELS;i++){
    char gk[8],oky[8]; snprintf(gk,8,"g%02d",i); snprintf(oky,8,"o%02d",i);
    prefs.putFloat(gk, c.cal[i].gain); prefs.putFloat(oky, c.cal[i].offset);
  }
  for(int a=0;a<4;a++){
    char mn[8],mx[8]; snprintf(mn,8,"mn%1d",a); snprintf(mx,8,"mx%1d",a);
    prefs.putFloat(mn, c.softMin[a]); prefs.putFloat(mx, c.softMax[a]);
  }
  prefs.putUChar("o1", c.out1_addr); prefs.putUChar("o2", c.out2_addr);
  prefs.putUChar("i1", c.in1_addr);  prefs.putUChar("i2", c.in2_addr);
  prefs.putUChar("lcd", c.lcd_addr);
  prefs.putBool("pol", c.output_active_low);
  prefs.end();
}

/*─────────────────────────────────────────────────────────────
  9) LCD Helpers
─────────────────────────────────────────────────────────────*/
LiquidCrystal_I2C* lcdp=nullptr;
void lcdInit(uint8_t addr){
  if(lcdp){ delete lcdp; lcdp=nullptr; }
  lcdp = new LiquidCrystal_I2C(addr, LCD_COLS, LCD_ROWS);
  lcdp->init(); lcdp->backlight(); lcdp->clear();
}
void lcdPrintLine(uint8_t row, const String& s){
  if(!lcdp) return;
  lcdp->setCursor(0,row);
  char buf[LCD_COLS+1];
  snprintf(buf,sizeof(buf), "%-*.*s", LCD_COLS, LCD_COLS, s.c_str());
  lcdp->print(buf);
}

/*─────────────────────────────────────────────────────────────
  10) SPIFFS Journal (enabled by default)
─────────────────────────────────────────────────────────────*/
static const char* JPATH="/journal.txt";
bool journalInitDone=false;
void journalInit(){
  if(!SPIFFS.begin(true)) return;
  journalInitDone=true;
  journalLog("INFO","BOOT");
}
void journalLog(const char* level, const char* msg){
  if(!journalInitDone) return;
  File f=SPIFFS.open(JPATH, FILE_APPEND);
  if(!f) return;
  f.printf("%10lu [%s] %s\n",(unsigned long)millis(), level, msg);
  f.close();
}

/*─────────────────────────────────────────────────────────────
  11) Alarms (Standardized) & Buffers
─────────────────────────────────────────────────────────────*/
const char* alarmToStr(AlarmCode c){
  switch(c){
    case AlarmCode::NONE: return "NONE";
    case AlarmCode::ESTOP: return "ESTOP";
    case AlarmCode::TEMP_WARN: return "TEMP_WARN";
    case AlarmCode::TEMP_TRIP: return "TEMP_TRIP";
    case AlarmCode::SENSOR_FAULT: return "SENSOR_FAULT";
    case AlarmCode::SOFTLIMIT: return "SOFTLIMIT";
    case AlarmCode::ENC_MISMATCH: return "ENC_MISMATCH";
    case AlarmCode::TASK_TIMEOUT: return "TASK_TIMEOUT";
    case AlarmCode::SAFE_RESTART_BLOCK: return "SAFE_RESTART_BLOCK";
    case AlarmCode::OUTPUT_INTERLOCK: return "OUTPUT_INTERLOCK";
    default: return "UNKNOWN";
  }
}
struct AlarmEntry { uint32_t ts; AlarmCode code; int16_t detail; };
#define MAX_ALARMS 32
AlarmEntry alarms[MAX_ALARMS]; uint8_t alarmHead=0, alarmCount=0;
void alarmPush(AlarmCode code, int16_t detail){
  alarms[alarmHead] = {millis(), code, detail};
  alarmHead=(alarmHead+1)%MAX_ALARMS; alarmCount = (alarmCount<MAX_ALARMS)?(alarmCount+1):MAX_ALARMS;
  char line[128]; snprintf(line,sizeof(line),"ALARM %s detail=%d", alarmToStr(code), (int)detail);
  journalLog("WARN", line);
}
void alarmClear(){ alarmHead=0; alarmCount=0; journalLog("INFO","ALARM_CLEAR"); }

/*─────────────────────────────────────────────────────────────
  12) PCF8574 I/O Mapping (Active-HIGH)
─────────────────────────────────────────────────────────────*/
// Output shadow
uint8_t outState1=0x00; // Y01..Y08  (PCF @ ADDR_OUT1)
uint8_t outState2=0x00; // Y09..Y16  (PCF @ ADDR_OUT2)

void pcfWrite(uint8_t addr, uint8_t value){
  Wire.beginTransmission(addr);
  Wire.write(value);
  Wire.endTransmission();
}
uint8_t pcfRead(uint8_t addr){
  Wire.requestFrom((int)addr,1);
  return Wire.available()? Wire.read() : 0x00;
}

// idx: 0..7 => OUT1 P0..P7 (Y01..Y08) ; 8..15 => OUT2 P0..P7 (Y09..Y16)
void setYIndex(uint8_t idx, bool on){
  if(idx<8){
    uint8_t mask=(1u<<idx);
    if(OUTPUT_ACTIVE_LOW){ if(on) outState1&=~mask; else outState1|=mask; }
    else                 { if(on) outState1|=mask;  else outState1&=~mask; }
    pcfWrite(ADDR_OUT1, outState1);
  } else if (idx<16){
    uint8_t b=idx-8, mask=(1u<<b);
    if(OUTPUT_ACTIVE_LOW){ if(on) outState2&=~mask; else outState2|=mask; }
    else                 { if(on) outState2|=mask;  else outState2&=~mask; }
    pcfWrite(ADDR_OUT2, outState2);
  }
}

// Named outputs per mapping
inline void Y_FAST(bool on){ setYIndex(0,on); }     // Y01
inline void Y_MED(bool on){ setYIndex(1,on); }      // Y02
inline void Y_AX_X(bool on){ setYIndex(2,on); }     // Y03
inline void Y_AX_Y(bool on){ setYIndex(3,on); }     // Y04
inline void Y_AX_Z(bool on){ setYIndex(4,on); }     // Y05
inline void Y_AX_A(bool on){ setYIndex(5,on); }     // Y06
inline void Y_DIR_POS(bool on){ setYIndex(6,on); }  // Y07
inline void Y_DIR_NEG(bool on){ setYIndex(7,on); }  // Y08
inline void Y_VS(bool on){ setYIndex(8,on); }       // Y09

// Inputs (Active-HIGH)
inline uint8_t X_IN_LO(){ return pcfRead(ADDR_IN1); } // X01..X08
inline uint8_t X_IN_HI(){ return pcfRead(ADDR_IN2); } // X09..X16
inline bool X_SEL_X(){ return (X_IN_LO() & (1u<<0))!=0; } // X01
inline bool X_SEL_Y(){ return (X_IN_LO() & (1u<<1))!=0; } // X02
inline bool X_SEL_XY(){return (X_IN_LO() & (1u<<2))!=0; } // X03
inline bool X_AUTO(){  return (X_IN_LO() & (1u<<3))!=0; } // X04

void outputsIdle(){
  Y_FAST(false); Y_MED(false);
  Y_AX_X(false); Y_AX_Y(false); Y_AX_Z(false); Y_AX_A(false);
  Y_DIR_POS(false); Y_DIR_NEG(false);
  Y_VS(false);
  journalLog("INFO","OUTPUTS_IDLE");
}

/*─────────────────────────────────────────────────────────────
  13) WJ66 Encoder Interface (health counters)
─────────────────────────────────────────────────────────────*/
struct WJ66State {
  long pos[4] = {0,0,0,0};
  uint8_t buf[128]; int len=0;
  bool hasFrame=false;
  uint32_t lastUpdate=0;
  // health
  uint32_t frames=0, parsed=0, staleHits=0;
} wj66;

bool wj66Stale(){ return (millis() - wj66.lastUpdate) > 1000; }

void wj66Poll(){
  bool parsedThisLoop=false;
  while (WJ66.available()){
    uint8_t b = (uint8_t)WJ66.read();
    if (wj66.len < (int)sizeof(wj66.buf)) wj66.buf[wj66.len++] = b;
    if (b=='\n' || wj66.len>=127){
      wj66.buf[wj66.len]=0; wj66.hasFrame=true; wj66.frames++;
      char *p=(char*)wj66.buf;
      long X=wj66.pos[0],Y=wj66.pos[1],Z=wj66.pos[2],A=wj66.pos[3];
      int n = sscanf(p,"X:%ld Y:%ld Z:%ld A:%ld",&X,&Y,&Z,&A);
      if (n==4){
        wj66.pos[0]=X; wj66.pos[1]=Y; wj66.pos[2]=Z; wj66.pos[3]=A;
        wj66.parsed++; parsedThisLoop=true;
      }
      wj66.len=0; wj66.lastUpdate = millis();
    }
  }
  if (!parsedThisLoop && wj66Stale()) wj66.staleHits++;
}
uint8_t wj66GoodPct(){
  if (wj66.frames==0) return 0;
  uint32_t pct = (wj66.parsed*100UL)/wj66.frames;
  return (pct>100)?100:(uint8_t)pct;
}

/*─────────────────────────────────────────────────────────────
  14) Axis / Motion Primitives
─────────────────────────────────────────────────────────────*/
/*─────────────────────────────────────────────────────────────
  Updated setSpeedBits()
  - Feed ≤ 300 → MEDIUM speed (Y_MED)
  - Feed ≥ 600 → FAST speed (Y_FAST)
  - Feed between → retain previous state (no relay flicker)
  - Feed ≤ 0 → defaults to MEDIUM (safety fallback)
─────────────────────────────────────────────────────────────*/
void setSpeedBits(float feed) {
  static uint8_t lastMode = 0; // 0=idle, 1=medium, 2=fast

  // Turn everything off initially
  Y_FAST(false);
  Y_MED(false);

  // Determine speed category
  uint8_t mode = 0;
  if (feed <= 0.0f) { mode = 1; }          // Default to MEDIUM if undefined
  else if (feed <= 300.0f) { mode = 1; }   // Medium speed threshold
  else if (feed >= 600.0f) { mode = 2; }   // Fast speed threshold
  else { mode = lastMode; }                // Between → keep previous speed

  // Apply relay state based on mode
  if (mode == 1) {
    Y_MED(true);
    lastMode = 1;
    journalLog("INFO", "SPEED_SET_MEDIUM");
  } else if (mode == 2) {
    Y_FAST(true);
    lastMode = 2;
    journalLog("INFO", "SPEED_SET_FAST");
  } else {
    lastMode = 0;
  }
}

void setAxisSelect(Axis a){
  Y_AX_X(false); Y_AX_Y(false); Y_AX_Z(false); Y_AX_A(false);
  switch(a){
    case AX_X: Y_AX_X(true); break;
    case AX_Y: Y_AX_Y(true); break;
    case AX_Z: Y_AX_Z(true); break;
    case AX_A: Y_AX_A(true); break;
  }
}
// Output Interlock-aware direction control
void setDirBits(bool dir_pos){
  Y_DIR_POS(false); Y_DIR_NEG(false);
  if (dir_pos) Y_DIR_POS(true); else Y_DIR_NEG(true);

  bool posOn = ( (outState1 & (1u<<6)) != 0 );
  bool negOn = ( (outState1 & (1u<<7)) != 0 );
  if (OUTPUT_ACTIVE_LOW){ posOn = !posOn; negOn = !negOn; }

  if (posOn && negOn){
    Y_DIR_POS(false); Y_DIR_NEG(false);
    alarmPush(AlarmCode::OUTPUT_INTERLOCK, 0);
    journalLog("ERROR","DIR_INTERLOCK both HIGH -> forced OFF");
  }
}

/*─────────────────────────────────────────────────────────────
  15) Motion Queue / Enqueue / G-code Parser
─────────────────────────────────────────────────────────────*/
#define QMAX 64
Move q[QMAX]; uint8_t qHead=0, qTail=0; volatile uint8_t qCnt=0;

bool qPush(const Move &m){ if(qCnt>=QMAX) return false; q[qTail]=m; qTail=(qTail+1)%QMAX; qCnt++; return true; }
bool qPop(Move &m){ if(qCnt==0) return false; m=q[qHead]; qHead=(qHead+1)%QMAX; qCnt--; return true; }

bool enqueueAxisMove(Axis a, float targetAbs, float feed){
  float mn = cfg.softMin[(int)a], mx = cfg.softMax[(int)a];
  if (targetAbs<mn || targetAbs>mx){ alarmPush(AlarmCode::SOFTLIMIT, (int16_t)a); return false; }
  Move m{a, targetAbs, feed}; return qPush(m);
}

/*─────────────────────────────────────────────────────────────
  Updated parseGcodeLine()
  - Default speed: MEDIUM (Y02) if no F specified
  - Mode gating:
      X_SEL_Y() → "C"  → allow Y, Z, F
      X_SEL_X() → "T"  → allow X, Z, F
      X_SEL_XY() → "C+T" → allow X, Y, Z, F
  - Only one axis per G-code line accepted
  - AUTO input (X04) must be HIGH to execute
─────────────────────────────────────────────────────────────*/
void parseGcodeLine(const String& line) {

  // AUTO mode gate
  if (!X_AUTO()) {
    journalLog("WARN", "GCODE_REJECT auto=LOW");
    return;
  }

  // Copy to C string for token parsing
  char buf[128];
  line.substring(0, sizeof(buf) - 1).toCharArray(buf, sizeof(buf));
  char *p = buf;

  // Detect G/M command
  char gc = ' ';
  int gnum = 0, mnum = 0;
  bool isM = false;

  while (*p == ' ') p++;
  if (*p == 'G') { isM = false; gc = 'G'; p++; gnum = strtol(p, &p, 10); }
  else if (*p == 'M') { isM = true; gc = 'M'; p++; mnum = strtol(p, &p, 10); }
  else { return; }

  // Axis / feed accumulators
  float tgt[4] = {NAN, NAN, NAN, NAN};
  float fFeed = 0.0f;

  // Tokenize rest of line
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    char c = toupper(*p);
    if (c == 'X' || c == 'Y' || c == 'Z' || c == 'A') {
      p++;
      float v = strtof(p, &p);
      int ai = (c == 'X') ? 0 : (c == 'Y') ? 1 : (c == 'Z') ? 2 : 3;
      tgt[ai] = v;
    } else if (c == 'F') {
      p++;
      fFeed = strtof(p, &p);
    } else {
      p++;
    }
  }

  // ------------------------------------------------------------
  // Determine allowed axes based on mode selector inputs
  bool modeC  = X_SEL_Y();   // "C" cut
  bool modeT  = X_SEL_X();   // "T" traverse
  bool modeCT = X_SEL_XY();  // "C+T"

  bool allowX = false, allowY = false, allowZ = true;  // Z always allowed
  if (modeC)  { allowY = true; }
  if (modeT)  { allowX = true; }
  if (modeCT) { allowX = true; allowY = true; }

  if (!modeC && !modeT && !modeCT) {
    journalLog("WARN", "NO_MODE_SELECTED");
    alarmPush(AlarmCode::TASK_TIMEOUT, 0);  // repurpose as mode fault
    return;
  }

  // ------------------------------------------------------------
  // Count and validate axes in command
  int axisCount = 0;
  for (int a = 0; a < 4; a++) if (!isnan(tgt[a])) axisCount++;

  if (axisCount == 0) {
    journalLog("WARN", "NO_AXIS_SPECIFIED");
    return;
  }
  if (axisCount > 1) {
    journalLog("WARN", "MULTI_AXIS_REJECTED");
    alarmPush(AlarmCode::TASK_TIMEOUT, axisCount);
    return;
  }

  // ------------------------------------------------------------
  // Resolve target axis and permission
  Axis ax = AX_X;
  float target = 0.0f;
  if (!isnan(tgt[0])) { ax = AX_X; target = tgt[0]; if (!allowX) { journalLog("WARN", "X_AXIS_NOT_ALLOWED"); return; } }
  if (!isnan(tgt[1])) { ax = AX_Y; target = tgt[1]; if (!allowY) { journalLog("WARN", "Y_AXIS_NOT_ALLOWED"); return; } }
  if (!isnan(tgt[2])) { ax = AX_Z; target = tgt[2]; if (!allowZ) { journalLog("WARN", "Z_AXIS_NOT_ALLOWED"); return; } }

  // ------------------------------------------------------------
  // Default feed rate handling
  if (fFeed <= 0.0f) {
    fFeed = 300.0f;  // default to medium feed
    journalLog("INFO", "FEED_DEFAULT_MEDIUM");
  }

  // ------------------------------------------------------------
  // Execute command type
  if (!isM) {
    if (gnum == 0 || gnum == 1) {
      enqueueAxisMove(ax, target, fFeed);
      journalLog("INFO", "GCODE_SINGLE_AXIS_OK");
    }
  } else {
    switch (mnum) {
      case 3:  Y_VS(true);  journalLog("INFO", "M3 SPINDLE ON");  break;
      case 5:  Y_VS(false); journalLog("INFO", "M5 SPINDLE OFF"); break;
      case 30: outputsIdle(); journalLog("INFO", "M30 PROGRAM END"); break;
      default: break;
    }
  }
}


/*─────────────────────────────────────────────────────────────
  16) Display Helpers: Delta Graphs, Diagnostics, CALIB UI
─────────────────────────────────────────────────────────────*/
long lastEnc[4]={0,0,0,0};
uint8_t deltaMag[4]={0,0,0,0}; // 0..10
uint32_t lastDeltaT=0;

void updateDeltaGraph(){
  if (millis()-lastDeltaT<200) return; // ~5 Hz
  lastDeltaT=millis();
  for (int a=0;a<4;a++){
    long d = labs(wj66.pos[a] - lastEnc[a]); lastEnc[a]=wj66.pos[a];
    uint32_t bucket = (d>100)?10 : (d/10);
    if (bucket>10) bucket=10;
    deltaMag[a]=(uint8_t)bucket;
  }
}
String bar10(uint8_t n){ String s=""; for (int i=0;i<10;i++) s += (i<n)?'|':' '; return s; }
String byteBits(uint8_t b){ String s; s.reserve(8); for (int i=7;i>=0;i--) s += ((b>>i)&1)?'1':'0'; return s; }

void showRun(){
  String l1 = "RUN  AUTO:" + String(X_AUTO()?"1":"0");
  String l2 = "Q:" + String(qCnt) + " T:" + String(mockTemperatureC(),1) + "C";
  String l3 = "X[" + bar10(deltaMag[0]) + "] Y[" + bar10(deltaMag[1]) + "]";
  lcdPrintLine(0, l1);
  lcdPrintLine(1, l2);
  lcdPrintLine(2, l3);
  lcdPrintLine(3, "Z[" + bar10(deltaMag[2]) + "] A[" + bar10(deltaMag[3]) + "]");
}

void showDiagnostics(){
  uint8_t xin1 = X_IN_LO(); uint8_t xin2 = X_IN_HI();
  lcdPrintLine(0, "DIAGNOSTICS");
  lcdPrintLine(1, "X01-08:"+byteBits(xin1));
  lcdPrintLine(2, "X09-16:"+byteBits(xin2));
  lcdPrintLine(3, "WJ66 ok%:"+String((int)wj66GoodPct())+" st:"+String(wj66.staleHits));
}

String barFromMinMax(float v, float mn, float mx){
  char buf[LCD_COLS+1]={0};
  float span = (mx>mn)? (mx-mn) : 1.0f;
  float t = (v-mn)/span; if (t<0) t=0; if (t>1) t=1;
  int filled = (int)(t * (LCD_COLS));
  for (int i=0;i<LCD_COLS;i++) buf[i] = (i<filled)?'#':'-';
  return String(buf);
}

uint8_t calibAxis = 0;
bool showStored=false; uint32_t storeMsgT=0;

void showCalib(){
  float mn = cfg.softMin[calibAxis], mx = cfg.softMax[calibAxis];
  float here = (float)wj66.pos[calibAxis];

  if (showStored && (millis()-storeMsgT<1000)){
    lcdPrintLine(0,"CALIB MODE");
    lcdPrintLine(1,"Stored! Axis:"+String(axisName[calibAxis]));
    lcdPrintLine(2,"Min:"+String(mn,0)+" Max:"+String(mx,0));
    lcdPrintLine(3, barFromMinMax(here, mn, mx));
    return;
  }
  lcdPrintLine(0,"CALIB MODE");
  lcdPrintLine(1,"Axis:"+String(axisName[calibAxis])+" ENC="+String((long)here));
  lcdPrintLine(2,"Min:"+String(mn,0)+" Max:"+String(mx,0));
  lcdPrintLine(3, barFromMinMax(here, mn, mx));
}

/*─────────────────────────────────────────────────────────────
  17) FSM, Buttons, Pages
─────────────────────────────────────────────────────────────*/
enum class State : uint8_t { IDLE=0, RUN=1, ERROR=2, DIAGNOSTICS=3, SELF_TEST=4, CALIB=5 };
State state = State::IDLE;

DebounceIn btnStart, swOnOff, inEstop;
uint32_t hbT=0;
uint32_t startPressT=0; bool startHeldDiag=false;

void showIdle(){
  lcdPrintLine(0, String(FW_NAME)+" "+FW_VERSION);
  String a="AUTO:"+String(X_AUTO()?"1":"0")+" ESTOP:"+String(inEstop.read()?"1":"0");
  lcdPrintLine(1, "Press START to Run");
  lcdPrintLine(2, a);
  lcdPrintLine(3, "");
}
void showError(){
  lcdPrintLine(0,"*** ERROR ***");
  if (alarmCount){
    uint8_t idx = (alarmHead + MAX_ALARMS - 1) % MAX_ALARMS;
    String s = String(alarmToStr(alarms[idx].code)) + " d=" + String(alarms[idx].detail);
    lcdPrintLine(1,s);
  } else {
    lcdPrintLine(1,"No alarms logged");
  }
  lcdPrintLine(2,"Release E-STOP");
  lcdPrintLine(3,"Press START to idle");
}
void enterCalib(){ state=State::CALIB; calibAxis=0; showStored=false; }

/*─────────────────────────────────────────────────────────────
  18) Motion Executor (queue-driven)
─────────────────────────────────────────────────────────────*/
void outputsMotionIdle(){ Y_FAST(false); Y_MED(false); Y_DIR_POS(false); Y_DIR_NEG(false); }

void motionTask(){
  static bool busy=false; static Move cur; static uint32_t startT=0;
  if (!busy){
    if (qCnt==0){ outputsMotionIdle(); return; }
    if (!qPop(cur)) return;
    setAxisSelect(cur.axis);
    long here = wj66.pos[(int)cur.axis];
    bool dirPos = (cur.targetAbs > (float)here);
    setDirBits(dirPos);
    setSpeedBits(cur.feed);
    startT = millis();
    busy=true;
    return;
  }
  long here = wj66.pos[(int)cur.axis];
  long tgt  = (long)cur.targetAbs;
  long delta = tgt - here;

  if (wj66Stale()){
    alarmPush(AlarmCode::ENC_MISMATCH, (int16_t)cur.axis);
    state=State::ERROR; outputsIdle(); busy=false; return;
  }
  if ((delta==0) || ((delta>0) && (here>=tgt)) || ((delta<0) && (here<=tgt))){
    outputsMotionIdle(); busy=false; return;
  }

  static long lastHere=0; static uint32_t stillT=0;
  if (here!=lastHere){ lastHere=here; stillT=millis(); }
  else if (millis()-stillT>2000){
    alarmPush(AlarmCode::ENC_MISMATCH, (int16_t)cur.axis);
    state=State::ERROR; outputsIdle(); busy=false; return;
  }
}

/*─────────────────────────────────────────────────────────────
  19) CALIB Logic
─────────────────────────────────────────────────────────────*/
void calibTick(){
  if (X_SEL_X()) calibAxis=0;
  else if (X_SEL_Y()) calibAxis=1;
  else if (X_SEL_XY()) calibAxis=(calibAxis+1)%4;

  if (btnStart.read()){
    if (startPressT==0) startPressT=millis();
    if ((millis()-startPressT)>=SELFTEST_HOLD_MS){
      calibAxis=(calibAxis+1)%4; startPressT=millis();
    }
  } else if (startPressT!=0){
    float here = (float)wj66.pos[calibAxis];
    cfg.softMin[calibAxis] = min(cfg.softMin[calibAxis], here);
    cfg.softMax[calibAxis] = max(cfg.softMax[calibAxis], here);
    saveConfig(cfg);
    showStored=true; storeMsgT=millis();
    startPressT=0;
  }
}

/*─────────────────────────────────────────────────────────────
  20) SELF-TEST (Y-walk)
─────────────────────────────────────────────────────────────*/
void task_selftest(){
  if (state!=State::SELF_TEST) return;
  static uint8_t idx=0; static uint32_t stepT=0;
  if (millis()-stepT<400) return; stepT=millis();
  outputsIdle();
  setYIndex(idx%9, true); // walk Y01..Y09
  idx++;
  if (btnStart.rose()){ state=State::DIAGNOSTICS; outputsIdle(); }
}

/*─────────────────────────────────────────────────────────────
  21) Periodic Tasks
─────────────────────────────────────────────────────────────*/
void task_pollInputs(){
  btnStart.poll(); swOnOff.poll(); inEstop.poll();

  if (inEstop.read()){
    if (state!=State::ERROR){
      state=State::ERROR; outputsIdle(); alarmPush(AlarmCode::ESTOP,0);
    }
  }

  if ((state==State::IDLE || state==State::RUN)){
    if (btnStart.read()){
      if (startPressT==0) startPressT=millis();
      if (!startHeldDiag && millis()-startPressT>=DIAG_LONGPRESS_MS){
        state=State::DIAGNOSTICS; startHeldDiag=true;
      }
    } else { startPressT=0; startHeldDiag=false; }
  }

  if (state==State::IDLE && swOnOff.read() && btnStart.rose()){
    state=State::RUN; journalLog("INFO","ENTER_RUN");
  }
  if (state==State::RUN && !swOnOff.read()){
    state=State::IDLE; outputsIdle(); journalLog("INFO","LEAVE_RUN");
  }

  if (state==State::ERROR){
    if (!inEstop.read() && btnStart.rose()){
      state=State::IDLE; outputsIdle();
    }
  }

  if (state==State::DIAGNOSTICS){
    if (btnStart.rose()) state=State::IDLE;
    if (btnStart.read() && (millis()-startPressT)>=SELFTEST_HOLD_MS){
      state=State::SELF_TEST;
    }
  }
}

void task_housekeeping(){
  if (millis()-hbT>500){ hbT=millis(); digitalWrite(PIN_HEARTBEAT, !digitalRead(PIN_HEARTBEAT)); }

  float tC = mockTemperatureC();
  if (tC>=cfg.temp_trip_C){
    state=State::ERROR; outputsIdle(); alarmPush(AlarmCode::TEMP_TRIP, (int16_t)tC);
  } else if (tC>=cfg.temp_warn_C){
    alarmPush(AlarmCode::TEMP_WARN, (int16_t)tC);
  }
}
void task_sensorSanity(){
  for (int i=0;i<ADC_NUM_CHANNELS;i++){
    float L=adcReadLinearized(i);
    if (L<-50.0f || L>5000.0f) alarmPush(AlarmCode::SENSOR_FAULT,i);
  }
}
void task_motion_wrap(){ if (state==State::RUN) motionTask(); }
void task_wj66(){ wj66Poll(); }
void task_display(){
  updateDeltaGraph();
  switch(state){
    case State::IDLE:        showIdle(); break;
    case State::RUN:         showRun(); break;
    case State::ERROR:       showError(); break;
    case State::DIAGNOSTICS: showDiagnostics(); break;
    case State::SELF_TEST:   lcdPrintLine(0,"SELF-TEST (Y-walk)"); break;
    case State::CALIB:       showCalib(); break;
  }
}
void task_serial_gcode(){
  static char lbuf[128]; static uint8_t len=0;
  while (Serial.available()){
    char c=(char)Serial.read();
    if (c=='\r') continue;
    if (c=='\n'){
      lbuf[len]=0; if (len>0) parseGcodeLine(String(lbuf));
      len=0; continue;
    }
    if (len<sizeof(lbuf)-1) lbuf[len++]=c;
  }
}

/*─────────────────────────────────────────────────────────────
  22) CLI (help, show config, set io, save, clear, calib)
─────────────────────────────────────────────────────────────*/
void serialShowConfig(){
  Serial.println(F("# CONFIG"));
  Serial.printf("schema: 0x%04X\n", cfg.schema);
  Serial.printf("debounce_ms: %u\n", (unsigned)cfg.debounce_ms);
  Serial.printf("temp_warn_C: %.1f temp_trip_C: %.1f\n", cfg.temp_warn_C, cfg.temp_trip_C);
  Serial.printf("I2C IN1:0x%02X IN2:0x%02X OUT1:0x%02X OUT2:0x%02X LCD:0x%02X\n",
                cfg.in1_addr,cfg.in2_addr,cfg.out1_addr,cfg.out2_addr,cfg.lcd_addr);
  Serial.printf("polarity active_low: %d\n", (int)cfg.output_active_low);
  Serial.printf("soft limits:\n");
  for (int a=0;a<4;a++){
    Serial.printf("  %s: min=%0.1f max=%0.1f\n", axisName[a], cfg.softMin[a], cfg.softMax[a]);
  }
}
void serialHelp(){
  Serial.println(F("# Commands"));
  Serial.println(F("h           - help"));
  Serial.println(F("s           - show config"));
  Serial.println(F("io          - show i2c map & polarity"));
  Serial.println(F("set io out1 <hex> | out2 <hex> | in1 <hex> | in2 <hex> | lcd <hex> | pol <0|1>"));
  Serial.println(F("x           - save config"));
  Serial.println(F("r           - clear alarms"));
  Serial.println(F("cal         - enter calibration mode"));
}
void serialShowIO(){
  Serial.printf("IN1:0x%02X IN2:0x%02X OUT1:0x%02X OUT2:0x%02X LCD:0x%02X POL:%d\n",
                (int)ADDR_IN1,(int)ADDR_IN2,(int)ADDR_OUT1,(int)ADDR_OUT2,(int)cfg.lcd_addr,(int)OUTPUT_ACTIVE_LOW);
}
void serialLoopOnce(){
  static char buf[128]; static uint8_t len=0;
  while (Serial.available()){
    char c=(char)Serial.read();
    if (c=='\r') continue;
    if (c=='\n'){
      buf[len]=0; len=0;
      String s(buf);
      s.trim();
      if (s=="h") serialHelp();
      else if (s=="s") serialShowConfig();
      else if (s=="io") serialShowIO();
      else if (s=="x"){ saveConfig(cfg); Serial.println(F("OK SAVED")); }
      else if (s=="r"){ alarmClear(); Serial.println(F("OK ALARMS CLEARED")); }
      else if (s=="cal"){ enterCalib(); Serial.println(F("CALIB MODE")); }
      else if (s.startsWith("set io ")){
        int p2=s.indexOf(' ',7); if (p2>0){
          String item=s.substring(7,p2); String val=s.substring(p2+1); val.trim();
          long v = strtol(val.c_str(), nullptr, 16);
          if      (item=="out1") { cfg.out1_addr=(uint8_t)v; ADDR_OUT1=cfg.out1_addr; }
          else if (item=="out2") { cfg.out2_addr=(uint8_t)v; ADDR_OUT2=cfg.out2_addr; }
          else if (item=="in1")  { cfg.in1_addr=(uint8_t)v; ADDR_IN1=cfg.in1_addr; }
          else if (item=="in2")  { cfg.in2_addr=(uint8_t)v; ADDR_IN2=cfg.in2_addr; }
          else if (item=="lcd")  { cfg.lcd_addr=(uint8_t)v; lcdInit(cfg.lcd_addr); }
          else if (item=="pol")  { cfg.output_active_low=(v!=0); OUTPUT_ACTIVE_LOW=cfg.output_active_low; }
          Serial.println(F("OK"));
        }
      } else {
        parseGcodeLine(s); // AUTO-gated inside
      }
      continue;
    }
    if (len<sizeof(buf)-1) buf[len++]=c;
  }
}

/*─────────────────────────────────────────────────────────────
  23) Scheduler
─────────────────────────────────────────────────────────────*/
struct Task { void (*fn)(); uint32_t period; uint32_t last; };
void _t_poll(){ task_pollInputs(); }
void _t_hk(){ task_housekeeping(); }
void _t_disp(){ task_display(); }
void _t_motion(){ task_motion_wrap(); }
void _t_serial(){ task_serial_gcode(); }
void _t_wj66(){ task_wj66(); }
void _t_sensor(){ task_sensorSanity(); }
void _t_self(){ task_selftest(); }

Task tasks[] = {
  {_t_poll,   10,0},
  {_t_hk,     50,0},
  {_t_disp,  250,0},
  {_t_motion,  5,0},
  {_t_serial, 20,0},
  {_t_wj66,   50,0},
  {_t_sensor,2000,0},
  {_t_self,  100,0},
};
const int NTASKS = sizeof(tasks)/sizeof(tasks[0]);

/*─────────────────────────────────────────────────────────────
  24) Safe Restart Check
─────────────────────────────────────────────────────────────*/
void safeRestartCheck(){
  bool estopActive = inEstop.read();
  bool autoHigh = X_AUTO();
  bool queued = (qCnt>0);

  if (estopActive || autoHigh || queued){
    state = State::ERROR;
    outputsIdle();
    int16_t bits = (int16_t)((estopActive?1:0) | (autoHigh?2:0) | (queued?4:0));
    alarmPush(AlarmCode::SAFE_RESTART_BLOCK, bits);
    journalLog("WARN","SAFE_RESTART_BLOCK");
    lcdPrintLine(0, String(FW_NAME)+" "+FW_VERSION);
    lcdPrintLine(1, "SAFE RESTART BLOCK");
    String r2="E:"+String(estopActive)+" A:"+String(autoHigh)+" Q:"+String(queued);
    lcdPrintLine(2, r2);
    lcdPrintLine(3, "Release & START");
  }
}

/*─────────────────────────────────────────────────────────────
  25) setup() & loop()
─────────────────────────────────────────────────────────────*/
void setup(){
  pinMode(PIN_HEARTBEAT, OUTPUT); digitalWrite(PIN_HEARTBEAT, LOW);
  pinMode(PIN_START_BTN, INPUT);  // handled by DebounceIn
  pinMode(PIN_ONOFF_SW,  INPUT);
  pinMode(PIN_ESTOP_IN,  INPUT);

  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.printf("%s %s\n", FW_NAME, FW_VERSION);

  bool had = loadConfig();
  if (!had) { Serial.println(F("CFG: defaults loaded")); saveConfig(cfg); }

  Wire.begin(); delay(20);
  lcdInit(cfg.lcd_addr);

  // SPIFFS journal enabled by default
  journalInit();

  outState1 = 0x00; outState2 = 0x00;
  pcfWrite(ADDR_OUT1, outState1);
  pcfWrite(ADDR_OUT2, outState2);
  outputsIdle();

  btnStart.begin(PIN_START_BTN, false, cfg.debounce_ms);
  swOnOff.begin(PIN_ONOFF_SW,   false, cfg.debounce_ms);
  inEstop.begin(PIN_ESTOP_IN,   false, cfg.debounce_ms);

  WJ66.begin(WJ66_UART_BAUD, SERIAL_8N1, WJ66_UART_RX_PIN, WJ66_UART_TX_PIN);

  showIdle();
  safeRestartCheck();

  journalLog("INFO","SETUP_COMPLETE");
}

void loop(){
  serialLoopOnce();

  uint32_t now=millis();
  for (int i=0;i<NTASKS;i++){
    if (now - tasks[i].last >= tasks[i].period){
      tasks[i].last = now;
      tasks[i].fn();
    }
  }
}
