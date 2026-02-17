#include "engineering_menu.h"
#include "board_variant.h"
#include "serial_logger.h"
#include "config_unified.h"
#include "config_keys.h"
#include "motion.h"
#include "fault_logging.h"
#include "lcd_interface.h"
#include "operator_alerts.h"
#include "altivar31_modbus.h"
#include <string.h>

// External diagnostic helper
void systemDumpDiagnostics();

EngineeringMenu engineeringMenu;

EngineeringMenu::EngineeringMenu() : 
    BaseMenu(4),
    m_state(STATE_INACTIVE), 
    m_prev_state(STATE_INACTIVE),
    m_last_interaction_time(0), 
    m_entry_press_count(0), 
    m_entry_last_press_time(0),
    m_pending_serial_dest(0),
    m_pending_lights_en(0),
    m_pending_vfd_en(0),
    m_raw_button_state(true),
    m_last_debounce_time(0),
    m_debounced_state(true),
    m_button_down_time(0),
    m_long_press_handled(false)
{
}

void EngineeringMenu::init() {
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
    m_raw_button_state = m_debounced_state = digitalRead(PIN_BOOT_BUTTON);
}

void EngineeringMenu::update() {
    if (!lcdInterfaceIsHardwarePresent()) {
        if (m_state != STATE_INACTIVE) exitMenu();
        return;
    }

    uint32_t now = millis();
    bool raw = digitalRead(PIN_BOOT_BUTTON);

    // 1. EMI Filter (Debouncer)
    // Low-pass: State must be stable for >15ms
    if (raw != m_raw_button_state) {
        m_last_debounce_time = now;
        m_raw_button_state = raw;
    }

    if (now - m_last_debounce_time > 15) {
        if (raw != m_debounced_state) {
            bool old_state = m_debounced_state;
            m_debounced_state = raw;

            // Handle Transitions
            if (m_debounced_state == LOW && old_state == HIGH) {
                // Pressed
                m_button_down_time = now;
                m_long_press_handled = false;
                
                if (m_state == STATE_INACTIVE) {
                    if (now - m_entry_last_press_time > 1000) m_entry_press_count = 1;
                    else m_entry_press_count++;
                    m_entry_last_press_time = now;
                    
                    if (m_entry_press_count >= 3) {
                        enterMenu();
                        m_entry_press_count = 0;
                    }
                } else {
                    m_last_interaction_time = now;
                }
            } 
            else if (m_debounced_state == HIGH && old_state == LOW) {
                // Released
                uint32_t duration = now - m_button_down_time;
                if (m_state != STATE_INACTIVE && !m_long_press_handled && duration < 500) {
                    nextOption();
                }
            }
        }
    }

    // 2. Long Press Logic (Independent of stable state release)
    if (m_debounced_state == LOW && m_state != STATE_INACTIVE && !m_long_press_handled) {
        if (now - m_button_down_time > 800) {
            selectOption();
            m_long_press_handled = true;
        }
    }

    // 3. Inactivity Timeout Logic
    if (m_state != STATE_INACTIVE && (now - m_last_interaction_time > 10000) && (m_debounced_state == HIGH)) {
        if (m_state == STATE_MAIN || m_state == STATE_HARDWARE || m_state == STATE_DIAGS || m_state == STATE_SYSTEM || m_state == STATE_MODBUS_HEALTH || m_state == STATE_VIEW_ALARMS) {
            bool changed = (m_pending_serial_dest != configGetInt(KEY_SERIAL_DEST, 0)) ||
                          (m_pending_lights_en != configGetInt(KEY_STATUS_LIGHT_EN, 0)) ||
                          (m_pending_vfd_en != configGetInt(KEY_VFD_EN, 1));

            if (changed) {
                m_prev_state = m_state;
                m_state = STATE_CONFIRM_SAVE;
                m_selection = 1; // Default to SAVE
                m_last_interaction_time = now;
                refreshMenuLines();
            } else {
                exitMenu();
            }
        } else {
            // Auto-cancel if no confirmation
            handleConfirmation(false);
        }
    }
}

