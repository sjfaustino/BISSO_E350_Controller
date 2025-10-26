/*
   ┌───────────────────────────────────────────────────────────────┐
   │   BISSO PROJECT — Industrial Motion & Process Controller      │
   │   BISSO_E350_Controller v0.4.6-R  (ESP32 / KC868-A16)         │
   │   Single-file unified build                                   │
   └───────────────────────────────────────────────────────────────┘
*/

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>

#if __has_include(<LiquidCrystal_I2C.h>)
  #include <LiquidCrystal_I2C.h>
#else
  #warning "LiquidCrystal_I2C.h not found! Install a compatible fork (e.g. John Rickman) via Library Manager."
  // Fallback shim to allow compile if header missing (no-op methods)
  class LiquidCrystal_I2C {
  public:
    LiquidCrystal_I2C(uint8_t, uint8_t, uint8_t) {}
    void init() {}
    void backlight() {}
    void clear() {}
    void setCursor(uint8_t, uint8_t) {}
    void print(const String&) {}
    void print(const char* s){ print(String(s)); }
  };
#endif

/*─────────────────────────────────────────────────────────────
  Versioning / Build metadata
─────────────────────────────────────────────────────────────*/
static const char* FW_NAME    = "BISSO_E350_Controller";
static const char* FW_VERSION = "v0.4.6-R";          // ← bumped
static const uint16_t CONFIG_SCHEMA_VERSION = 0x0460; // ← bumped
static const char* BUILD_DATE = __DATE__;
static const char* BUILD_TIME = __TIME__;

/*─────────────────────────────────────────────────────────────
  Safety: use macros (Arduino protopreprocessor-safe)
─────────────────────────────────────────────────────────────*/
#ifndef CLAMP
#define CLAMP(v, lo, hi) ( ((v) < (lo)) ? (lo) : ( ((v) > (hi)) ? (hi) : (v) ) )
#endif

/*─────────────────────────────────────────────────────────────
  Forward declarations (satisfy auto-prototyper)
─────────────────────────────────────────────────────────────*/
enum class AlarmCode : uint16_t {
  NONE=0, ESTOP=1, TEMP_WARN=2, TEMP_TRIP=3, SENSOR_FAULT=4,
  SOFTLIMIT=5, ENC_MISMATCH=6, TASK_TIMEOUT=7, SAFE_RESTART_BLOCK=8,
  OUTPUT_INTERLOCK=9
};
enum Axis : uint8_t { AX_X=0, AX_Y=1, AX_Z=2, AX_A=3 };
enum class State : uint8_t { IDLE=0, RUN, ERROR, DIAGNOSTICS, SELF_TEST, CALIB };

struct Move { Axis axis; float targetAbs; float feed; };
struct Cal { float gain, offset; };
struct Config {
  uint16_t schema;
  uint16_t reserved;
  uint32_t debounce_ms;
  float temp_warn_C, temp_trip_C;
  Cal   cal[4];                      // ADC linearization (gain/offset)
  float softMin[4], softMax[4];      // per-axis software limits
  uint8_t out1_addr, out2_addr;      // PCF8574 outputs
  uint8_t in1_addr,  in2_addr;       // PCF8574 inputs
  uint8_t lcd_addr;                  // LCD I2C address
  bool output_active_low;            // if true, invert relays (we use active-HIGH per project)
};

static const int ADC_NUM_CHANNELS = 4;

/*─────────────────────────────────────────────────────────────
  Hardware pins / I2C addresses (defaults can be overridden by config)
─────────────────────────────────────────────────────────────*/
// KC868-A16 I2C expanders (confirmed)
static uint8_t ADDR_OUT1 = 0x24; // Y01..Y08
static uint8_t ADDR_OUT2 = 0x25; // Y09..Y16
static uint8_t ADDR_IN1  = 0x21; // X01..X08
static uint8_t ADDR_IN2  = 0x22; // X09..X16
static uint8_t ADDR_LCD  = 0x27; // 20x4 LCD

// UART2 (WJ66) — 9600 8N1; pins from project mapping
// HT2 → GPIO33 (RX), HT1 → GPIO14 (TX), HT3=GPIO32 (aux, kept input)
static const int WJ66_RX = 33;
static const int WJ66_TX = 14;
static const int WJ66_AUX = 32; // reserved / not used (input)

// Heartbeat LED (any available GPIO, KC868 onboard blue LED often GPIO2)
static const int PIN_HEARTBEAT = 2;

// Local inputs for buttons/switches (board-level)
static const int PIN_START_BTN = 35;   // pulled-up/filtered externally as needed
static const int PIN_ONOFF_SW  = 34;   // ON/OFF permissive switch
static const int PIN_ESTOP_IN  = 36;   // ESTOP input (active-HIGH logic expected via opto/level)

// ADC channels (example mapping; adjust to your board analog pins)
static const int ADC_PINS[ADC_NUM_CHANNELS] = { 39, 36, 34, 35 }; // read-only ADC pins on ESP32 (ensure free)

/*─────────────────────────────────────────────────────────────
  LCD
─────────────────────────────────────────────────────────────*/
LiquidCrystal_I2C lcd(ADDR_LCD, 20, 4);

/*─────────────────────────────────────────────────────────────
  Globals
─────────────────────────────────────────────────────────────*/
Preferences prefs;
Config cfg;

// Motion queue
static const int QMAX = 64;
Move q[QMAX];
volatile int qHead=0, qTail=0, qCnt=0;

// Output states (cached mirror of PCF bytes)
uint8_t outState1 = 0x00;  // Y01..Y08
uint8_t outState2 = 0x00;  // Y09..Y16

// Input cache (updated in polling)
uint8_t inState1 = 0x00;   // X01..X08
uint8_t inState2 = 0x00;   // X09..X16

// State machine
volatile State state = State::IDLE;

// Alarm log
struct AlarmEntry { uint32_t t; AlarmCode code; int16_t detail; };
static const uint8_t MAX_ALARMS=32;
AlarmEntry alarms[MAX_ALARMS];
uint8_t alarmHead=0, alarmCount=0;

// Timers
uint32_t hbT=0;
uint32_t displayT=0;

// START long-press for DIAG/SELFTEST
uint32_t startPressT=0;
bool startHeldDiag=false;

// WJ66 encoder interface
struct {
  long pos[4] = {0,0,0,0};
  uint32_t frames=0, parsed=0;
  uint32_t lastUpdateMs=0;
  uint32_t staleHits=0;
} wj66;

