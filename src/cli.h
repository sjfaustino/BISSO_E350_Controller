#pragma once
#include "globals.h"
void cliInit();
void cliPollOnce();
void parseGcodeLine(const String& line);
