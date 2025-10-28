#pragma once
#include "globals.h"
void inputsInit(uint16_t debounce_ms);
void inputsPoll();
bool btnStartRose();
bool btnStartRead();
bool swOnOffRead();
bool inEstopRead();
bool X_AUTO();
bool X_SEL_X();
bool X_SEL_Y();
bool X_SEL_XY();