// Live encoder delta bar (RUN screen)
uint16_t deltaMag[4] = {0,0,0,0};

// Journal / SPIFFS rotation (added in 0.4.6-R)
static const char* JPATH = "/journal.txt";
static const char* JPATH_OLD = "/journal.1";
const size_t JOURNAL_MAX_BYTES = 128UL * 1024UL; // ~128KB
bool journalInitDone=false;

// Run-hours counter (persisted)
uint64_t run_ms_accum = 0;
uint32_t run_last_start_ms = 0;
uint32_t run_saveT = 0;

// Last move estimate (ms)
uint32_t lastMoveEst_ms = 0;

// Direction interlock timing
uint32_t lastDirChangeT = 0;
int8_t lastDirState = -1; // -1 none, 0 NEG, 1 POS

// WJ66 health thresholds
const uint8_t WJ66_OK_PCT = 90;

// Config export/import paths
static const char* CFG_EXPORT_PATH = "/config.json";

/*─────────────────────────────────────────────────────────────
  Function prototypes (so Arduino doesn't reorder things oddly)
─────────────────────────────────────────────────────────────*/
void journalInit();
void journalLog(const char* level, const char* msg);
void journalTailPrint(size_t N);
void journalRotateIfNeeded();

const char* alarmToStr(AlarmCode c);
const char* alarmHelpStr(AlarmCode c);
void alarmPush(AlarmCode code, int16_t detail);

void outputsIdle();
void setYIndex(uint8_t idx, bool on);
void setSpeedBits(float feed);
void setDirBits(bool dir_pos);
void setAxisBits(Axis a);

bool pcfReadByte(uint8_t addr, uint8_t &val);
void pcfWrite(uint8_t addr, uint8_t value);
uint8_t pcfReadInputs(uint8_t addr);

bool qPush(const Move &m);
bool qPop(Move &m);
bool enqueueAxisMove(Axis a, float targetAbs, float feed);

void wj66Poll();
uint8_t wj66GoodPct();

void lcdPrintLine(uint8_t row, const String& s);
String byteBits(uint8_t b);
String bar10(uint16_t v);

/*─────────────────────────────────────────────────────────────
  Config helpers / defaults / load-save
─────────────────────────────────────────────────────────────*/
void loadDefaultConfig(Config &c){
  c.schema = CONFIG_SCHEMA_VERSION;
  c.reserved = 0;
  c.debounce_ms = 30;
  c.temp_warn_C = 70.0f;
  c.temp_trip_C = 85.0f;
  for (int i=0;i<ADC_NUM_CHANNELS;i++){ c.cal[i].gain=1.0f; c.cal[i].offset=0.0f; }
  for (int a=0;a<4;a++){ c.softMin[a]=-1000.0f; c.softMax[a]=+1000.0f; }
  c.out1_addr=0x24; c.out2_addr=0x25;
  c.in1_addr=0x21;  c.in2_addr=0x22;
  c.lcd_addr=0x27;
  c.output_active_low=false;   // project uses active-HIGH relays
}

bool saveConfig(const Config &c){
  prefs.begin("bisso-e350", false);
  prefs.putUShort("schema", c.schema);
  prefs.putUShort("resv", c.reserved);
  prefs.putUInt("debounce", c.debounce_ms);
  prefs.putFloat("twarn", c.temp_warn_C);
  prefs.putFloat("ttrip", c.temp_trip_C);
  for (int i=0;i<ADC_NUM_CHANNELS;i++){
    char gk[8], oky[8]; snprintf(gk,8,"g%02d",i); snprintf(oky,8,"o%02d",i);
    prefs.putFloat(gk, c.cal[i].gain);
    prefs.putFloat(oky, c.cal[i].offset);
  }
  for (int a=0;a<4;a++){
    char mn[8], mx[8]; snprintf(mn,8,"mn%1d",a); snprintf(mx,8,"mx%1d",a);
    prefs.putFloat(mn, c.softMin[a]); prefs.putFloat(mx, c.softMax[a]);
  }
  prefs.putUChar("o1", c.out1_addr); prefs.putUChar("o2", c.out2_addr);
  prefs.putUChar("i1", c.in1_addr);  prefs.putUChar("i2", c.in2_addr);
  prefs.putUChar("lcd", c.lcd_addr);
  prefs.putBool("pol", c.output_active_low);
  prefs.putULong64("runms", run_ms_accum);
  prefs.end();
  return true;
}

bool loadConfig(){
  prefs.begin("bisso-e350", true);
  uint16_t sch = prefs.getUShort("schema",0);
  bool ok = (sch == CONFIG_SCHEMA_VERSION);
  prefs.end();

  if(ok){
    prefs.begin("bisso-e350", true);
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
    cfg.in1_addr=prefs.getUChar("i1",0x21);  cfg.in2_addr=prefs.getUChar("i2",0x22);
    cfg.lcd_addr=prefs.getUChar("lcd",0x27);
    cfg.output_active_low=prefs.getBool("pol",false);
    run_ms_accum = prefs.getULong64("runms", 0ULL);
    prefs.end();
  } else {
    loadDefaultConfig(cfg);
    saveConfig(cfg);
  }

  ADDR_OUT1=cfg.out1_addr; ADDR_OUT2=cfg.out2_addr;
  ADDR_IN1=cfg.in1_addr;   ADDR_IN2=cfg.in2_addr;
  ADDR_LCD=cfg.lcd_addr;
  return ok;
}

void cfgResetToDefaults(){
  loadDefaultConfig(cfg);
  saveConfig(cfg);
  journalLog("INFO","CFG_DEFAULTS_APPLIED");
}

/*─────────────────────────────────────────────────────────────
  SPIFFS journal with rotation + viewer
─────────────────────────────────────────────────────────────*/
void journalRotateIfNeeded() {
  File f = SPIFFS.open(JPATH, FILE_READ);
  if (!f) return;
  size_t sz = f.size(); f.close();
  if (sz < JOURNAL_MAX_BYTES) return;
  SPIFFS.remove(JPATH_OLD);
  SPIFFS.rename(JPATH, JPATH_OLD);
  File nf = SPIFFS.open(JPATH, FILE_WRITE);
  if (nf) nf.close();
}

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
  journalRotateIfNeeded();
}

