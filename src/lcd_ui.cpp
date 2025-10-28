#include "lcd_ui.h"
#include "config.h"
#include "wj66.h"
#include "journal.h"
#include "io.h"
#include "motion.h"

static LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
extern AlarmCode alarmLatestCode();
extern int16_t   alarmLatestDetail();

void lcdInit(){ if(i2cTryLock(50)){ lcd.init(); lcd.backlight(); i2cUnlock(); }
                lcdPrintLine(0,String("BISSO E350 ")+FW_VERSION);
                lcdPrintLine(1,String("Schema 0x")+String(CONFIG_SCHEMA_VER,HEX));
                lcdPrintLine(2,"Init OK"); lcdPrintLine(3,"115200/9600"); delay(600); }

void lcdPrintLine(uint8_t row, const String& s){
  String t=s; if (t.length()<20){ int pad=20-t.length(); while(pad--) t+=' '; }
  else if (t.length()>20) t=t.substring(0,20);
  if(i2cTryLock(50)){ lcd.setCursor(0,row); lcd.print(t); i2cUnlock(); }
}

void showRun(){
  char l0[21],l1[21],l2[21],l3[21];
  snprintf(l0,sizeof(l0),"RUN A:%c Q:%02d", X_AUTO()?'1':'0', motionQueueCount());
  snprintf(l1,sizeof(l1),"TEMP:%4.1fC", mockTemperatureC());
  snprintf(l2,sizeof(l2),"ENC:%3u%% OK", (unsigned)wj66GoodPct());
  AlarmCode c=alarmLatestCode(); int16_t d=alarmLatestDetail(); (void)d;
  snprintf(l3,sizeof(l3),"ALM:%s", (c==AlarmCode::NONE)?"NONE":"SET");
  lcdPrintLine(0,l0); lcdPrintLine(1,l1); lcdPrintLine(2,l2); lcdPrintLine(3,l3);
}

static const char* alarmToStr(AlarmCode c){
  switch(c){
    case AlarmCode::SOFTLIMIT: return "SOFTLIMIT";
    case AlarmCode::SENSOR_FAULT: return "SENSOR_FAULT";
    case AlarmCode::TEMP_TRIP: return "TEMP_TRIP";
    case AlarmCode::ESTOP: return "ESTOP";
    case AlarmCode::OUTPUT_INTERLOCK: return "OUTPUT_INTERLOCK";
    case AlarmCode::ENC_MISMATCH: return "ENC_MISMATCH";
    case AlarmCode::STALL: return "STALL";
    default: return "NONE";
  }
}
void showError(){
  AlarmCode c=alarmLatestCode(); int16_t d=alarmLatestDetail();
  lcdPrintLine(0,"*** ERROR ***");
  lcdPrintLine(1,String(alarmToStr(c)));
  char l2[21]; snprintf(l2,sizeof(l2),"Detail:%d",(int)d);
  lcdPrintLine(2,l2);
  lcdPrintLine(3,"Press START to ack");
}

void showCalib(uint8_t axisSel){
  char l0[21],l1[21],l2[21],l3[21];
  float raw=adcReadRaw(axisSel), lin=adcReadLinearized(axisSel);
  snprintf(l0,sizeof(l0),"CAL AXIS:%c",'X'+axisSel);
  snprintf(l1,sizeof(l1),"GAIN:%6.3f OFF:%5.3f", cfg.cal[axisSel].gain, cfg.cal[axisSel].offset);
  snprintf(l2,sizeof(l2),"RAW:%6.2f LIN:%6.2f", raw, lin);
  snprintf(l3,sizeof(l3),"Hold START>3s=SAVE");
  lcdPrintLine(0,l0); lcdPrintLine(1,l1); lcdPrintLine(2,l2); lcdPrintLine(3,l3);
}
