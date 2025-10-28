#include "motion.h"
#include "io.h"
#include "journal.h"
#include "wj66.h"
#include "config.h"

static Move q[QMAX]; static int qHead=0,qTail=0,qCnt=0;
static bool  busy=false; static Move cur;
static long  startEnc=0, lastEnc=0;
static uint32_t startT=0, lastEncT=0;

static inline void setSpeedBits(float feed){
  Y_FAST(false); Y_MED(false);
  if (feed>=1.0f) Y_FAST(true); else Y_MED(true);
}
static inline void setAxisBits(Axis a){
  Y_AX_X(false); Y_AX_Y(false); Y_AX_Z(false); Y_AX_A(false);
  switch(a){ case Axis::AX_X:Y_AX_X(true);break; case Axis::AX_Y:Y_AX_Y(true);break;
             case Axis::AX_Z:Y_AX_Z(true);break; case Axis::AX_A:Y_AX_A(true);break; }
}

static struct{ bool pending; bool wantPos; uint32_t tStartMs; bool warned; } g_dir={false,true,0,false};
static inline void dirAllOff(){ Y_DIR_POS(false); Y_DIR_NEG(false); }
bool setDirBits(bool dirPositive){
  uint32_t now=millis();
  if(!g_dir.pending){
    bool pos=(bool)Y_DIR_POS_STATE(), neg=(bool)Y_DIR_NEG_STATE();
    if((pos^neg) && ((dirPositive&&pos)||(!dirPositive&&neg))) return true;
    dirAllOff(); g_dir.pending=true; g_dir.wantPos=dirPositive; g_dir.tStartMs=now; g_dir.warned=false; return false;
  }
  if(now-g_dir.tStartMs<DIR_DEAD_MS){
    if(!g_dir.warned){ journalLog("WARN","DIR_INTERLOCK_WAIT"); g_dir.warned=true; }
    dirAllOff(); return false;
  }
  if(g_dir.wantPos){ Y_DIR_POS(true); Y_DIR_NEG(false); journalLog("INFO","DIR_POSITIVE_SET"); }
  else              { Y_DIR_POS(false); Y_DIR_NEG(true); journalLog("INFO","DIR_NEGATIVE_SET"); }
  bool pos=(bool)Y_DIR_POS_STATE(), neg=(bool)Y_DIR_NEG_STATE();
  if(pos&&neg){ journalLog("ERROR","OUTPUT_INTERLOCK BOTH_DIR_ON"); alarmPush(AlarmCode::OUTPUT_INTERLOCK,0);
                outputsIdle(); onSystemError(AlarmCode::OUTPUT_INTERLOCK,0); g_dir.pending=false; return false; }
  g_dir.pending=false; return true;
}

void motionInit(){ busy=false; qHead=qTail=qCnt=0; }

int motionQueueCount(){ return qCnt; }
static bool qPush(const Move&m){ if(qCnt>=QMAX) return false; q[qTail]=m; qTail=(qTail+1)%QMAX; qCnt++; return true; }
static bool qPop(Move& m){ if(qCnt==0) return false; m=q[qHead]; qHead=(qHead+1)%QMAX; qCnt--; return true; }

bool enqueueAxisMove(Axis a, float targetAbs, float feed){
  if(qCnt>=QMAX){ journalLog("ERROR","Q_FULL"); return false; }
  int ai=(int)a; float lo=cfg.softMin[ai], hi=cfg.softMax[ai];
  if(targetAbs<lo||targetAbs>hi){
    char msg[96]; snprintf(msg,sizeof(msg),"SOFTLIMIT_REJECT axis=%d target=%.3f [%.3f..%.3f]",ai,targetAbs,lo,hi);
    journalLog("ERROR",msg); alarmPush(AlarmCode::SOFTLIMIT,(int16_t)ai); outputsIdle(); onSystemError(AlarmCode::SOFTLIMIT,(int16_t)ai); return false;
  }
  Move m{a,targetAbs,feed,(float)wj66.pos[ai],millis()};
  if(!qPush(m)) return false;
  journalLog("INFO","MOVE_ENQUEUED");
  return true;
}

void motionTask(){
  if(!busy){
    if(!qPop(cur)) return;
    busy=true; startT=millis(); setAxisBits(cur.axis);
    startEnc=wj66.pos[(int)cur.axis]; lastEnc=startEnc; lastEncT=millis();
    bool dirPos=(cur.targetAbs>(float)startEnc);
    if(!setDirBits(dirPos)){ outputsIdle(); return; }
    setSpeedBits(cur.feed); Y_VS(true);
  }
  int ai=(int)cur.axis; float lo=cfg.softMin[ai], hi=cfg.softMax[ai];
  float posNow=(float)wj66.pos[ai];
  if(posNow<lo||posNow>hi){
    char msg[96]; snprintf(msg,sizeof(msg),"SOFTLIMIT_BREACH axis=%d pos=%.3f [%.3f..%.3f]",ai,posNow,lo,hi);
    journalLog("ERROR",msg); outputsIdle(); alarmPush(AlarmCode::SOFTLIMIT,(int16_t)ai); onSystemError(AlarmCode::SOFTLIMIT,(int16_t)ai); busy=false; return;
  }
  float err=fabsf(cur.targetAbs-posNow);
  if(err<=POS_TOL){ busy=false; journalLog("INFO","MOVE_COMPLETED"); Y_VS(false); outputsIdle(); return; }

  // Stall detection while VS active
  long encNow = wj66.pos[ai];
  if (encNow != lastEnc){ lastEnc=encNow; lastEncT=millis(); }
  if (millis()-lastEncT > 300){
    alarmPush(AlarmCode::STALL, (int16_t)ai); Y_VS(false); outputsIdle(); onSystemError(AlarmCode::STALL,(int16_t)ai); busy=false; return;
  }
}