void journalTailPrint(size_t N){
  File f = SPIFFS.open(JPATH, FILE_READ);
  if(!f){ Serial.println(F("# no journal")); return; }
  size_t sz=f.size();
  std::unique_ptr<char[]> buf(new char[sz+1]);
  if(!buf){ Serial.println(F("# mem err")); f.close(); return; }
  f.readBytes(buf.get(), sz); buf[sz]=0; f.close();

  size_t lines=0; for(size_t i=0;i<sz;i++) if(buf[i]=='\n') lines++;
  if (N==0 || N>lines) N=lines;
  size_t toSkip = lines - N;
  size_t i=0, seen=0;
  while (i<sz && seen<toSkip){ if(buf[i]=='\n') seen++; i++; }
  Serial.write(&buf[i], sz - i);
}

/*─────────────────────────────────────────────────────────────
  Low-level I2C IO for PCF8574 expanders
─────────────────────────────────────────────────────────────*/
bool pcfReadByte(uint8_t addr, uint8_t &val){
  Wire.requestFrom((int)addr,1);
  if (Wire.available()){ val = Wire.read(); return true; }
  return false;
}

// write with verification attempts
void pcfWrite(uint8_t addr, uint8_t value){
  for(int attempt=0; attempt<3; ++attempt){
    Wire.beginTransmission(addr);
    Wire.write(value);
    uint8_t rc = Wire.endTransmission();
    if (rc==0){
      delay(2);
      uint8_t r=0;
      if (pcfReadByte(addr, r)){
        if ((addr==ADDR_OUT1 && r==outState1) || (addr==ADDR_OUT2 && r==outState2) || (addr!=ADDR_OUT1 && addr!=ADDR_OUT2)){
          return; // verified (or not an output bank)
        }
      } else {
        return; // can't read back, but write acked—accept
      }
    }
    delay(2);
  }
  journalLog("ERROR","PCF_WRITE_VERIFY_FAIL");
  alarmPush(AlarmCode::SENSOR_FAULT, (int16_t)addr);
}

uint8_t pcfReadInputs(uint8_t addr){
  uint8_t v=0x00; pcfReadByte(addr, v); return v;
}

/*─────────────────────────────────────────────────────────────
  Output bit helpers (active-HIGH relay logic)
  Y01..Y08 → outState1 bit0..bit7
  Y09..Y16 → outState2 bit0..bit7
─────────────────────────────────────────────────────────────*/
static inline void OUT1_set(uint8_t bit, bool on){
  if (on) outState1 |=  (1u<<bit); else outState1 &= ~(1u<<bit);
  pcfWrite(ADDR_OUT1, outState1);
}
static inline void OUT2_set(uint8_t bit, bool on){
  if (on) outState2 |=  (1u<<bit); else outState2 &= ~(1u<<bit);
  pcfWrite(ADDR_OUT2, outState2);
}

// Named signals
#define Y_FAST(b)        OUT1_set(0,(b))  // Y01
#define Y_MED(b)         OUT1_set(1,(b))  // Y02
#define Y_AX_X(b)        OUT1_set(2,(b))  // Y03
#define Y_AX_Y(b)        OUT1_set(3,(b))  // Y04
#define Y_AX_Z(b)        OUT1_set(4,(b))  // Y05
#define Y_AX_A(b)        OUT1_set(5,(b))  // Y06
#define Y_DIR_POS(b)     OUT1_set(6,(b))  // Y07
#define Y_DIR_NEG(b)     OUT1_set(7,(b))  // Y08
#define Y_VS(b)          OUT2_set(0,(b))  // Y09 (spindle)

// Inputs (cached reads)
#define X_IN_LO()        (inState1)       // X01..X08
#define X_IN_HI()        (inState2)       // X09..X16
// Convenience selectors for modes
#define X_SEL_X()        ((X_IN_LO() & 0x01)!=0)  // X01 → "T"
#define X_SEL_Y()        ((X_IN_LO() & 0x02)!=0)  // X02 → "C"
#define X_SEL_XY()       ((X_IN_LO() & 0x04)!=0)  // X03 → "C+T"
#define X_AUTO()         ((X_IN_LO() & 0x08)!=0)  // X04 → AUTO gate

/*─────────────────────────────────────────────────────────────
  High-level output helpers
─────────────────────────────────────────────────────────────*/
void outputsIdle(){
  Y_FAST(false); Y_MED(false);
  Y_AX_X(false); Y_AX_Y(false); Y_AX_Z(false); Y_AX_A(false);
  Y_DIR_POS(false); Y_DIR_NEG(false);
  Y_VS(false);
  journalLog("INFO","OUTPUTS_IDLE");
}

void setYIndex(uint8_t idx, bool on){
  // idx: 1..9 (we only drive up to Y09 here)
  if (idx>=1 && idx<=8) OUT1_set(idx-1, on);
  else if (idx==9) OUT2_set(0, on);
}

// Enforce direction interlock with dead-time (100 ms)
void setDirBits(bool dir_pos){
  int8_t want = dir_pos ? 1 : 0;
  uint32_t now = millis();
  if (lastDirState != -1 && want != lastDirState){
    Y_DIR_POS(false); Y_DIR_NEG(false);
    if (now - lastDirChangeT < 100) return; // dead-time window
  }
  Y_DIR_POS(false); Y_DIR_NEG(false);
  if (dir_pos) Y_DIR_POS(true); else Y_DIR_NEG(true);
  lastDirChangeT = now; lastDirState = want;
}

// Speed relays with hysteresis and default-medium behavior
void setSpeedBits(float feed) {
  static uint8_t lastMode = 0; // 0=idle, 1=medium, 2=fast
  Y_FAST(false); Y_MED(false);
  uint8_t mode = 0;
  if (feed <= 0.0f) mode = 1;
  else if (feed <= 300.0f) mode = 1;
  else if (feed >= 600.0f) mode = 2;
  else mode = lastMode;

  if (mode == 1){ Y_MED(true);  if (lastMode!=1) journalLog("INFO","SPEED_SET_MEDIUM"); lastMode=1; }
  else if (mode == 2){ Y_FAST(true); if (lastMode!=2) journalLog("INFO","SPEED_SET_FAST"); lastMode=2; }
  else { lastMode = 0; }
}

// Axis select relays
void setAxisBits(Axis a){
  Y_AX_X(false); Y_AX_Y(false); Y_AX_Z(false); Y_AX_A(false);
  switch(a){
    case AX_X: Y_AX_X(true); break;
    case AX_Y: Y_AX_Y(true); break;
    case AX_Z: Y_AX_Z(true); break;
    case AX_A: Y_AX_A(true); break;
  }
}