void EngineeringMenu::enterMenu() {
    m_state = STATE_MAIN;
    m_selection = 0;
    m_last_interaction_time = millis();
    
    m_pending_serial_dest = configGetInt(KEY_SERIAL_DEST, 0);
    m_pending_lights_en = configGetInt(KEY_STATUS_LIGHT_EN, 0);
    m_pending_vfd_en = configGetInt(KEY_VFD_EN, 1);
    
    if (motionIsMoving()) motionPause();
    
    // Visual feedback: Yellow light while in maintenance
    statusLightSetState(SYSTEM_STATE_RUNNING); // Yellow solid in current implementation
    
    refreshMenuLines();
}

void EngineeringMenu::exitMenu() {
    m_state = STATE_INACTIVE;
    // Restore light state based on system state
    statusLightSetState(motionIsMoving() ? SYSTEM_STATE_RUNNING : SYSTEM_STATE_IDLE);
}

void EngineeringMenu::nextOption() {
    m_last_interaction_time = millis();
    
    // Audio feedback: Short beep
    if (configGetInt(KEY_BUZZER_EN, 1) != 0) {
        buzzerPlay(BUZZER_BEEP_SHORT);
    }

    if (m_state == STATE_MAIN) {
        m_selection = (m_selection + 1) % 4;
    } else if (m_state == STATE_HARDWARE) {
        m_selection = (m_selection + 1) % 4;
    } else if (m_state == STATE_DIAGS) {
        m_selection = (m_selection + 1) % 5;
    } else if (m_state == STATE_SYSTEM) {
        m_selection = (m_selection + 1) % 3;
    } else if (m_state == STATE_MODBUS_HEALTH || m_state == STATE_VIEW_ALARMS) {
        m_selection = 0; // Toggle only one button (BACK)
    } else if (m_state == STATE_CONFIRM_SAVE || m_state == STATE_CONFIRM_RESET) {
        m_selection = (m_selection == 0) ? 1 : 0;
    }
    
    refreshMenuLines();
}

void EngineeringMenu::selectOption() {
    m_last_interaction_time = millis();
    
    // Audio feedback: Long beep
    if (configGetInt(KEY_BUZZER_EN, 1) != 0) {
        buzzerPlay(BUZZER_BEEP_LONG);
    }

    if (m_state == STATE_MAIN) {
        if (m_selection == 0) {
            m_state = STATE_HARDWARE;
            m_selection = 0;
        }
        else if (m_selection == 1) {
            m_state = STATE_DIAGS;
            m_selection = 0;
        }
        else if (m_selection == 2) {
            m_state = STATE_SYSTEM;
            m_selection = 0;
        }
        else if (m_selection == 3) {
            // EXIT
            bool changed = (m_pending_serial_dest != configGetInt(KEY_SERIAL_DEST, 0)) ||
                          (m_pending_lights_en != configGetInt(KEY_STATUS_LIGHT_EN, 0)) ||
                          (m_pending_vfd_en != configGetInt(KEY_VFD_EN, 1));

            if (changed) {
                m_prev_state = STATE_MAIN;
                m_state = STATE_CONFIRM_SAVE;
                m_selection = 1; // Default to SAVE
                m_last_interaction_time = millis();
            } else {
                exitMenu();
                return;
            }
        }
    } else if (m_state == STATE_HARDWARE) {
        if (m_selection == 0) m_pending_serial_dest = !m_pending_serial_dest;
        else if (m_selection == 1) m_pending_lights_en = !m_pending_lights_en;
        else if (m_selection == 2) m_pending_vfd_en = !m_pending_vfd_en;
        else if (m_selection == 3) {
            m_state = STATE_MAIN;
            m_selection = 0;
        }
        
        if (m_state == STATE_HARDWARE) {
            applyHardwareState(m_pending_serial_dest, m_pending_lights_en, m_pending_vfd_en);
        }
    } else if (m_state == STATE_DIAGS) {
        if (m_selection == 0) {
            m_state = STATE_VIEW_ALARMS;
            m_selection = 0;
        }
        else if (m_selection == 1) {
            // Log Level
            log_level_t current = serialLoggerGetLevel();
            serialLoggerSetLevel(current == LOG_LEVEL_DEBUG ? LOG_LEVEL_INFO : LOG_LEVEL_DEBUG);
        }
        else if (m_selection == 2) {
            m_state = STATE_MODBUS_HEALTH;
            m_selection = 0;
        }
        else if (m_selection == 3) dumpDiagnostics();
        else if (m_selection == 4) {
            m_state = STATE_MAIN;
            m_selection = 1;
        }
    } else if (m_state == STATE_VIEW_ALARMS) {
        m_state = STATE_DIAGS;
        m_selection = 0;
    } else if (m_state == STATE_MODBUS_HEALTH) {
        uint8_t count = 0;
        rs485GetDevices(&count);
        if (m_selection == count) {
            m_state = STATE_DIAGS;
            m_selection = 1;
        }
    } else if (m_state == STATE_SYSTEM) {
        if (m_selection == 0) ESP.restart();
        else if (m_selection == 1) {
            m_state = STATE_CONFIRM_RESET;
            m_selection = 0;
        }
        else if (m_selection == 2) {
            m_state = STATE_MAIN;
            m_selection = 4;
        }
    }
 else if (m_state == STATE_CONFIRM_SAVE) {
        handleConfirmation(m_selection == 1);
    } else if (m_state == STATE_CONFIRM_RESET) {
        if (m_selection == 1) performFactoryReset();
        else {
            m_state = STATE_SYSTEM;
            m_selection = 2;
        }
    }
    
    refreshMenuLines();
}

