/**
 * @file job_recovery.cpp
 * @brief Power Loss Recovery - Save and restore job state
 * @project BISSO E350 Controller
 */

#include "job_recovery.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <Preferences.h>
#include <string.h>
#include <time.h>

// NVS namespace for recovery data
static Preferences recoveryPrefs;
static const char* RECOVERY_NS = "jobrecov";

// Cached state
static job_recovery_t cached_state;
static bool has_valid_state = false;
static uint32_t lines_since_save = 0;
static uint32_t save_interval = 50;  // Default, loaded from config

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

static uint32_t getTimestamp(void) {
    time_t now;
    time(&now);
    return (uint32_t)now;
}

static bool loadFromNVS(void) {
    if (!recoveryPrefs.begin(RECOVERY_NS, false)) {  // Read-write to allow creation on first boot
        return false;
    }
    
    cached_state.magic = recoveryPrefs.getUInt("magic", 0);
    if (cached_state.magic != RECOVERY_MAGIC) {
        recoveryPrefs.end();
        return false;
    }
    
    recoveryPrefs.getString("filename", cached_state.filename, sizeof(cached_state.filename));
    cached_state.line_number = recoveryPrefs.getUInt("line", 0);
    cached_state.pos_x = recoveryPrefs.getFloat("pos_x", 0.0f);
    cached_state.pos_y = recoveryPrefs.getFloat("pos_y", 0.0f);
    cached_state.pos_z = recoveryPrefs.getFloat("pos_z", 0.0f);
    cached_state.pos_a = recoveryPrefs.getFloat("pos_a", 0.0f);
    cached_state.wcs_index = recoveryPrefs.getUChar("wcs", 0);
    cached_state.feed_rate = recoveryPrefs.getFloat("feed", 0.0f);
    cached_state.timestamp = recoveryPrefs.getUInt("timestamp", 0);
    
    recoveryPrefs.end();
    return true;
}

static bool saveToNVS(const job_recovery_t* state) {
    if (!recoveryPrefs.begin(RECOVERY_NS, false)) {  // Read-write
        logError("[RECOVERY] Failed to open NVS for write");
        return false;
    }
    
    recoveryPrefs.putUInt("magic", state->magic);
    recoveryPrefs.putString("filename", state->filename);
    recoveryPrefs.putUInt("line", state->line_number);
    recoveryPrefs.putFloat("pos_x", state->pos_x);
    recoveryPrefs.putFloat("pos_y", state->pos_y);
    recoveryPrefs.putFloat("pos_z", state->pos_z);
    recoveryPrefs.putFloat("pos_a", state->pos_a);
    recoveryPrefs.putUChar("wcs", state->wcs_index);
    recoveryPrefs.putFloat("feed", state->feed_rate);
    recoveryPrefs.putUInt("timestamp", state->timestamp);
    
    recoveryPrefs.end();
    return true;
}

// =============================================================================
// PUBLIC API
// =============================================================================

void recoveryInit(void) {
    // Load config
    int enabled = configGetInt(KEY_RECOV_EN, 1);
    save_interval = configGetInt(KEY_RECOV_INTERVAL, 50);
    
    if (!enabled) {
        logInfo("[RECOVERY] Power loss recovery DISABLED");
        has_valid_state = false;
        return;
    }
    
    // Check for existing recovery state
    has_valid_state = loadFromNVS();
    
    if (has_valid_state) {
        logWarning("[RECOVERY] ⚠️ RECOVERY DATA FOUND!");
        logPrintf("  File: %s\n", cached_state.filename);
        logPrintf("  Line: %lu\n", (unsigned long)cached_state.line_number);
        logPrintf("  Position: X%.2f Y%.2f Z%.2f\n", 
                  cached_state.pos_x, cached_state.pos_y, cached_state.pos_z);
        logPrintln("  Use 'job resume' to continue or 'job recovery clear' to discard");
    } else {
        logDebug("[RECOVERY] No recovery data found");
    }
    
    lines_since_save = 0;
}

bool recoveryHasState(void) {
    return has_valid_state;
}

bool recoveryGetState(job_recovery_t* state) {
    if (!has_valid_state || state == NULL) {
        return false;
    }
    memcpy(state, &cached_state, sizeof(job_recovery_t));
    return true;
}

void recoverySaveState(const char* filename, uint32_t line_number,
                       float x, float y, float z, float a,
                       uint8_t wcs_index, float feed_rate) {
    
    int enabled = configGetInt(KEY_RECOV_EN, 1);
    if (!enabled) return;
    
    job_recovery_t state;
    state.magic = RECOVERY_MAGIC;
    strncpy(state.filename, filename, sizeof(state.filename) - 1);
    state.filename[sizeof(state.filename) - 1] = '\0';
    state.line_number = line_number;
    state.pos_x = x;
    state.pos_y = y;
    state.pos_z = z;
    state.pos_a = a;
    state.wcs_index = wcs_index;
    state.feed_rate = feed_rate;
    state.timestamp = getTimestamp();
    
    if (saveToNVS(&state)) {
        memcpy(&cached_state, &state, sizeof(job_recovery_t));
        has_valid_state = true;
        lines_since_save = 0;
        logDebug("[RECOVERY] State saved at line %lu", (unsigned long)line_number);
    }
}

void recoveryClear(void) {
    if (!recoveryPrefs.begin(RECOVERY_NS, false)) {
        logError("[RECOVERY] Failed to clear NVS");
        return;
    }
    
    recoveryPrefs.clear();
    recoveryPrefs.end();
    
    has_valid_state = false;
    memset(&cached_state, 0, sizeof(cached_state));
    lines_since_save = 0;
    
    logInfo("[RECOVERY] Recovery data cleared");
}

void recoveryPrintStatus(void) {
    int enabled = configGetInt(KEY_RECOV_EN, 1);
    
    logPrintln("\n[RECOVERY] === Power Loss Recovery ===");
    logPrintf("  Enabled:  %s\n", enabled ? "YES" : "NO");
    logPrintf("  Interval: %lu lines\n", (unsigned long)save_interval);
    
    if (has_valid_state) {
        logPrintln("\n  ** RECOVERY DATA AVAILABLE **");
        logPrintf("  File:     %s\n", cached_state.filename);
        logPrintf("  Line:     %lu\n", (unsigned long)cached_state.line_number);
        logPrintf("  Position: X%.2f Y%.2f Z%.2f A%.2f\n",
                  cached_state.pos_x, cached_state.pos_y, 
                  cached_state.pos_z, cached_state.pos_a);
        logPrintf("  WCS:      G%d\n", 54 + cached_state.wcs_index);
        logPrintf("  Feed:     %.1f mm/min\n", cached_state.feed_rate);
        
        // Format timestamp
        if (cached_state.timestamp > 0) {
            time_t ts = cached_state.timestamp;
            struct tm* tm_info = localtime(&ts);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
            logPrintf("  Saved:    %s\n", buf);
        }
    } else {
        logPrintln("\n  No recovery data available");
    }
}

uint32_t recoveryGetLinesSinceSave(void) {
    return lines_since_save;
}

void recoveryCheckAutoSave(const char* filename, uint32_t line_number,
                           float x, float y, float z, float a,
                           uint8_t wcs_index, float feed_rate) {
    
    int enabled = configGetInt(KEY_RECOV_EN, 1);
    if (!enabled) return;
    
    lines_since_save++;
    
    if (lines_since_save >= save_interval) {
        recoverySaveState(filename, line_number, x, y, z, a, wcs_index, feed_rate);
    }
}