/*─────────────────────────────────────────────────────────────
  Alarm handling
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

const char* alarmHelpStr(AlarmCode c){
  switch(c){
    case AlarmCode::ESTOP: return "E-STOP active";
    case AlarmCode::TEMP_WARN: return "Temp high";
    case AlarmCode::TEMP_TRIP: return "Temp trip!";
    case AlarmCode::SENSOR_FAULT: return "Sensor/IO fault";
    case AlarmCode::SOFTLIMIT: return "Soft-limit hit";
    case AlarmCode::ENC_MISMATCH: return "Encoder stalled";
    case AlarmCode::TASK_TIMEOUT: return "Task rejected";
    case AlarmCode::SAFE_RESTART_BLOCK: return "Unsafe on boot";
    case AlarmCode::OUTPUT_INTERLOCK: return "Dir interlock";
    default: return "";
  }
}

void alarmPush(AlarmCode code, int16_t detail){
  alarms[alarmHead] = { millis(), code, detail };
  alarmHead = (alarmHead+1)%MAX_ALARMS;
  if (alarmCount<MAX_ALARMS) alarmCount++;
  char line[128]; snprintf(line,sizeof(line),"ALARM %s detail=%d", alarmToStr(code), (int)detail);
  journalLog("ERROR", line);
}

/*─────────────────────────────────────────────────────────────
  ADC helpers (simple, linearized)
─────────────────────────────────────────────────────────────*/
uint16_t adcReadRaw(int i){
  int v = analogRead(ADC_PINS[i]);
  return (uint16_t)CLAMP(v, 0, 4095);
}
float adcReadLinearized(int i){
  float r = (float)adcReadRaw(i);
  return r * cfg.cal[i].gain + cfg.cal[i].offset;
}

// Mock temperature (from CH1 for demo purposes); replace with sensor model
float mockTemperatureC(){
  float v = adcReadLinearized(0);
  // map 4–20mA → 0–100C as example (fake)
  return CLAMP((v/4095.0f)*100.0f, 0.0f, 120.0f);
}

/*─────────────────────────────────────────────────────────────
  LCD helpers
─────────────────────────────────────────────────────────────*/
void lcdPrintLine(uint8_t row, const String& s){
  static char prev[4][21]; // 20 chars + 0
  String t = s;
  while (t.length() < 20) t += ' ';
  t = t.substring(0,20);
  if (strncmp(prev[row], t.c_str(), 20)!=0){
    lcd.setCursor(0,row); lcd.print(t);
    strncpy(prev[row], t.c_str(), 21);
  }
}

String byteBits(uint8_t b){
  String s;
  for(int i=7;i>=0;i--) s += (b&(1<<i)) ? '1':'0';
  return s;
}
String bar10(uint16_t v){
  // v scaled 0..1000 → 10 chars
  uint8_t n = (uint8_t)CLAMP((int)(v/100), 0, 10);
  String s; for(uint8_t i=0;i<10;i++) s += (i<n)? '#':'-';
  return s;
}
/*─────────────────────────────────────────────────────────────
  Motion queue
─────────────────────────────────────────────────────────────*/
bool qPush(const Move &m){
  if (qCnt>=QMAX) { journalLog("WARN","Q_FULL"); return false; }
  q[qTail]=m; qTail=(qTail+1)%QMAX; qCnt++; return true;
}
bool qPop(Move &m){
  if (qCnt==0) return false;
  m=q[qHead]; qHead=(qHead+1)%QMAX; qCnt--; return true;
}

/*─────────────────────────────────────────────────────────────
  WJ66 encoder UART parsing
  - Expects ASCII lines like: "X:123 Y:45 Z:0 A:-5"
  - Updates wj66.pos[], frames/parsed counters, stale detection
─────────────────────────────────────────────────────────────*/
uint8_t wj66GoodPct(){
  if (wj66.frames==0) return 0;
  uint32_t pct = (wj66.parsed*100UL)/wj66.frames;
  return (uint8_t)CLAMP((int)pct,0,100);
}

void wj66Poll(){
  static char line[96];
  static size_t len=0;

  while (Serial2.available()){
    char c = (char)Serial2.read();
    if (c=='\n' || c=='\r'){
      if (len>0){
        line[len]=0; len=0;
        wj66.frames++;
        // parse tokens
        long x=wj66.pos[0], y=wj66.pos[1], z=wj66.pos[2], a=wj66.pos[3];
        char *p=line;
        auto eat = [&](char axis)->bool{
          char *k = strchr(p, axis);
          if(!k) return false;
          char *colon = strchr(k, ':'); if(!colon) return false;
          long v = strtol(colon+1, nullptr, 10);
          switch(axis){ case 'X': x=v; break; case 'Y': y=v; break; case 'Z': z=v; break; case 'A': a=v; break; }
          return true;
        };
        bool ok=false;
        ok |= eat('X'); ok |= eat('Y'); ok |= eat('Z'); ok |= eat('A');
        if (ok){
          // compute deltas for bargraph
          for (int i=0;i<4;i++){
            long prev = wj66.pos[i];
            long nowv = (i==0)?x:(i==1)?y:(i==2)?z:a;
            long d = labs(nowv - prev);
            deltaMag[i] = (uint16_t)CLAMP((int)d, 0, 1000);
          }
          wj66.pos[0]=x; wj66.pos[1]=y; wj66.pos[2]=z; wj66.pos[3]=a;
          wj66.parsed++;
          wj66.lastUpdateMs = millis();
        }
      }
    } else {
      if (len<sizeof(line)-1) line[len++]=c;
    }
  }

  // stale check (1s)
  if (millis() - wj66.lastUpdateMs > 1000){
    wj66.staleHits++;
    wj66.lastUpdateMs = millis();
  }
}

/*─────────────────────────────────────────────────────────────
  Enqueue with soft-limit precheck + travel time estimation
─────────────────────────────────────────────────────────────*/
bool enqueueAxisMove(Axis a, float targetAbs, float feed){
  // soft-limit gate
  float mn = cfg.softMin[(int)a], mx = cfg.softMax[(int)a];
  if (targetAbs<mn || targetAbs>mx){
    alarmPush(AlarmCode::SOFTLIMIT, (int16_t)a);
    journalLog("WARN","SOFTLIMIT_REJECT");
    return false;
  }
  // estimate duration (units: mm/min assumed; you can rescale if needed)
  long here = wj66.pos[(int)a];
  float delta = fabsf(targetAbs - (float)here);
  float feed_mm_per_min = (feed>0.0f) ? feed : 300.0f; // default MED
  float feed_mm_per_s   = feed_mm_per_min / 60.0f;
  lastMoveEst_ms = (feed_mm_per_s>0.0f) ? (uint32_t)((delta / feed_mm_per_s) * 1000.0f) : 0;

  Move m{a, targetAbs, feed};
  return qPush(m);
}

