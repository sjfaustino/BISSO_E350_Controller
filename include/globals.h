#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <FS.h>
#include <SPIFFS.h>
#include <string.h>
#include <math.h>

#define FW_VERSION          "v0.4.9-DevR3"
#define CONFIG_SCHEMA_VER   0x0493
#define LCD_ADDR            0x27

#define QMAX                16
#define POS_TOL             1.0f
#define DIR_DEAD_MS         100
#define SELFTEST_TIMEOUT_MS 30000
#define SELFTEST_STEP_MS    500

// ADC pins (adjust per board)
#define ADC_PIN0 36
#define ADC_PIN1 39
#define ADC_PIN2 34
#define ADC_PIN3 35

enum class Axis : uint8_t { AX_X=0, AX_Y, AX_Z, AX_A };
enum class State : uint8_t { IDLE=0, RUN, CALIB, DIAGNOSTICS, SELF_TEST, ERROR };
enum class AlarmCode : int8_t {
  NONE=0, SOFTLIMIT, SENSOR_FAULT, TEMP_TRIP, ESTOP,
  OUTPUT_INTERLOCK, ENC_MISMATCH, STALL
};

struct Move { Axis axis; float targetAbs; float feed; float startAbs; uint32_t enqueuedMs; };
struct Cal  { float gain; float offset; };

struct Config {
  uint16_t schema;
  uint16_t debounce_ms;
  float temp_warn_C, temp_trip_C;
  float softMin[4], softMax[4];
  Cal cal[4];
  uint32_t journal_flush_ms, journal_flush_batch, journal_max_bytes;
  uint64_t run_ms_total;
};

struct WJ66Data {
  long pos[4];
  uint32_t lastFrameMs, parsed, frames, staleHits, malformed;
};

extern State state;
extern Config cfg;
extern WJ66Data wj66;

template<typename T> static inline T clampT(T v,T lo,T hi){ return v<lo?lo:(v>hi?hi:v); }

// Cooperative I2C lock
void i2cLockInit();
bool i2cTryLock(uint32_t timeoutMs);
void i2cUnlock();

// System error reporting
void onSystemError(AlarmCode code, int16_t detail);
