/**
 * @file operator_alerts.cpp
 * @brief Audible Buzzer and Tower Light Control
 * @project BISSO E350 Controller
 * 
 * Uses PCF8574 I/O expander outputs via plc_iface elboQ73SetRelay()
 */

#include "operator_alerts.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <Arduino.h>
#include "plc_iface.h"

// =============================================================================
// STATE VARIABLES
// =============================================================================

// Status light state
static bool status_light_enabled = false;
static uint16_t status_light_pin_green = 124;  // Default Y9
static uint16_t status_light_pin_yellow = 125; // Default Y10
static uint16_t status_light_pin_red = 126;    // Default Y11
static system_display_state_t current_state = SYSTEM_STATE_IDLE;
static uint32_t last_blink_time = 0;
static bool blink_state = false;

// Buzzer state
static bool buzzer_enabled = false;
static uint16_t buzzer_pin = 127;       // Default Y12
static buzzer_pattern_t current_pattern = BUZZER_OFF;
static uint32_t buzzer_start_time = 0;
static uint8_t buzzer_step = 0;

// Blink timing
#define BLINK_INTERVAL_MS 500
#define BEEP_SHORT_MS 100
#define BEEP_LONG_MS 500
#define BEEP_GAP_MS 200

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

static void statusLightSet(uint16_t pin, bool state) {
    if (pin == 0 || pin == 0xFFFF) return; // Not configured
    plcSetOutput(pin, state);
}

static void updateStatusLightOutputs(bool green, bool yellow, bool red) {
    if (!status_light_enabled) return;
    statusLightSet(status_light_pin_green, green);
    statusLightSet(status_light_pin_yellow, yellow);
    statusLightSet(status_light_pin_red, red);
}

// =============================================================================
// STATUS LIGHT API
// =============================================================================

void statusLightInit(void) {
    status_light_enabled = configGetInt(KEY_STATUS_LIGHT_EN, 0) != 0;  // Default: disabled
    
    // PHASE 16: Auto-migrate legacy pin indices (1-16) to virtual pin IDs (116-131)
    auto migratePin = [](const char* key, uint16_t def) -> uint16_t {
        uint16_t p = configGetInt(key, def);
        if (p >= 1 && p <= 16) {
            uint16_t virtualPin = p + 115;
            logInfo("[STATUS] Migrating legacy pin %d -> Virtual %d for %s", p, virtualPin, key);
            configSetInt(key, virtualPin);
            return virtualPin;
        }
        return p;
    };

    status_light_pin_green = migratePin(KEY_STATUS_LIGHT_GREEN, 124);
    status_light_pin_yellow = migratePin(KEY_STATUS_LIGHT_YELLOW, 125);
    status_light_pin_red = migratePin(KEY_STATUS_LIGHT_RED, 126);
    
    if (status_light_enabled) {
        logInfo("[STATUS] Status light ENABLED (Pins: G:%d Y:%d R:%d)", 
                status_light_pin_green, status_light_pin_yellow, status_light_pin_red);
        // Start in idle state (green)
        statusLightSetState(SYSTEM_STATE_IDLE);
    } else {
        logDebug("[STATUS] Status light disabled");
    }
}

void statusLightSetState(system_display_state_t state) {
    if (!status_light_enabled) return;
    
    current_state = state;
    blink_state = true;  // Reset blink phase
    last_blink_time = millis();
    
    // Set initial output state
    switch (state) {
        case SYSTEM_STATE_IDLE:
            updateStatusLightOutputs(true, false, false);  // Green only
            break;
        case SYSTEM_STATE_RUNNING:
            updateStatusLightOutputs(false, true, false);  // Yellow only
            break;
        case SYSTEM_STATE_PAUSED:
            updateStatusLightOutputs(true, true, false);   // Green + Yellow
            break;
        case SYSTEM_STATE_ESTOP:
            updateStatusLightOutputs(false, false, true);  // Red only
            break;
        case SYSTEM_STATE_FAULT:
            updateStatusLightOutputs(false, false, true);  // Red (will blink)
            break;
        case SYSTEM_STATE_HOMING:
            updateStatusLightOutputs(false, true, false);  // Yellow (will blink)
            break;
    }
}