/*─────────────────────────────────────────────────────────────
  Motion executor
  - Single-axis moves only (parser enforces)
  - Uses encoder position as feedback (coarse)
  - Declares simple completion when within threshold OR encoder delta stalls
─────────────────────────────────────────────────────────────*/
void motionTask(){
  static bool busy=false;
  static Move cur;
  static uint32_t startT=0;
  static long startEnc=0;

  if (!busy){
    if (!qPop(cur)) return;
    busy=true; startT=millis();
    setAxisBits(cur.axis);

    // direction from current encoder position
    long posNow = wj66.pos[(int)cur.axis];
    startEnc = posNow;
    bool dirPos = (cur.targetAbs > (float)posNow);
    setDirBits(dirPos);
    setSpeedBits(cur.feed);
  }

  // monitor progress
  long posNow = wj66.pos[(int)cur.axis];
  float err = fabsf(cur.targetAbs - (float)posNow);

  // simple completion threshold
  const float POS_TOL = 1.0f; // encoder counts tolerance; adjust as needed
  const uint32_t MAX_MS = CLAMP((int)(lastMoveEst_ms*2), 500, 60000); // guard

  if (err <= POS_TOL){
    // reached
    outputsIdle();
    busy=false;
    return;
  }

  // mismatch guard: if no movement for too long
  if (millis() - startT > MAX_MS){
    long delta = labs(posNow - startEnc);
    if (delta < 1){
      alarmPush(AlarmCode::ENC_MISMATCH, (int16_t)cur.axis);
      state = State::ERROR;
      outputsIdle();
      busy=false;
      return;
    } else {
      // extend a bit more
      startT = millis();
      startEnc = posNow;
    }
  }
}

/*─────────────────────────────────────────────────────────────
  G-code parser
  - AUTO (X04) must be HIGH
  - Mode gating via X01/X02/X03 → T/C/C+T
  - Exactly one axis allowed per line
  - Default feed → MEDIUM if not specified
  - M3/M5/M30 handled
─────────────────────────────────────────────────────────────*/
void parseGcodeLine(const String& line) {
  // AUTO gate
  if (!X_AUTO()) { journalLog("WARN","GCODE_REJECT auto=LOW"); return; }

  char buf[128];
  line.substring(0, sizeof(buf)-1).toCharArray(buf, sizeof(buf));
  char *p = buf;

  char gc=' '; int gnum=0, mnum=0; bool isM=false;
  while (*p==' ') p++;
  if (*p=='G'){ isM=false; gc='G'; p++; gnum=strtol(p,&p,10); }
  else if (*p=='M'){ isM=true;  gc='M'; p++; mnum=strtol(p,&p,10); }
  else return;

  float tgt[4] = {NAN,NAN,NAN,NAN};
  float fFeed = 0.0f;

  while (*p){
    while (*p==' ' || *p==',') p++;
    char c = toupper(*p);
    if (c=='X' || c=='Y' || c=='Z' || c=='A'){
      p++; float v=strtof(p,&p);
      int ai = (c=='X')?0:(c=='Y')?1:(c=='Z')?2:3;
      tgt[ai]=v;
    } else if (c=='F'){
      p++; fFeed=strtof(p,&p);
    } else {
      p++;
    }
  }

  // Determine allowed axes by mode
  bool modeC  = X_SEL_Y();   // C (Y,Z,F)
  bool modeT  = X_SEL_X();   // T (X,Z,F)
  bool modeCT = X_SEL_XY();  // C+T (X,Y,Z,F)
  bool allowX=false, allowY=false, allowZ=true; // Z always allowed
  if (modeC)  allowY=true;
  if (modeT)  allowX=true;
  if (modeCT) { allowX=true; allowY=true; }
  if (!modeC && !modeT && !modeCT){
    journalLog("WARN","NO_MODE_SELECTED");
    alarmPush(AlarmCode::TASK_TIMEOUT, 0);
    return;
  }

  // axis count
  int axisCount=0; for(int a=0;a<4;a++) if(!isnan(tgt[a])) axisCount++;
  if (axisCount==0){ journalLog("WARN","NO_AXIS_SPECIFIED"); return; }
  if (axisCount>1){ journalLog("WARN","MULTI_AXIS_REJECTED"); alarmPush(AlarmCode::TASK_TIMEOUT, axisCount); return; }

  // resolve axis/target and permission
  Axis ax=AX_X; float target=0.0f;
  if (!isnan(tgt[0])){ ax=AX_X; target=tgt[0]; if(!allowX){ journalLog("WARN","X_NOT_ALLOWED"); return; } }
  if (!isnan(tgt[1])){ ax=AX_Y; target=tgt[1]; if(!allowY){ journalLog("WARN","Y_NOT_ALLOWED"); return; } }
  if (!isnan(tgt[2])){ ax=AX_Z; target=tgt[2]; if(!allowZ){ journalLog("WARN","Z_NOT_ALLOWED"); return; } }

  // default feed → medium
  if (fFeed<=0.0f){ fFeed=300.0f; journalLog("INFO","FEED_DEFAULT_MEDIUM"); }

  if (!isM){
    if (gnum==0 || gnum==1){
      enqueueAxisMove(ax, target, fFeed);
      journalLog("INFO","GCODE_SINGLE_AXIS_OK");
    }
  } else {
    switch (mnum){
      case 3:  Y_VS(true);  journalLog("INFO","M3 SPINDLE ON"); break;
      case 5:  Y_VS(false); journalLog("INFO","M5 SPINDLE OFF"); break;
      case 30: outputsIdle(); journalLog("INFO","M30 PROGRAM END"); break;
      default: break;
    }
  }
}
/*─────────────────────────────────────────────────────────────
  Simple GPIO debouncer for local button/switch/estop
─────────────────────────────────────────────────────────────*/
struct Debounce {
  int pin=-1; bool invert=false; uint32_t dbms=30;
  bool last=false, stable=false; uint32_t lastT=0;
  bool roseFlag=false;

