#pragma once
#include "globals.h"

void ioInit();
void readInputs();
void pushOutputs();
void outputsIdle();
void setYIndex(uint8_t idx, bool on);

bool X_SEL_X();
bool X_SEL_Y();
bool X_SEL_XY();
bool X_AUTO();

bool Y_DIR_POS_STATE();
bool Y_DIR_NEG_STATE();

void Y_FAST(bool on);
void Y_MED(bool on);
void Y_AX_X(bool on);
void Y_AX_Y(bool on);
void Y_AX_Z(bool on);
void Y_AX_A(bool on);
void Y_DIR_POS(bool on);
void Y_DIR_NEG(bool on);
void Y_VS(bool on);
