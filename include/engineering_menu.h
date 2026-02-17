#ifndef ENGINEERING_MENU_H
#define ENGINEERING_MENU_H

#include <Arduino.h>
#include "ui_menu_base.h"

/**
 * @file engineering_menu.h
 * @brief Logic for BOOT button triggered engineering menu
 */

class EngineeringMenu : public BaseMenu {
public:
    enum MenuState {
        STATE_INACTIVE,
        STATE_MAIN,
        STATE_HARDWARE,     // Sub-menu for Serial/Lights/VFD
        STATE_DIAGS,        // Sub-menu for Log/Modbus/Dump
        STATE_SYSTEM,       // Sub-menu for Reboot/Reset
        STATE_MODBUS_HEALTH, // RS485 Device Diagnostics
        STATE_VIEW_ALARMS,   // Show recent faults
        STATE_CONFIRM_SAVE,
        STATE_CONFIRM_RESET
    };

     EngineeringMenu();
    void init();
    void update();
    
    bool isActive() const { return m_state != STATE_INACTIVE; }

private:
    void enterMenu();
    void exitMenu();
    
    // Framework overrides/additions
    void nextOption();   // Short press
    void selectOption(); // Long press
    
    void handleConfirmation(bool confirmed);
    void applyHardwareState(int dest, int lights, int vfd);
    void dumpDiagnostics();
    void performFactoryReset();

    MenuState m_state;
    MenuState m_prev_state; // For returning from confirmation
    
    uint32_t m_last_interaction_time;
    uint8_t m_entry_press_count;
    uint32_t m_entry_last_press_time;
    
    // Pending settings (in-memory)
    int m_pending_serial_dest;
    int m_pending_lights_en;
    int m_pending_vfd_en;
    
    // Debouncing (EMI Filter)
    bool m_raw_button_state;
    uint32_t m_last_debounce_time;
    bool m_debounced_state;
    
    // Button Logic
    uint32_t m_button_down_time;
    bool m_long_press_handled;
    
    void refreshMenuLines();
};

extern EngineeringMenu engineeringMenu;

#endif // ENGINEERING_MENU_H