  void begin(int p, bool inv, uint32_t ms){
    pin=p; invert=inv; dbms=ms; pinMode(pin, invert?INPUT_PULLUP:INPUT);
    bool r = raw(); stable=r; last=r; lastT=millis(); roseFlag=false;
  }
  bool raw(){ int v=digitalRead(pin); return invert? !v : v; }
  void poll(){
    bool r = raw();
    if (r!=last){ last=r; lastT=millis(); }
    if (millis()-lastT>=dbms){
      bool prev=stable; stable=last;
      if (!prev && stable) roseFlag=true;
    }
  }
  bool read() const { return stable; }
  bool rose(){ bool f=roseFlag; roseFlag=false; return f; }
};

Debounce btnStart, swOnOff, inEstop;

/*─────────────────────────────────────────────────────────────
  LCD pages
─────────────────────────────────────────────────────────────*/
void showIdle(){
  lcdPrintLine(0, String(FW_NAME)+" "+String(FW_VERSION));
  lcdPrintLine(1, "Press START to Run");
  String flags = String("AUTO:")+String(X_AUTO()?"1":"0")+" ESTOP:"+String(inEstop.read()?"1":"0");
  lcdPrintLine(2, flags);
  lcdPrintLine(3, "");
}

void showRun(){
  String icon = (wj66GoodPct()>=WJ66_OK_PCT) ? "(OK)" : "(!)";
  String l1 = "RUN  AUTO:" + String(X_AUTO()?"1":"0") + " " + icon;
  String l2 = "Q:" + String(qCnt) + " T:" + String(mockTemperatureC(),1) + "C";
  String l3 = "X[" + bar10(deltaMag[0]) + "] Y[" + bar10(deltaMag[1]) + "]";
  lcdPrintLine(0, l1);
  lcdPrintLine(1, l2);
  lcdPrintLine(2, l3);
  lcdPrintLine(3, "Z[" + bar10(deltaMag[2]) + "] A[" + bar10(deltaMag[3]) + "]");
}

void showError(){
  lcdPrintLine(0,"*** ERROR ***");
  if (alarmCount){
    uint8_t idx = (alarmHead + MAX_ALARMS - 1) % MAX_ALARMS;
    AlarmCode ac = alarms[idx].code;
    String s = String(alarmToStr(ac)) + " d=" + String(alarms[idx].detail);
    lcdPrintLine(1, s);
    lcdPrintLine(2, String("Hint: ") + alarmHelpStr(ac));
  } else {
    lcdPrintLine(1,"No alarms logged");
    lcdPrintLine(2,"");
  }
  lcdPrintLine(3,"Press START to idle");
}

String byteBits16(uint16_t w){
  String a = byteBits((uint8_t)(w>>8));
  String b = byteBits((uint8_t)(w&0xFF));
  return a + "/" + b;
}

void showDiagnostics(){
  uint8_t xin1 = X_IN_LO(); uint8_t xin2 = X_IN_HI();
  lcdPrintLine(0, "DIAGNOSTICS");
  lcdPrintLine(1, "X01-08:"+byteBits(xin1));
  lcdPrintLine(2, "X09-16:"+byteBits(xin2));
  String icon = (wj66GoodPct()>=WJ66_OK_PCT) ? "(OK)" : "(!)";
  lcdPrintLine(3, "WJ66 ok%:"+String((int)wj66GoodPct())+" "+icon+" st:"+String(wj66.staleHits));
}

void showSelfTest(uint8_t idx){
  lcdPrintLine(0,"SELF-TEST");
  lcdPrintLine(1,"Walking relays");
  lcdPrintLine(2, String("Active: Y") + String((int)idx,10));
  lcdPrintLine(3,"Press START -> DIAG");
}

void showCalib(){
  lcdPrintLine(0,"CALIBRATION");
  lcdPrintLine(1,"Use selectors X/Y");
  lcdPrintLine(2,"Z allowed always");
  lcdPrintLine(3,"CLI 'cal' to exit");
}

/*─────────────────────────────────────────────────────────────
  Tasks
─────────────────────────────────────────────────────────────*/
void task_wj66(){ wj66Poll(); }

void task_sensorSanity(){
  for (int i=0;i<ADC_NUM_CHANNELS;i++){
    float L=adcReadLinearized(i);
    if (L<-50.0f || L>5000.0f) alarmPush(AlarmCode::SENSOR_FAULT,i);
  }
}

void task_housekeeping(){
  if (millis()-hbT>500){ hbT=millis(); digitalWrite(PIN_HEARTBEAT, !digitalRead(PIN_HEARTBEAT)); }

  // Temperature checks
  float tC = mockTemperatureC();
  if (tC >= cfg.temp_trip_C){
    state=State::ERROR; outputsIdle(); alarmPush(AlarmCode::TEMP_TRIP, (int16_t)tC);
  } else if (tC >= cfg.temp_warn_C){
    alarmPush(AlarmCode::TEMP_WARN, (int16_t)tC);
  }

  // Run-hour accumulation & periodic persist
  if (state==State::RUN){
    if (run_last_start_ms==0) run_last_start_ms = millis();
    if (millis() - run_saveT > 30000){
      uint32_t now = millis();
      if (run_last_start_ms) run_ms_accum += (uint64_t)(now - run_last_start_ms);
      run_last_start_ms = now;
      prefs.begin("bisso-e350", false);
      prefs.putULong64("runms", run_ms_accum);
      prefs.end();
      run_saveT = millis();
    }
  }
}

void task_display(){
  if (millis()-displayT < 200) return;
  displayT = millis();

  switch(state){
    case State::IDLE:         showIdle(); break;
    case State::RUN:          showRun(); break;
    case State::ERROR:        showError(); break;
    case State::DIAGNOSTICS:  showDiagnostics(); break;
    case State::SELF_TEST:    /* UI drawn in task_selftest */ break;
    case State::CALIB:        showCalib(); break;
  }
}

void task_motion_wrap(){ if (state==State::RUN) motionTask(); }

/*─────────────────────────────────────────────────────────────
  Inputs poll (PCF8574 + local buttons)
─────────────────────────────────────────────────────────────*/
void refreshInputs(){
  inState1 = pcfReadInputs(ADDR_IN1);
  inState2 = pcfReadInputs(ADDR_IN2);
}