void EngineeringMenu::applyHardwareState(int dest, int lights, int vfd) {
    if (dest == 1) {
        static HardwareSerial* AltSerial = nullptr;
        if (!AltSerial) {
            AltSerial = new HardwareSerial(1);
            AltSerial->begin(115200, SERIAL_8N1, PIN_ALT_UART_RX, PIN_ALT_UART_TX);
        }
        serialLoggerSetStream(AltSerial);
    } else {
        serialLoggerSetStream(&Serial);
    }
    statusLightSetEnabled(lights != 0);
    Altivar31.setEnabled(vfd != 0);
}

void EngineeringMenu::handleConfirmation(bool confirmed) {
    if (confirmed) {
        configSetInt(KEY_SERIAL_DEST, m_pending_serial_dest);
        configSetInt(KEY_STATUS_LIGHT_EN, m_pending_lights_en);
        configSetInt(KEY_VFD_EN, m_pending_vfd_en);
        configUnifiedSave();
    } else {
        applyHardwareState(
            configGetInt(KEY_SERIAL_DEST, 0),
            configGetInt(KEY_STATUS_LIGHT_EN, 0),
            configGetInt(KEY_VFD_EN, 1)
        );
    }
    exitMenu();
}

void EngineeringMenu::dumpDiagnostics() {
    systemDumpDiagnostics();
}

void EngineeringMenu::performFactoryReset() {
    configUnifiedReset();
    ESP.restart();
}

