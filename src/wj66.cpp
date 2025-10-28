#include "wj66.h"
#include "journal.h"

WJ66Data wj66 = {{0,0,0,0},0,0,0,0,0};
uint8_t wj66_consecStale = 0;

#define WJ66_MAX_LINE   64
#define WJ66_TIMEOUT_MS 1000

void wj66Init(){ wj66 = {{0,0,0,0}, 0,0,0,0,0}; wj66_consecStale=0; }

void wj66Poll(){
  static char buf[WJ66_MAX_LINE]; static uint8_t idx=0; static bool inLine=false;
  while(Serial2.available()){
    char c=Serial2.read();
    if(c=='\r' || c=='\n'){
      if(!inLine) continue;
      inLine=false; buf[idx]='\0'; idx=0; wj66.frames++;
      // Expect: "01,+000123,+000456,+000789,+001234"
      long tmp[4]={wj66.pos[0],wj66.pos[1],wj66.pos[2],wj66.pos[3]};
      int found=0; char* save=nullptr; char* tok=strtok_r(buf,",",&save);
      int field=0;
      while(tok){
        if(field>=1 && field<=4){
          long v=strtol(tok,nullptr,10);
          tmp[field-1]=v; found++;
        }
        field++; tok=strtok_r(nullptr,",",&save);
      }
      if(found==4){ memcpy(wj66.pos,tmp,sizeof(tmp)); wj66.parsed++; wj66.lastFrameMs=millis(); wj66_consecStale=0; }
      else { wj66.malformed++; }
    }else{
      if(!inLine){ inLine=true; idx=0; }
      if(idx<WJ66_MAX_LINE-1) buf[idx++]=c; else { idx=0; inLine=false; wj66.malformed++; }
    }
  }
  uint32_t now=millis();
  if(now - wj66.lastFrameMs > WJ66_TIMEOUT_MS){
    wj66.staleHits++; wj66_consecStale++; wj66.lastFrameMs=now;
  }
}

uint8_t wj66GoodPct(){
  if(wj66.frames==0){ static uint32_t lastWarn=0; if(millis()-lastWarn>10000){ journalLog("WARN","WJ66_NO_FRAMES"); lastWarn=millis(); } return 0; }
  return (uint8_t)((wj66.parsed*100UL)/wj66.frames);
}
