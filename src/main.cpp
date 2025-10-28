#include "globals.h"
#include "io.h"
#include "journal.h"
#include "config.h"
#include "wj66.h"
#include "motion.h"
#include "cli.h"
#include "lcd_ui.h"
#include "selftest.h"
#include "inputs.h"

State state = State::IDLE;
void onSystemError(AlarmCode code, int16_t detail){ (void)code; (void)detail; state=State::ERROR; }

static uint64_t run_ms_accum=0;
static uint32_t run_last_start_ms=0, lastInterlockVerifyMs=0, lastRuntimePersistMs=0;

void setup(){
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17); // WJ66
  journalInit();
  ioInit();
  loadConfig();
  inputsInit(cfg.debounce_ms);
  lcdInit();
  wj66Init();
  motionInit();
  cliInit();
  journalLog("INFO","BOOT_OK");
}

static void task_stateAndSafety(){
  if(state==State::ERROR && btnStartRose()){ state=State::IDLE; outputsIdle(); }
  if(state==State::IDLE && swOnOffRead() && btnStartRose()){ state=State::RUN; run_last_start_ms=millis(); journalLog("INFO","ENTER_RUN"); }
  if(state==State::RUN && !swOnOffRead()){
    state=State::IDLE; run_last_start_ms=0; cfg.run_ms_total += run_ms_accum; run_ms_accum=0; saveConfig(cfg);
    journalLog("INFO","LEAVE_RUN_SAVE");
  }
  if(inEstopRead()){ outputsIdle(); alarmPush(AlarmCode::ESTOP,0); state=State::ERROR; run_last_start_ms=0; }
}

static void task_housekeeping(){
  if(state==State::RUN && run_last_start_ms!=0){
    uint32_t now=millis(); uint32_t delta=now-run_last_start_ms; run_ms_accum+=delta; run_last_start_ms=now;
  }
  float tC=mockTemperatureC();
  if(tC>=cfg.temp_trip_C){ outputsIdle(); alarmPush(AlarmCode::TEMP_TRIP,(int16_t)tC); onSystemError(AlarmCode::TEMP_TRIP,(int16_t)tC); }

  if(millis()-lastInterlockVerifyMs>5000){
    lastInterlockVerifyMs=millis();
    if(Y_DIR_POS_STATE()&&Y_DIR_NEG_STATE()){ alarmPush(AlarmCode::OUTPUT_INTERLOCK,1); outputsIdle(); onSystemError(AlarmCode::OUTPUT_INTERLOCK,1); }
  }

  journalFlushToFS(false);
  if(millis()-lastRuntimePersistMs>600000UL){
    lastRuntimePersistMs=millis(); cfg.run_ms_total += run_ms_accum; run_ms_accum=0; saveConfig(cfg); journalLog("INFO","RUNTIME_SAVED");
  }
}

static void task_calibration(){
  static uint8_t axisSel=0; static uint32_t lastDisp=0,lastAdj=0,exitHoldT=0,offHoldT=0;
  if(X_SEL_XY()) axisSel=2; else if(X_SEL_Y()) axisSel=1; else if(X_SEL_X()) axisSel=0;

  if(btnStartRose() && millis()-lastAdj>300){ cfg.cal[axisSel].gain=clampT(cfg.cal[axisSel].gain+0.01f,0.1f,10.0f); lastAdj=millis(); }
  if(swOnOffRead() && millis()-lastAdj>300){ cfg.cal[axisSel].gain=clampT(cfg.cal[axisSel].gain-0.01f,0.1f,10.0f); lastAdj=millis(); }

  if(btnStartRead()){
    if(!offHoldT) offHoldT=millis();
    else if(millis()-offHoldT>1000 && millis()-lastAdj>300){ cfg.cal[axisSel].offset=clampT(cfg.cal[axisSel].offset+0.001f,-5.0f,5.0f); lastAdj=millis(); }
  } else offHoldT=0;

  if(btnStartRead()){
    if(!exitHoldT) exitHoldT=millis();
    if(millis()-exitHoldT>3000){ saveConfig(cfg); state=State::IDLE; lcdPrintLine(0,"Calibration saved"); delay(500); exitHoldT=0; }
  } else exitHoldT=0;

  if(millis()-lastDisp>400){ showCalib(axisSel); lastDisp=millis(); }
}

void loop(){
  cliPollOnce();
  inputsPoll();
  readInputs();
  wj66Poll();

  switch(state){
    case State::RUN: motionTask(); showRun(); break;
    case State::ERROR: showError(); break;
    case State::CALIB: task_calibration(); break;
    case State::SELF_TEST: selftestTask(); break;
    case State::DIAGNOSTICS: /* placeholder */ break;
    case State::MANUAL_TILT:
      manualTiltTask();
      showManualTilt(getManualTiltTarget(), readTiltAngleDegrees(), cfg.a_axis_tilt_tolerance);
      break;
    default: break;
  }

  task_stateAndSafety();
  task_housekeeping();
  pushOutputs(); // flush batched IO writes
  delay(10);
}