void EngineeringMenu::refreshMenuLines() {
    clearLines();
    
    if (m_state == STATE_MAIN) {
        setLine(0, "== ENGINEER MENU ==");
        setLine(1, "%c1.Hardware  >>", (m_selection == 0 ? '>' : ' '));
        setLine(2, "%c2.Diags     >>", (m_selection == 1 ? '>' : ' '));
        setLine(3, "%c3.Sys Utils >>", (m_selection == 2 ? '>' : ' '));
        // Manual wrapping for EXIT on small screen
        if (m_selection == 3) {
            setLine(1, " 2.Diags     >>");
            setLine(2, " 3.Sys Utils >>");
            setLine(3, "%c4.EXIT", '>');
        }
    } else if (m_state == STATE_HARDWARE) {
        setLine(0, "== HARDWARE CTRL ==");
        setLine(1, "%c1.Serial: %s", (m_selection == 0 ? '>' : ' '), (m_pending_serial_dest ? "UART" : "USB"));
        setLine(2, "%c2.Lights: %s", (m_selection == 1 ? '>' : ' '), (m_pending_lights_en ? "ON" : "OFF"));
        setLine(3, "%c3.VFD:    %s", (m_selection == 2 ? '>' : ' '), (m_pending_vfd_en ? "ON" : "OFF"));
        if (m_selection == 3) {
            setLine(1, " 2.Lights: %s", (m_pending_lights_en ? "ON" : "OFF"));
            setLine(2, " 3.VFD:    %s", (m_pending_vfd_en ? "ON" : "OFF"));
            setLine(3, "%c4.BACK", '>');
        }
    } else if (m_state == STATE_DIAGS) {
        setLine(0, "== DIAGNOSTICS ==");
        setLine(1, "%c1.View Alarms >>", (m_selection == 0 ? '>' : ' '));
        setLine(2, "%c2.LogLvl: %s", (m_selection == 1 ? '>' : ' '), (serialLoggerGetLevel() >= LOG_LEVEL_DEBUG ? "DEBUG" : "INFO"));
        if (m_selection < 3) {
            setLine(3, "%c3.Modbus Health >>", (m_selection == 2 ? '>' : ' '));
        } else if (m_selection == 3) {
            setLine(2, " 3.Modbus Health >>");
            setLine(3, "%c4.Diag Dump (Ser)", '>');
        } else {
            setLine(2, " 4.Diag Dump (Ser)");
            setLine(3, "%c5.BACK", '>');
        }
    } else if (m_state == STATE_VIEW_ALARMS) {
        setLine(0, "== RECENT ALARMS ==");
        uint8_t count = faultGetRingBufferEntryCount();
        if (count == 0) {
            setLine(1, " [ No Alarms ]");
            setLine(2, "");
        } else {
            // Show up to 2 entries
            for (int i = 0; i < 2 && i < count; i++) {
                const fault_entry_t* fe = faultGetRingBufferEntry(i);
                if (fe) {
                    setLine(i + 1, "%d:%-17.17s", fe->code, faultCodeToString(fe->code));
                }
            }
        }
        setLine(3, "%cBACK", '>');
    } else if (m_state == STATE_MODBUS_HEALTH) {
        uint8_t count = 0;
        rs485_device_t** devices = rs485GetDevices(&count);
        setLine(0, "== MODBUS HEALTH ==");
        
        if (m_selection < count) {
            rs485_device_t* d = devices[m_selection];
            setLine(1, "Dev: %s", d->name);
            setLine(2, "OK: %lu ERR: %lu", d->poll_count, d->error_count);
            setLine(3, " [Next/Back: Click]");
        } else {
            setLine(1, "End of Slaves");
            setLine(2, "");
            setLine(3, "%cBACK", '>');
        }
    } else if (m_state == STATE_SYSTEM) {
        setLine(0, "== SYSTEM UTILS ==");
        setLine(1, "%c1.Reboot", (m_selection == 0 ? '>' : ' '));
        setLine(2, "%c2.Factory Reset >>", (m_selection == 1 ? '>' : ' '));
        setLine(3, "%c3.BACK", (m_selection == 2 ? '>' : ' '));
    } else if (m_state == STATE_CONFIRM_SAVE) {
        setLine(0, "== SAVE CHANGES? ==");
        setLine(1, "%c CANCEL", (m_selection == 0 ? '>' : ' '));
        setLine(2, "%c SAVE",   (m_selection == 1 ? '>' : ' '));
        setLine(3, "Long press select");
    } else if (m_state == STATE_CONFIRM_RESET) {
        setLine(0, "== FACTORY RESET? ==");
        setLine(1, "%c NO (Cancel)", (m_selection == 0 ? '>' : ' '));
        setLine(2, "%c YES (Erase)",  (m_selection == 1 ? '>' : ' '));
        setLine(3, "CAUTION: Erase all!");
    }
}
