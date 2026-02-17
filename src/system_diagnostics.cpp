/**
 * @file system_diagnostics.cpp
 * @brief System Diagnostic Snapshot Exporter
 */

#include <Arduino.h>
#include "serial_logger.h"
#include "config_unified.h"
#include "encoder_wj66.h"
#include "altivar31_modbus.h"
#include "motion.h"

void systemDumpDiagnostics() {
    serialLoggerLock();
    logPrintln("\n[DIAG] === SYSTEM SNAPSHOT ===");
    
    // 1. Version & Build
    logPrintf("Firmware: %s (Compiled: %s %s)\n", "BISSO E350 v1.2", __DATE__, __TIME__);
    logPrintf("Uptime  : %lu ms\n", millis());
    
    // 2. Motion State
    logPrintf("Motion  : %s\n", motionIsMoving() ? "MOVING" : "IDLE");
    
    // 3. Encoder Status
    const encoder_wj66_stats_t* e = encoderWj66GetStats();
    logPrintf("Encoder : Raw:%ld Pos:%.3f (Errors:%u)\n", 
        encoderWj66GetLastValue(), 
        motionGetAxisPosition(0), 
        e->error_count);
        
    // 4. VFD Status
    const altivar31_state_t* v = altivar31GetState();
    logPrintf("VFD     : Freq:%.1fHz Curr:%.1fA (Errors:%u)\n", 
        v->frequency_hz, v->current_amps, v->error_count);
        
    // 5. Config Dump
    logPrintln("\n[DIAG] NVS Config:");
    configUnifiedPrintAll();
    
    logPrintln("\n[DIAG] === END SNAPSHOT ===\n");
    serialLoggerUnlock();
}
