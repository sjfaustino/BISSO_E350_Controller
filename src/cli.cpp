#include "cli.h"
#include "config.h"
#include "journal.h"
#include "io.h"
#include "motion.h"
#include "wj66.h"
#include "lcd_ui.h"
#include "selftest.h"

static String inbuf;

void cliInit(){ inbuf=""; }

void parseGcodeLine(const String& line){
  if(!X_AUTO()) { journalLog("WARN","GCODE_REJECT_NO_AUTO"); return; }
  if(!(line.startsWith("G0")||line.startsWith("G1"))) return;
  float tgt[4]={NAN,NAN,NAN,NAN}; float f=0.5f; // default to medium
  int moves=0;
  for(int i=0;i<(int)line.length();i++){
    char c=line[i];
    if(c=='X'||c=='Y'||c=='Z'||c=='A'||c=='F'){
      int j=i+1; while(j<(int)line.length()&&line[j]==' ') j++;
      String num=""; while(j<(int)line.length()&&(isDigit(line[j])||line[j]=='-'||line[j]=='.'||line[j]=='+')) num+=line[j++];
      float v=num.toFloat();
      if(c=='F') f=v; else { int ax=(c=='X')?0:(c=='Y')?1:(c=='Z')?2:3; tgt[ax]=v; moves++; }
      i=j-1;
    }
  }
  if(moves!=1) { journalLog("WARN","GCODE_REJECT_MULTIAXIS"); return; }
  for(int a=0;a<4;a++) if(!isnan(tgt[a])) enqueueAxisMove((Axis)a,tgt[a],f);
}

static void i2cScan(){
  Serial.println(F("I2C scan:"));
  for(uint8_t addr=0x03; addr<=0x77; addr++){
    Wire.beginTransmission(addr); uint8_t err=Wire.endTransmission();
    if(err==0) Serial.printf(" - 0x%02X\n", addr);
  }
}

void cliPollOnce(){
  while(Serial.available()){
    char c=Serial.read();
    if(c=='\n'||c=='\r'){
      if(inbuf.length()==0) continue;
      String s=inbuf; inbuf="";
      if(s=="help"){
        Serial.println(F("help, ver, cfg show, cfg export, cfg import, jtail [n], flush, i2cscan, cal, selftest"));
        Serial.println(F("set cal <ch> <gain> <offset>"));
        Serial.println(F("# G0/G1: single axis X|Y|Z|A with optional F (AUTO only)"));
      } else if(s=="ver"){
        Serial.printf("FW %s schema 0x%04X\n", FW_VERSION, CONFIG_SCHEMA_VER);
      } else if(s=="cfg show"){
        Serial.printf("schema=0x%04X debounce=%u temp_warn=%.1f temp_trip=%.1f\n",
          CONFIG_SCHEMA_VER,(unsigned)cfg.debounce_ms,cfg.temp_warn_C,cfg.temp_trip_C);
        Serial.printf("softMin=[%.2f,%.2f,%.2f,%.2f]\n", cfg.softMin[0],cfg.softMin[1],cfg.softMin[2],cfg.softMin[3]);
        Serial.printf("softMax=[%.2f,%.2f,%.2f,%.2f]\n", cfg.softMax[0],cfg.softMax[1],cfg.softMax[2],cfg.softMax[3]);
        for(int i=0;i<4;i++) Serial.printf("cal[%d]={gain=%.5f,offset=%.5f}\n",i,cfg.cal[i].gain,cfg.cal[i].offset);
        Serial.printf("journal_flush_ms=%u batch=%u max=%u run_ms_total=%llu\n",
          (unsigned)cfg.journal_flush_ms,(unsigned)cfg.journal_flush_batch,(unsigned)cfg.journal_max_bytes,
          (unsigned long long)cfg.run_ms_total);
      } else if(s=="cfg export"){ cfgExportJSON();
      } else if(s=="cfg import"){ cfgImportJSON();
      } else if(s.startsWith("jtail")){
        int n=50; if(s.length()>6){ int t=s.substring(6).toInt(); if(t>0) n=min(t,500); } journalTailPrint((size_t)n);
      } else if(s=="flush"){
        journalFlushToFS(true); Serial.println(F("OK flushed"));
      } else if(s=="i2cscan"){ i2cScan();
      } else if(s=="cal"){ state=State::CALIB;
      } else if(s.startsWith("set cal ")){
        int sp1=s.indexOf(' ',8); if(sp1<0){ Serial.println(F("Usage: set cal <ch> <gain> <offset>")); continue; }
        int ch=s.substring(8,sp1).toInt(); if(ch<0||ch>3){ Serial.println(F("Invalid ch")); continue; }
        String rest=s.substring(sp1+1); int sp2=rest.indexOf(' '); if(sp2<0){ Serial.println(F("Missing gain/offset")); continue; }
        float gain=rest.substring(0,sp2).toFloat(); float off=rest.substring(sp2+1).toFloat();
        if(gain<0.1f||gain>10.0f || fabs(off)>5.0f){ Serial.println(F("Out of range")); continue; }
        cfg.cal[ch].gain=gain; cfg.cal[ch].offset=off; saveConfig(cfg); Serial.println(F("OK"));
      } else if(s=="selftest"){ selftestEnter();
      } else {
        parseGcodeLine(s);
      }
    } else {
      inbuf+=c; if(inbuf.length()>200) inbuf.remove(0,50);
    }
  }
}