void statusLightUpdate(void) {
    if (!status_light_enabled) return;
    
    uint32_t now = millis();
    if (now - last_blink_time >= BLINK_INTERVAL_MS) {
        last_blink_time = now;
        blink_state = !blink_state;
        
        // Update blinking outputs based on state
        switch (current_state) {
            case SYSTEM_STATE_PAUSED:
                // Green solid, Yellow blink
                statusLightSet(status_light_pin_yellow, blink_state);
                break;
            case SYSTEM_STATE_FAULT:
                // Red blink
                statusLightSet(status_light_pin_red, blink_state);
                break;
            case SYSTEM_STATE_HOMING:
                // Yellow blink
                statusLightSet(status_light_pin_yellow, blink_state);
                break;
            default:
                // No blinking needed
                break;
        }
    }
}

void statusLightTest(void) {
    if (!status_light_enabled) {
        logWarning("[STATUS] Status light not enabled");
        return;
    }
    
    logInfo("[STATUS] Testing outputs...");
    
    // All off
    updateStatusLightOutputs(false, false, false);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Green
    logPrintln("  GREEN");
    updateStatusLightOutputs(true, false, false);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Yellow
    logPrintln("  YELLOW");
    updateStatusLightOutputs(false, true, false);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Red
    logPrintln("  RED");
    updateStatusLightOutputs(false, false, true);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // All on
    logPrintln("  ALL");
    updateStatusLightOutputs(true, true, true);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Return to current state
    statusLightSetState(current_state);
    logInfo("[STATUS] Test complete");
}

system_display_state_t statusLightGetState(void) {
    return current_state;
}

// =============================================================================
// BUZZER API
// =============================================================================

void buzzerInit(void) {
    buzzer_enabled = configGetInt(KEY_BUZZER_EN, 1) != 0;  // Default: enabled
    
    // PHASE 16: Auto-migration for buzzer
    uint16_t p = configGetInt(KEY_BUZZER_PIN, 127);
    if (p >= 1 && p <= 16) {
        buzzer_pin = p + 115;
        logInfo("[BUZZER] Migrating legacy pin %d -> Virtual %d", p, buzzer_pin);
        configSetInt(KEY_BUZZER_PIN, buzzer_pin);
    } else {
        buzzer_pin = p;
    }
    
    if (buzzer_enabled) {
        logInfo("[BUZZER] Buzzer ENABLED on Virtual Pin %d", buzzer_pin);
        plcSetOutput(buzzer_pin, false);  // Start off
    } else {
        logDebug("[BUZZER] Buzzer disabled");
    }
}

void buzzerPlay(buzzer_pattern_t pattern) {
    if (!buzzer_enabled) return;
    
    current_pattern = pattern;
    buzzer_start_time = millis();
    buzzer_step = 0;
    
    // Start first beep for most patterns
    if (pattern != BUZZER_OFF) {
        plcSetOutput(buzzer_pin, true);
    }
}

void buzzerStop(void) {
    current_pattern = BUZZER_OFF;
    plcSetOutput(buzzer_pin, false);
}

