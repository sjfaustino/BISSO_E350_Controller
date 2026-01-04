#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>
#include "safety_state_machine.h" // Includes the safety_fsm_state_t enum definition

#define SAFETY_ALARM_PIN 2
#define SAFETY_MAX_STALL_TIME_MS 2000
#define SAFETY_STALL_THRESHOLD 100
#define SAFETY_FAULT_HISTORY_SIZE 16

// This enumeration defines the CAUSES/TYPES OF FAULTS (e.g., what happened)
typedef enum {
  SAFETY_OK = 0, 
  SAFETY_STALLED = 1,
  SAFETY_SOFT_LIMIT = 2,
  SAFETY_PLC_FAULT = 3,
  SAFETY_THERMAL = 4,
  SAFETY_ALARM = 5,
  SAFETY_ENCODER_ERROR = 6
} safety_fault_t;

// FIX: This structure defines the data container for the entire safety module.
// The name is changed to resolve the structural conflict.
typedef struct safety_system_data { 
  safety_fault_t current_fault;
  safety_fsm_state_t fsm_state; // Use the FSM state type defined in the other header
  uint32_t fault_timestamp;
  uint32_t fault_duration_ms;
  uint32_t fault_count;
  char fault_message[256];
  safety_fault_t fault_history[SAFETY_FAULT_HISTORY_SIZE];
  uint8_t history_index;
} safety_system_data_t; // FIX: New name for the struct type

void safetyInit();
void safetyUpdate();
bool safetyCheckMotionAllowed(uint8_t axis);
// PHASE 5.10: Added fault_type parameter for thread-safe fault assignment
void safetyTriggerAlarm(const char* reason, safety_fault_t fault_type);
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
