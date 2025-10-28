#pragma once
#include "globals.h"

void motionInit();
bool enqueueAxisMove(Axis a, float targetAbs, float feed);
void motionTask();
int  motionQueueCount();
bool setDirBits(bool dirPositive);