void buzzerUpdate(void) {
    if (!buzzer_enabled || current_pattern == BUZZER_OFF) return;
    
    uint32_t elapsed = millis() - buzzer_start_time;
    
    switch (current_pattern) {
        case BUZZER_BEEP_SHORT:
            if (elapsed >= BEEP_SHORT_MS) {
                buzzerStop();
            }
            break;
            
        case BUZZER_BEEP_LONG:
            if (elapsed >= BEEP_LONG_MS) {
                buzzerStop();
            }
            break;
            
        case BUZZER_BEEP_DOUBLE:
            if (buzzer_step == 0 && elapsed >= BEEP_SHORT_MS) {
                plcSetOutput(buzzer_pin, false);
                buzzer_step = 1;
                buzzer_start_time = millis();
            } else if (buzzer_step == 1 && elapsed >= BEEP_GAP_MS) {
                plcSetOutput(buzzer_pin, true);
                buzzer_step = 2;
                buzzer_start_time = millis();
            } else if (buzzer_step == 2 && elapsed >= BEEP_SHORT_MS) {
                buzzerStop();
            }
            break;
            
        case BUZZER_BEEP_TRIPLE:
            // 3 short beeps with gaps
            if (buzzer_step % 2 == 0) {  // Beep phase
                if (elapsed >= BEEP_SHORT_MS) {
                    plcSetOutput(buzzer_pin, false);
                    buzzer_step++;
                    buzzer_start_time = millis();
                    if (buzzer_step >= 5) buzzerStop();  // Done after 3 beeps
                }
            } else {  // Gap phase
                if (elapsed >= BEEP_GAP_MS) {
                    plcSetOutput(buzzer_pin, true);
                    buzzer_step++;
                    buzzer_start_time = millis();
                }
            }
            break;
            
        case BUZZER_ALARM_CONTINUOUS:
            // Stay on continuously
            break;
            
        case BUZZER_JOB_COMPLETE:
            // Distinctive pattern: long-short-short
            if (buzzer_step == 0 && elapsed >= BEEP_LONG_MS) {
                plcSetOutput(buzzer_pin, false);
                buzzer_step = 1;
                buzzer_start_time = millis();
            } else if (buzzer_step == 1 && elapsed >= BEEP_GAP_MS) {
                plcSetOutput(buzzer_pin, true);
                buzzer_step = 2;
                buzzer_start_time = millis();
            } else if (buzzer_step == 2 && elapsed >= BEEP_SHORT_MS) {
                plcSetOutput(buzzer_pin, false);
                buzzer_step = 3;
                buzzer_start_time = millis();
            } else if (buzzer_step == 3 && elapsed >= BEEP_GAP_MS) {
                plcSetOutput(buzzer_pin, true);
                buzzer_step = 4;
                buzzer_start_time = millis();
            } else if (buzzer_step == 4 && elapsed >= BEEP_SHORT_MS) {
                buzzerStop();
            }
            break;
            
        default:
            break;
    }
}

bool buzzerIsActive(void) {
    return current_pattern != BUZZER_OFF;
}

// =============================================================================
// CONVENIENCE FUNCTIONS
// =============================================================================

void alertJobComplete(void) {
    statusLightSetState(SYSTEM_STATE_IDLE);
    buzzerPlay(BUZZER_JOB_COMPLETE);
    logInfo("[ALERT] Job complete");
}

void alertFault(void) {
    statusLightSetState(SYSTEM_STATE_FAULT);
    buzzerPlay(BUZZER_ALARM_CONTINUOUS);
    logError("[ALERT] FAULT CONDITION");
}

void alertEstop(void) {
    statusLightSetState(SYSTEM_STATE_ESTOP);
    buzzerPlay(BUZZER_ALARM_CONTINUOUS);
    logError("[ALERT] E-STOP ACTIVE");
}

void alertWarning(void) {
    buzzerPlay(BUZZER_BEEP_TRIPLE);
    logWarning("[ALERT] Warning");
}

void alertsPrintStatus(void) {
    logPrintln("\n[ALERTS] === Operator Alert System ===");
    
    logPrintln("\nBuzzer:");
    logPrintf("  Enabled: %s\n", buzzer_enabled ? "YES" : "NO");
    if (buzzer_enabled) {
        logPrintf("  Pin:     Output %d\n", buzzer_pin);
        logPrintf("  Active:  %s\n", buzzerIsActive() ? "YES" : "NO");
    }
    
    logPrintln("\nStatus Light:");
    logPrintf("  Enabled: %s\n", status_light_enabled ? "YES" : "NO");
    if (status_light_enabled) {
        logPrintf("  Green:   Output %d\n", status_light_pin_green);
        logPrintf("  Yellow:  Output %d\n", status_light_pin_yellow);
        logPrintf("  Red:     Output %d\n", status_light_pin_red);
        
        const char* state_str = "UNKNOWN";
        switch (current_state) {
            case SYSTEM_STATE_IDLE:    state_str = "IDLE (Green)"; break;
            case SYSTEM_STATE_RUNNING: state_str = "RUNNING (Yellow)"; break;
            case SYSTEM_STATE_PAUSED:  state_str = "PAUSED (Green+Yellow blink)"; break;
            case SYSTEM_STATE_ESTOP:   state_str = "E-STOP (Red)"; break;
            case SYSTEM_STATE_FAULT:   state_str = "FAULT (Red blink)"; break;
            case SYSTEM_STATE_HOMING:  state_str = "HOMING (Yellow blink)"; break;
        }
        logPrintf("  State:   %s\n", state_str);
    }
}
