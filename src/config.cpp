#include "config.h"
#include "journal.h"

Preferences prefs;
Config cfg;

void cfgDefaults(){
  cfg.schema=CONFIG_SCHEMA_VER; cfg.debounce_ms=50;
  cfg.temp_warn_C=80.0f; cfg.temp_trip_C=90.0f;
  for(int i=0;i<4;i++){ cfg.softMin[i]=-1000.0f; cfg.softMax[i]=1000.0f; cfg.cal[i]={1.0f,0.0f}; }
  cfg.journal_flush_ms=5000; cfg.journal_flush_batch=10; cfg.journal_max_bytes=131072;
  cfg.run_ms_total=0;
  cfg.a_axis_sensor_ch=1;
  cfg.a_axis_degrees_per_unit=0.1f;
  cfg.a_axis_tilt_tolerance=2.0f;
}
void cfgValidate(){
  if(cfg.debounce_ms<10||cfg.debounce_ms>1000) cfg.debounce_ms=50;
  if(cfg.temp_warn_C<0||cfg.temp_warn_C>150) cfg.temp_warn_C=80.0f;
  if(cfg.temp_trip_C<=cfg.temp_warn_C) cfg.temp_trip_C=cfg.temp_warn_C+10.0f;
  if(cfg.temp_trip_C>160.0f) cfg.temp_trip_C=160.0f;
  for(int a=0;a<4;a++){ if(cfg.softMin[a]>=cfg.softMax[a]){ cfg.softMin[a]=-1000.0f; cfg.softMax[a]=1000.0f; } }
  for(int i=0;i<4;i++){ if(cfg.cal[i].gain<0.1f||cfg.cal[i].gain>10.0f) cfg.cal[i].gain=1.0f;
                        if(fabs(cfg.cal[i].offset)>5.0f) cfg.cal[i].offset=0.0f; }
  if(cfg.journal_flush_ms<500||cfg.journal_flush_ms>60000) cfg.journal_flush_ms=5000;
  if(cfg.journal_flush_batch<1||cfg.journal_flush_batch>100) cfg.journal_flush_batch=10;
  if(cfg.journal_max_bytes<16384||cfg.journal_max_bytes>524288) cfg.journal_max_bytes=131072;
  if(cfg.a_axis_sensor_ch>3) cfg.a_axis_sensor_ch=1;
  if(cfg.a_axis_degrees_per_unit<0.01f||cfg.a_axis_degrees_per_unit>10.0f) cfg.a_axis_degrees_per_unit=0.1f;
  if(cfg.a_axis_tilt_tolerance<0.1f||cfg.a_axis_tilt_tolerance>90.0f) cfg.a_axis_tilt_tolerance=2.0f;
}
void saveConfig(const Config& c){ prefs.begin("bisso",false); prefs.putBytes("cfg",&c,sizeof(Config)); prefs.end(); }
void loadConfig(){
  prefs.begin("bisso",true);
  size_t n=prefs.getBytesLength("cfg");
  if(n==sizeof(Config)){ prefs.getBytes("cfg",&cfg,sizeof(Config));
    if(cfg.schema!=CONFIG_SCHEMA_VER){ cfgDefaults(); saveConfig(cfg); } else cfgValidate(); }
  else { cfgDefaults(); saveConfig(cfg); }
  prefs.end();
}

void cfgExportJSON(){
  File f=SPIFFS.open("/config.json",FILE_WRITE); if(!f){ Serial.println(F("[ERR] open /config.json")); return; }
  f.printf("{\"schema\":%u,", (unsigned)cfg.schema);
  f.printf("\"debounce_ms\":%u,", (unsigned)cfg.debounce_ms);
  f.printf("\"temp_warn_C\":%.3f,\"temp_trip_C\":%.3f,", cfg.temp_warn_C, cfg.temp_trip_C);
  f.printf("\"softMin\":[%.3f,%.3f,%.3f,%.3f],", cfg.softMin[0],cfg.softMin[1],cfg.softMin[2],cfg.softMin[3]);
  f.printf("\"softMax\":[%.3f,%.3f,%.3f,%.3f],", cfg.softMax[0],cfg.softMax[1],cfg.softMax[2],cfg.softMax[3]);
  f.printf("\"cal\":[");
  for(int i=0;i<4;i++) f.printf("{\"gain\":%.6f,\"offset\":%.6f}%s", cfg.cal[i].gain, cfg.cal[i].offset, (i<3)?",":"");
  f.printf("],\"journal_flush_ms\":%u,\"journal_flush_batch\":%u,\"journal_max_bytes\":%u,",
           (unsigned)cfg.journal_flush_ms,(unsigned)cfg.journal_flush_batch,(unsigned)cfg.journal_max_bytes);
  f.printf("\"run_ms_total\":%llu,", (unsigned long long)cfg.run_ms_total);
  f.printf("\"a_axis_sensor_ch\":%u,\"a_axis_degrees_per_unit\":%.6f,\"a_axis_tilt_tolerance\":%.3f}\n",
           (unsigned)cfg.a_axis_sensor_ch, cfg.a_axis_degrees_per_unit, cfg.a_axis_tilt_tolerance);
  f.close(); Serial.println(F("OK /config.json written"));
}

