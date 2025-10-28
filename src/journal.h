#pragma once
#include "globals.h"
void journalInit();
void journalLog(const char* level, const char* msg);
void journalFlushToFS(bool force);
void journalTailPrint(size_t nLines);
void alarmPush(AlarmCode code, int16_t detail);
AlarmCode alarmLatestCode();
int16_t   alarmLatestDetail();