void task_pollInputs(){
  // poll local GPIO
  btnStart.poll(); swOnOff.poll(); inEstop.poll();

  // update PCF inputs
  refreshInputs();

  // Error latch management
  if (state==State::ERROR){
    // Clear error on START press if ESTOP released and ON/OFF ON
    if (btnStart.rose() && !inEstop.read()){
      state = State::IDLE;
      journalLog("INFO","ERROR_CLEARED");
    }
    return;
  }

  // DIAGNOSTICS enter via long-press START >=5s from IDLE/RUN
  if (btnStart.read()){
    if (startPressT==0) startPressT=millis();
    if (!startHeldDiag && millis()-startPressT>=5000 && (state==State::IDLE || state==State::RUN)){
      state=State::DIAGNOSTICS; startHeldDiag=true;
      journalLog("INFO","ENTER_DIAG");
    }
  } else { startPressT=0; startHeldDiag=false; }

  // Transitions
  if (state==State::IDLE && swOnOff.read() && btnStart.rose()){
    state=State::RUN; journalLog("INFO","ENTER_RUN");
    run_last_start_ms = millis();
  }
  if (state==State::RUN && !swOnOff.read()){
    uint32_t now = millis();
    if (run_last_start_ms) run_ms_accum += (uint64_t)(now - run_last_start_ms);
    run_last_start_ms = 0;
    prefs.begin("bisso-e350", false); prefs.putULong64("runms", run_ms_accum); prefs.end();
    state=State::IDLE; outputsIdle(); journalLog("INFO","LEAVE_RUN");
  }

  if (inEstop.read()){
    state=State::ERROR; outputsIdle(); alarmPush(AlarmCode::ESTOP,0);
  }

  // DIAG → IDLE on short press
  if (state==State::DIAGNOSTICS && btnStart.rose()){
    state=State::IDLE; journalLog("INFO","LEAVE_DIAG");
  }
}

/*─────────────────────────────────────────────────────────────
  SELF-TEST task (relay walk)
─────────────────────────────────────────────────────────────*/
void task_selftest(){
  static uint8_t idx=1; static uint32_t t=0;
  if (state!=State::SELF_TEST) return;

  if (millis()-t>400){
    t=millis();
    outputsIdle();
    setYIndex(idx, true);
    showSelfTest(idx);
    idx++; if (idx>9) idx=1;
  }
  if (btnStart.rose()){ state=State::DIAGNOSTICS; journalLog("INFO","SELFTEST_TO_DIAG"); }
}

/*─────────────────────────────────────────────────────────────
  Serial: line reader (shared for CLI and G-code)
─────────────────────────────────────────────────────────────*/
String serialReadLine(Stream &s){
  static String acc;
  while (s.available()){
    char c = (char)s.read();
    if (c=='\r') continue;
    if (c=='\n'){
      String out = acc; acc=""; return out;
    }
    acc += c;
    if (acc.length()>200) acc.remove(0, acc.length()-200);
  }
  return "";
}

/*─────────────────────────────────────────────────────────────
  CLI helpers
─────────────────────────────────────────────────────────────*/
void serialHelp(){
  Serial.println(F("# Commands:"));
  Serial.println(F("h           - help"));
  Serial.println(F("s           - show config"));
  Serial.println(F("io          - show i2c map/polarity"));
  Serial.println(F("set io <item> <hex>  (out1,out2,in1,in2,lcd,pol)"));
  Serial.println(F("x           - save config"));
  Serial.println(F("r           - clear alarms"));
  Serial.println(F("cal         - enter calibration mode"));
  Serial.println(F("version     - show firmware version and build date"));
  Serial.println(F("default     - restore factory defaults (and save)"));
  Serial.println(F("log [N]     - print last N journal lines (default all)"));
  Serial.println(F("cfg export  - write /config.json"));
  Serial.println(F("cfg import  - read  /config.json"));
  Serial.println(F("hours       - show accumulated RUN hours"));
  Serial.println(F("scan        - scan I2C bus for devices"));
  Serial.println(F("G/M lines   - G-code accepted only when AUTO=HIGH"));
}

void serialShow(){
  Serial.printf("schema=0x%04X debounce=%ums twarn=%.1fC ttrip=%.1fC\n",
    cfg.schema, (unsigned)cfg.debounce_ms, cfg.temp_warn_C, cfg.temp_trip_C);
  Serial.printf("soft X[%.1f..%.1f] Y[%.1f..%.1f] Z[%.1f..%.1f] A[%.1f..%.1f]\n",
    cfg.softMin[0],cfg.softMax[0], cfg.softMin[1],cfg.softMax[1],
    cfg.softMin[2],cfg.softMax[2], cfg.softMin[3],cfg.softMax[3]);
  Serial.printf("addr out1=0x%02X out2=0x%02X in1=0x%02X in2=0x%02X lcd=0x%02X pol=%d\n",
    cfg.out1_addr,cfg.out2_addr,cfg.in1_addr,cfg.in2_addr,cfg.lcd_addr,(int)cfg.output_active_low);
}

void serialIO(){
  Serial.printf("OUT1[0x%02X] OUT2[0x%02X]  IN1[0x%02X] IN2[0x%02X]  LCD[0x%02X]\n",
    ADDR_OUT1,ADDR_OUT2,ADDR_IN1,ADDR_IN2,ADDR_LCD);
  Serial.printf("outState1=0x%02X outState2=0x%02X  inState1=0x%02X inState2=0x%02X\n",
    outState1,outState2,inState1,inState2);
}

/*─────────────────────────────────────────────────────────────
  Config export/import (JSON-lite from Patch 9)
─────────────────────────────────────────────────────────────*/
bool cfgExportJSON();
bool cfgImportJSON();

