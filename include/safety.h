#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>

#define SAFETY_ALARM_PIN 2
#define SAFETY_MAX_STALL_TIME_MS 2000
#define SAFETY_STALL_THRESHOLD 100
#define SAFETY_FAULT_HISTORY_SIZE 16

typedef enum {
  SAFETY_OK = 0,
  SAFETY_STALLED = 1,
  SAFETY_SOFT_LIMIT = 2,
  SAFETY_PLC_FAULT = 3,
  SAFETY_THERMAL = 4,
  SAFETY_ALARM = 5,
  SAFETY_ENCODER_ERROR = 6
} safety_fault_t;

typedef struct {
  safety_fault_t current_fault;
  uint32_t fault_timestamp;
  uint32_t fault_duration_ms;
  uint32_t fault_count;
  char fault_message[256];
  safety_fault_t fault_history[SAFETY_FAULT_HISTORY_SIZE];
  uint8_t history_index;
} safety_state_t;

void safetyInit();
void safetyUpdate();
bool safetyCheckMotionAllowed(uint8_t axis);
void safetyTriggerAlarm(const char* reason);
void safetyResetAlarm();
void safetyReportStall(uint8_t axis);
void safetyReportSoftLimit(uint8_t axis);
void safetyReportEncoderError(uint8_t axis);
void safetyReportPLCFault();
safety_fault_t safetyGetCurrentFault();
bool safetyIsAlarmed();
uint32_t safetyGetAlarmDuration();
void safetyDiagnostics();

#endif
