#pragma once
#include "globals.h"
void lcdInit();
void lcdPrintLine(uint8_t row, const String& s);
void showRun();
void showError();
void showCalib(uint8_t axisSel);
void showManualTilt(float targetDeg, float currentDeg, float tolerance);
