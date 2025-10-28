#include "selftest.h"
#include "io.h"
#include "journal.h"
#include "lcd_ui.h"
#include "inputs.h"

static uint32_t st_startMs=0, st_lastStep=0; static uint8_t st_index=0;

void selftestEnter(){
  st_startMs=millis(); st_lastStep=0; st_index=0;
  state=State::SELF_TEST; journalLog("INFO","SELFTEST_START");
  lcdPrintLine(0,"SELF-TEST: RUNNING");
}
void selftestTask(){
  uint32_t now=millis();
  if(now - st_startMs > SELFTEST_TIMEOUT_MS){
    outputsIdle(); state=State::DIAGNOSTICS; journalLog("INFO","SELFTEST_TIMEOUT");
    lcdPrintLine(0,"SELF-TEST: TIMEOUT"); delay(800); return;
  }
  if(now - st_lastStep >= SELFTEST_STEP_MS){
    outputsIdle(); setYIndex(st_index,true); st_index=(st_index+1)%9; st_lastStep=now;
  }
  if(btnStartRose()){ outputsIdle(); state=State::DIAGNOSTICS; journalLog("INFO","SELFTEST_EXIT");
    lcdPrintLine(0,"SELF-TEST: EXIT"); delay(500); }
}
