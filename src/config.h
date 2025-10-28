#pragma once
#include "globals.h"
extern Preferences prefs;
extern Config cfg;
void cfgDefaults();
void cfgValidate();
void loadConfig();
void saveConfig(const Config& c);
void cfgExportJSON();
void cfgImportJSON();
float adcReadRaw(int ch);
float adcReadLinearized(int ch);
float mockTemperatureC();
float readTiltAngleDegrees();
