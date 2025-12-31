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

// Forward declare PLC interface function
extern void elboQ73SetRelay(uint8_t relay_bit, bool state);

// =============================================================================
// STATE VARIABLES
// =============================================================================

// Tower light state
static bool tower_enabled = false;
static uint8_t tower_pin_green = 13;   // Output 13 (0-indexed: 12)
static uint8_t tower_pin_yellow = 14;  // Output 14
static uint8_t tower_pin_red = 15;     // Output 15
static system_display_state_t current_state = SYSTEM_STATE_IDLE;
static uint32_t last_blink_time = 0;
static bool blink_state = false;

// Buzzer state
static bool buzzer_enabled = false;
static uint8_t buzzer_pin = 16;        // Output 16
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

static void setOutput(uint8_t pin, bool state) {
    // Convert 1-based pin to 0-based relay bit (0-15)
    if (pin >= 1 && pin <= 16) {
        elboQ73SetRelay(pin - 1, state);
    }
}

static void updateTowerOutputs(bool green, bool yellow, bool red) {
    if (!tower_enabled) return;
    setOutput(tower_pin_green, green);
    setOutput(tower_pin_yellow, yellow);
    setOutput(tower_pin_red, red);
}

// =============================================================================
// TOWER LIGHT API
// =============================================================================

void towerLightInit(void) {
    tower_enabled = configGetInt(KEY_TOWER_EN, 0) != 0;  // Default: disabled
    tower_pin_green = configGetInt(KEY_TOWER_GREEN, 13);
    tower_pin_yellow = configGetInt(KEY_TOWER_YELLOW, 14);
    tower_pin_red = configGetInt(KEY_TOWER_RED, 15);
    
    if (tower_enabled) {
        logInfo("[TOWER] Tower light ENABLED (G:%d Y:%d R:%d)", 
                tower_pin_green, tower_pin_yellow, tower_pin_red);
        // Start in idle state (green)
        towerLightSetState(SYSTEM_STATE_IDLE);
    } else {
        logDebug("[TOWER] Tower light disabled");
    }
}

void towerLightSetState(system_display_state_t state) {
    if (!tower_enabled) return;
    
    current_state = state;
    blink_state = true;  // Reset blink phase
    last_blink_time = millis();
    
    // Set initial output state
    switch (state) {
        case SYSTEM_STATE_IDLE:
            updateTowerOutputs(true, false, false);  // Green only
            break;
        case SYSTEM_STATE_RUNNING:
            updateTowerOutputs(false, true, false);  // Yellow only
            break;
        case SYSTEM_STATE_PAUSED:
            updateTowerOutputs(true, true, false);   // Green + Yellow
            break;
        case SYSTEM_STATE_ESTOP:
            updateTowerOutputs(false, false, true);  // Red only
            break;
        case SYSTEM_STATE_FAULT:
            updateTowerOutputs(false, false, true);  // Red (will blink)
            break;
        case SYSTEM_STATE_HOMING:
            updateTowerOutputs(false, true, false);  // Yellow (will blink)
            break;
    }
}

void towerLightUpdate(void) {
    if (!tower_enabled) return;
    
    uint32_t now = millis();
    if (now - last_blink_time >= BLINK_INTERVAL_MS) {
        last_blink_time = now;
        blink_state = !blink_state;
        
        // Update blinking outputs based on state
        switch (current_state) {
            case SYSTEM_STATE_PAUSED:
                // Green solid, Yellow blink
                setOutput(tower_pin_yellow, blink_state);
                break;
            case SYSTEM_STATE_FAULT:
                // Red blink
                setOutput(tower_pin_red, blink_state);
                break;
            case SYSTEM_STATE_HOMING:
                // Yellow blink
                setOutput(tower_pin_yellow, blink_state);
                break;
            default:
                // No blinking needed
                break;
        }
    }
}

void towerLightTest(void) {
    if (!tower_enabled) {
        logWarning("[TOWER] Tower light not enabled");
        return;
    }
    
    logInfo("[TOWER] Testing outputs...");
    
    // All off
    updateTowerOutputs(false, false, false);
    delay(500);
    
    // Green
    logPrintln("  GREEN");
    updateTowerOutputs(true, false, false);
    delay(1000);
    
    // Yellow
    logPrintln("  YELLOW");
    updateTowerOutputs(false, true, false);
    delay(1000);
    
    // Red
    logPrintln("  RED");
    updateTowerOutputs(false, false, true);
    delay(1000);
    
    // All on
    logPrintln("  ALL");
    updateTowerOutputs(true, true, true);
    delay(1000);
    
    // Return to current state
    towerLightSetState(current_state);
    logInfo("[TOWER] Test complete");
}

system_display_state_t towerLightGetState(void) {
    return current_state;
}

// =============================================================================
// BUZZER API
// =============================================================================

void buzzerInit(void) {
    buzzer_enabled = configGetInt(KEY_BUZZER_EN, 1) != 0;  // Default: enabled
    buzzer_pin = configGetInt(KEY_BUZZER_PIN, 16);
    
    if (buzzer_enabled) {
        logInfo("[BUZZER] Buzzer ENABLED on output %d", buzzer_pin);
        setOutput(buzzer_pin, false);  // Start off
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
        setOutput(buzzer_pin, true);
    }
}

void buzzerStop(void) {
    current_pattern = BUZZER_OFF;
    setOutput(buzzer_pin, false);
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
                setOutput(buzzer_pin, false);
                buzzer_step = 1;
                buzzer_start_time = millis();
            } else if (buzzer_step == 1 && elapsed >= BEEP_GAP_MS) {
                setOutput(buzzer_pin, true);
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
                    setOutput(buzzer_pin, false);
                    buzzer_step++;
                    buzzer_start_time = millis();
                    if (buzzer_step >= 5) buzzerStop();  // Done after 3 beeps
                }
            } else {  // Gap phase
                if (elapsed >= BEEP_GAP_MS) {
                    setOutput(buzzer_pin, true);
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
                setOutput(buzzer_pin, false);
                buzzer_step = 1;
                buzzer_start_time = millis();
            } else if (buzzer_step == 1 && elapsed >= BEEP_GAP_MS) {
                setOutput(buzzer_pin, true);
                buzzer_step = 2;
                buzzer_start_time = millis();
            } else if (buzzer_step == 2 && elapsed >= BEEP_SHORT_MS) {
                setOutput(buzzer_pin, false);
                buzzer_step = 3;
                buzzer_start_time = millis();
            } else if (buzzer_step == 3 && elapsed >= BEEP_GAP_MS) {
                setOutput(buzzer_pin, true);
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
    towerLightSetState(SYSTEM_STATE_IDLE);
    buzzerPlay(BUZZER_JOB_COMPLETE);
    logInfo("[ALERT] Job complete");
}

void alertFault(void) {
    towerLightSetState(SYSTEM_STATE_FAULT);
    buzzerPlay(BUZZER_ALARM_CONTINUOUS);
    logError("[ALERT] FAULT CONDITION");
}

void alertEstop(void) {
    towerLightSetState(SYSTEM_STATE_ESTOP);
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
    
    logPrintln("\nTower Light:");
    logPrintf("  Enabled: %s\n", tower_enabled ? "YES" : "NO");
    if (tower_enabled) {
        logPrintf("  Green:   Output %d\n", tower_pin_green);
        logPrintf("  Yellow:  Output %d\n", tower_pin_yellow);
        logPrintf("  Red:     Output %d\n", tower_pin_red);
        
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
