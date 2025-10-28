#include "journal.h"

#define JBUF_BYTES 8192
static char jbuf[JBUF_BYTES]; static size_t jlen=0;
static File jf; static uint32_t lastFlush=0; static bool jOpen=false;

extern Config cfg;

static void rotateJournalIfNeeded(){
  File f=SPIFFS.open("/journal.txt",FILE_READ);
  if(f){ size_t sz=f.size(); f.close();
    if(sz>=cfg.journal_max_bytes){ SPIFFS.remove("/journal.1"); SPIFFS.rename("/journal.txt","/journal.1"); }
  }
}
static void jOpenIfNeeded(){ if(jOpen && jf) return; jf=SPIFFS.open("/journal.txt",FILE_APPEND); jOpen = (bool)jf; lastFlush=millis(); }

void journalInit(){ SPIFFS.begin(true); rotateJournalIfNeeded(); jOpen=false; jlen=0; }

static void jbufWrite(const char* s,size_t n){
  if(n>JBUF_BYTES){ jOpenIfNeeded(); if(jOpen) jf.write((const uint8_t*)s,n); return; }
  if(jlen+n>JBUF_BYTES){ jOpenIfNeeded(); if(jOpen && jlen) jf.write((const uint8_t*)jbuf,jlen); jlen=0; }
  memcpy(jbuf+jlen,s,n); jlen+=n;
}
void journalFlushToFS(bool force){
  uint32_t now=millis();
  jOpenIfNeeded(); if(!jOpen) return;
  if(force || (now-lastFlush)>=cfg.journal_flush_ms || jlen>=JBUF_BYTES/2){
    if(jlen) jf.write((const uint8_t*)jbuf,jlen), jlen=0;
    jf.flush(); lastFlush=now; rotateJournalIfNeeded();
  }
}
void journalLog(const char* level, const char* msg){
  char line[256]; int n=snprintf(line,sizeof(line),"[%lu] %s %s\n",(unsigned long)millis(),level,msg);
  if(n>0) jbufWrite(line,(size_t)n);
}

void journalTailPrint(size_t nLines){
  journalFlushToFS(true);
  File f=SPIFFS.open("/journal.txt",FILE_READ); if(!f){ Serial.println(F("[ERR] journal missing")); return; }
  const size_t B=256; char buf[B]; size_t found=0; size_t pos=f.size();
  while(pos>0 && found<=nLines){ size_t chunk=(pos>=B?B:pos); pos-=chunk; f.seek(pos);
    size_t r=f.read((uint8_t*)buf,chunk); for(int i=(int)r-1;i>=0;--i) if(buf[i]=='\n') found++; }
  if(pos>0) f.seek(pos); else f.seek(0);
  Serial.println(F("--- Journal Tail ---")); while(f.available()) Serial.write(f.read());
  Serial.println(F("--- End ---")); f.close();
}

#define MAX_ALARMS 32
struct AlarmItem{ AlarmCode code; int16_t detail; };
static AlarmItem alarms[MAX_ALARMS]; static int aHead=0, aCount=0, aLatest=-1;

void alarmPush(AlarmCode code, int16_t detail){
  alarms[aHead].code=code; alarms[aHead].detail=detail; aLatest=aHead;
  aHead=(aHead+1)%MAX_ALARMS; if(aCount<MAX_ALARMS) aCount++; else aCount=MAX_ALARMS;
  char msg[64]; snprintf(msg,sizeof(msg),"ALARM code=%d detail=%d",(int)code,(int)detail);
  journalLog("ERROR",msg);
}
AlarmCode alarmLatestCode(){ if(aLatest<0) return AlarmCode::NONE; return alarms[aLatest].code; }
int16_t   alarmLatestDetail(){ if(aLatest<0) return 0; return alarms[aLatest].detail; }