/*─────────────────────────────────────────────────────────────
  Serial line handler — CLI & G-code
─────────────────────────────────────────────────────────────*/
void serialLoopOnce(){
  String line = serialReadLine(Serial);
  if (line.length()==0) return;

  String s = line; s.trim();
  if (s.length()==0) return;

  // G/M lines first
  if (s[0]=='G' || s[0]=='g' || s[0]=='M' || s[0]=='m'){
    parseGcodeLine(s);
    return;
  }

  // CLI
  s.toLowerCase();
  if (s=="h" || s=="help") serialHelp();
  else if (s=="s") serialShow();
  else if (s=="io") serialIO();
  else if (s.startsWith("set io ")){
    // set io <item> <hex>
    int sp1 = s.indexOf(' ', 7);
    if (sp1>0){
      String item = s.substring(7, sp1);
      String hex  = s.substring(sp1+1);
      uint8_t v = (uint8_t) strtoul(hex.c_str(), nullptr, 16);
      if (item=="out1"){ cfg.out1_addr=v; ADDR_OUT1=v; }
      else if (item=="out2"){ cfg.out2_addr=v; ADDR_OUT2=v; }
      else if (item=="in1"){ cfg.in1_addr=v; ADDR_IN1=v; }
      else if (item=="in2"){ cfg.in2_addr=v; ADDR_IN2=v; }
      else if (item=="lcd"){ cfg.lcd_addr=v; ADDR_LCD=v; }
      else if (item=="pol"){ cfg.output_active_low=(v!=0); }
      Serial.println(F("OK"));
    }
  }
  else if (s=="x"){ saveConfig(cfg); Serial.println(F("OK SAVED")); }
  else if (s=="r"){ alarmCount=0; Serial.println(F("OK ALARMS CLEARED")); }
  else if (s=="cal"){ state=State::CALIB; Serial.println(F("CALIB ENTER")); }
  else if (s=="version"){
    Serial.printf("%s %s (built %s %s)\n", FW_NAME, FW_VERSION, BUILD_DATE, BUILD_TIME);
  } else if (s=="default"){
    cfgResetToDefaults(); Serial.println(F("OK DEFAULTS"));
  } else if (s.startsWith("log")){
    int N=0; int sp = s.indexOf(' ');
    if (sp>0) N = s.substring(sp+1).toInt();
    journalTailPrint((size_t)N);
  } else if (s=="cfg export"){
    Serial.println(cfgExportJSON() ? F("OK EXPORTED /config.json") : F("ERR EXPORT"));
  } else if (s=="cfg import"){
    Serial.println(cfgImportJSON() ? F("OK IMPORTED /config.json") : F("ERR IMPORT"));
  } else if (s=="hours"){
    double hrs = (double)run_ms_accum / 3600000.0;
    Serial.printf("run_hours: %.3f h\n", hrs);
  } else if (s=="scan"){
    Serial.println(F("# I2C scan"));
    for (uint8_t addr=1; addr<127; addr++){
      Wire.beginTransmission(addr);
      uint8_t err = Wire.endTransmission();
      if (err==0) Serial.printf("  0x%02X\n", addr);
      delay(2);
    }
  } else {
    Serial.println(F("# unknown"));
  }
}

/*─────────────────────────────────────────────────────────────
  G-code over Serial2 (optional host)
─────────────────────────────────────────────────────────────*/
void serialGcodeFromSerial2(){
  String line = serialReadLine(Serial2);
  if (line.length()==0) return;
  parseGcodeLine(line);
}
/*─────────────────────────────────────────────────────────────
  Config export/import minimal JSON (ASCII)
─────────────────────────────────────────────────────────────*/
bool cfgExportJSON() {
  File f = SPIFFS.open(CFG_EXPORT_PATH, FILE_WRITE);
  if (!f) return false;
  f.printf("{\n");
  f.printf("  \"schema\": %u,\n", cfg.schema);
  f.printf("  \"debounce\": %u,\n", cfg.debounce_ms);
  f.printf("  \"twarn\": %.1f,\n", cfg.temp_warn_C);
  f.printf("  \"ttrip\": %.1f,\n", cfg.temp_trip_C);
  f.printf("  \"out1\": %u,\n", cfg.out1_addr);
  f.printf("  \"out2\": %u,\n", cfg.out2_addr);
  f.printf("  \"in1\": %u,\n", cfg.in1_addr);
  f.printf("  \"in2\": %u,\n", cfg.in2_addr);
  f.printf("  \"lcd\": %u,\n", cfg.lcd_addr);
  f.printf("  \"pol\": %u,\n", cfg.output_active_low ? 1 : 0);
  for (int a=0;a<4;a++)
    f.printf("  \"softMin%d\": %.2f,\n", a, cfg.softMin[a]);
  for (int a=0;a<4;a++)
    f.printf("  \"softMax%d\": %.2f,\n", a, cfg.softMax[a]);
  f.printf("  \"run_ms\": %llu\n", (unsigned long long)run_ms_accum);
  f.printf("}\n");
  f.close();
  return true;
}

bool cfgImportJSON() {
  File f = SPIFFS.open(CFG_EXPORT_PATH, FILE_READ);
  if (!f) return false;
  String s; while (f.available()) s += (char)f.read();
  f.close();
  auto findF = [&](const char* key)->float{ int p=s.indexOf(key); if(p<0)return NAN;
    int c=s.indexOf(':',p); if(c<0)return NAN; return s.substring(c+1).toFloat(); };
  cfg.debounce_ms = (uint32_t)findF("debounce");
  cfg.temp_warn_C = findF("twarn");
  cfg.temp_trip_C = findF("ttrip");
  cfg.out1_addr = (uint8_t)findF("out1");
  cfg.out2_addr = (uint8_t)findF("out2");
  cfg.in1_addr  = (uint8_t)findF("in1");
  cfg.in2_addr  = (uint8_t)findF("in2");
  cfg.lcd_addr  = (uint8_t)findF("lcd");
  cfg.output_active_low = (findF("pol")>0.5f);
  for (int a=0;a<4;a++){ char k1[16],k2[16]; sprintf(k1,"softMin%d",a); sprintf(k2,"softMax%d",a);
    cfg.softMin[a]=findF(k1); cfg.softMax[a]=findF(k2); }
  run_ms_accum = (uint64_t)findF("run_ms");
  saveConfig(cfg);
  return true;
}

/*─────────────────────────────────────────────────────────────
  Setup
─────────────────────────────────────────────────────────────*/
void setup(){
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, WJ66_RX, WJ66_TX);
  Wire.begin();
  pinMode(PIN_HEARTBEAT, OUTPUT);

  loadConfig();
  lcd = LiquidCrystal_I2C(ADDR_LCD,20,4);
  lcd.init(); lcd.backlight();
  lcd.clear();
  lcdPrintLine(0, String(FW_NAME)+" "+String(FW_VERSION));
  lcdPrintLine(1, "Booting...");
  delay(500);

  journalInit();
  outputsIdle();

  btnStart.begin(PIN_START_BTN,false,cfg.debounce_ms);
  swOnOff.begin(PIN_ONOFF_SW,false,cfg.debounce_ms);
  inEstop.begin(PIN_ESTOP_IN,false,cfg.debounce_ms);

  // Safe restart: if any critical input high, block run
  if (inEstop.read()) {
    alarmPush(AlarmCode::SAFE_RESTART_BLOCK,0);
    state=State::ERROR;
  }

  journalLog("INFO","SETUP_DONE");
}

/*─────────────────────────────────────────────────────────────
  Loop
─────────────────────────────────────────────────────────────*/
void loop(){
  serialLoopOnce();
  serialGcodeFromSerial2();

  task_pollInputs();
  task_motion_wrap();
  task_wj66();
  task_sensorSanity();
  task_housekeeping();
  task_selftest();
  task_display();

  delay(10);
}