static bool parseArray4(const String& line, const char* key, float outv[4]){
  int k=line.indexOf(key); if(k<0) return false;
  int lb=line.indexOf('[',k); int rb=line.indexOf(']',lb);
  if(lb<0||rb<0) return false; String arr=line.substring(lb+1,rb);
  for(int i=0;i<4;i++){ int comma=arr.indexOf(','); String tok=(comma<0)?arr:arr.substring(0,comma);
    outv[i]=tok.toFloat(); if(comma<0) break; arr=arr.substring(comma+1); } return true;
}

void cfgImportJSON(){
  File f=SPIFFS.open("/config.json",FILE_READ); if(!f){ Serial.println(F("[ERR] open /config.json")); return; }
  String s; while(f.available()){
    s=f.readStringUntil('\n');
    int k;
    if((k=s.indexOf("\"debounce_ms\""))>=0){ int c=s.indexOf(':',k); cfg.debounce_ms=(uint16_t)s.substring(c+1).toInt(); }
    if((k=s.indexOf("\"temp_warn_C\""))>=0){ int c=s.indexOf(':',k); cfg.temp_warn_C=s.substring(c+1).toFloat(); }
    if((k=s.indexOf("\"temp_trip_C\""))>=0){ int c=s.indexOf(':',k); cfg.temp_trip_C=s.substring(c+1).toFloat(); }
    if((k=s.indexOf("\"journal_flush_ms\""))>=0){ int c=s.indexOf(':',k); cfg.journal_flush_ms=(uint32_t)s.substring(c+1).toInt(); }
    if((k=s.indexOf("\"journal_flush_batch\""))>=0){ int c=s.indexOf(':',k); cfg.journal_flush_batch=(uint16_t)s.substring(c+1).toInt(); }
    if((k=s.indexOf("\"journal_max_bytes\""))>=0){ int c=s.indexOf(':',k); cfg.journal_max_bytes=(uint32_t)s.substring(c+1).toInt(); }
    if((k=s.indexOf("\"run_ms_total\""))>=0){ int c=s.indexOf(':',k); cfg.run_ms_total=(uint64_t)s.substring(c+1).toInt(); }
    if((k=s.indexOf("\"a_axis_sensor_ch\""))>=0){ int c=s.indexOf(':',k); cfg.a_axis_sensor_ch=(uint8_t)s.substring(c+1).toInt(); }
    if((k=s.indexOf("\"a_axis_degrees_per_unit\""))>=0){ int c=s.indexOf(':',k); cfg.a_axis_degrees_per_unit=s.substring(c+1).toFloat(); }
    if((k=s.indexOf("\"a_axis_tilt_tolerance\""))>=0){ int c=s.indexOf(':',k); cfg.a_axis_tilt_tolerance=s.substring(c+1).toFloat(); }
    float tmp4[4];
    if(parseArray4(s,"\"softMin\"",tmp4)) for(int i=0;i<4;i++) cfg.softMin[i]=tmp4[i];
    if(parseArray4(s,"\"softMax\"",tmp4)) for(int i=0;i<4;i++) cfg.softMax[i]=tmp4[i];

    if(s.indexOf("\"gain\"")>=0 && s.indexOf("\"offset\"")>=0){
      int pos=0, idx=0;
      while(idx<4){
        int gk=s.indexOf("\"gain\"",pos); if(gk<0) break;
        int ok=s.indexOf("\"offset\"",gk); if(ok<0) break;
        int gc=s.indexOf(':',gk); int oc=s.indexOf(':',ok); if(gc<0||oc<0) break;
        float g=s.substring(gc+1).toFloat();
        float o=s.substring(oc+1).toFloat();
        cfg.cal[idx].gain=clampT(g,0.1f,10.0f);
        cfg.cal[idx].offset=clampT(o,-5.0f,5.0f);
        idx++; pos=oc+1;
      }
    }
  }
  f.close(); cfgValidate(); saveConfig(cfg); Serial.println(F("OK /config.json loaded (all keys)."));
}

float adcReadRaw(int ch){
  int pin = (ch==0)?ADC_PIN0:(ch==1)?ADC_PIN1:(ch==2)?ADC_PIN2:ADC_PIN3;
  uint16_t v = analogRead(pin);
  return (float)v;
}
float adcReadLinearized(int ch){ return adcReadRaw(ch)*cfg.cal[ch&3].gain + cfg.cal[ch&3].offset; }
float mockTemperatureC(){ return adcReadLinearized(0)*0.1f; }
float readTiltAngleDegrees(){ return adcReadLinearized(cfg.a_axis_sensor_ch)*cfg.a_axis_degrees_per_unit; }
