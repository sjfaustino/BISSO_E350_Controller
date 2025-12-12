/**
 * @file spindle_current_rs485.cpp
 * @brief RS485 Multiplexer Implementation
 * @project BISSO E350 Controller - Phase 5.0
 */

#include "spindle_current_rs485.h"
#include "serial_logger.h"
#include <Arduino.h>

// Global multiplexer state
static rs485_mux_state_t mux_state = {
    .current_device = RS485_DEVICE_ENCODER,
    .pending_device = RS485_DEVICE_ENCODER,
    .last_switch_time_ms = 0,
    .inter_frame_delay_ms = 10,
    .need_switch = false,
    .tx_count = 0,
    .rx_count = 0,
    .error_count = 0
};

bool rs485MuxInit(void) {
    mux_state.current_device = RS485_DEVICE_ENCODER;
    mux_state.pending_device = RS485_DEVICE_ENCODER;
    mux_state.last_switch_time_ms = millis();
    mux_state.inter_frame_delay_ms = 10;
    mux_state.need_switch = false;
    mux_state.tx_count = 0;
    mux_state.rx_count = 0;
    mux_state.error_count = 0;

    Serial.println("[RS485-MUX] Initialized (Encoder primary, 10ms inter-frame delay)");
    return true;
}

bool rs485MuxSwitchDevice(rs485_device_t device) {
    if (device == mux_state.current_device) {
        return false;  // Already on that device
    }

    mux_state.pending_device = device;
    mux_state.need_switch = true;
    return true;
}

bool rs485MuxCanSwitch(void) {
    if (!mux_state.need_switch) {
        return false;
    }

    uint32_t now = millis();
    uint32_t elapsed = now - mux_state.last_switch_time_ms;

    return (elapsed >= mux_state.inter_frame_delay_ms);
}

rs485_device_t rs485MuxGetCurrentDevice(void) {
    return mux_state.current_device;
}

bool rs485MuxUpdate(void) {
    if (!mux_state.need_switch) {
        return false;
    }

    if (!rs485MuxCanSwitch()) {
        return false;  // Still waiting for inter-frame delay
    }

    // Perform the switch
    mux_state.current_device = mux_state.pending_device;
    mux_state.last_switch_time_ms = millis();
    mux_state.need_switch = false;

    const char* device_name = (mux_state.current_device == RS485_DEVICE_ENCODER) ? "Encoder" : "Spindle";
    Serial.printf("[RS485-MUX] Switched to %s\n", device_name);

    return true;
}

bool rs485MuxSetInterFrameDelay(uint32_t delay_ms) {
    if (delay_ms < 1 || delay_ms > 1000) {
        return false;  // Out of reasonable range
    }
    mux_state.inter_frame_delay_ms = delay_ms;
    return true;
}

uint32_t rs485MuxGetInterFrameDelay(void) {
    return mux_state.inter_frame_delay_ms;
}

const rs485_mux_state_t* rs485MuxGetState(void) {
    return &mux_state;
}

void rs485MuxPrintDiagnostics(void) {
    Serial.println("\n[RS485-MUX] === Diagnostics ===");
    Serial.printf("Current Device:      %s\n", (mux_state.current_device == RS485_DEVICE_ENCODER) ? "Encoder" : "Spindle");
    Serial.printf("Inter-Frame Delay:   %lu ms\n", (unsigned long)mux_state.inter_frame_delay_ms);
    Serial.printf("TX Count:            %lu\n", (unsigned long)mux_state.tx_count);
    Serial.printf("RX Count:            %lu\n", (unsigned long)mux_state.rx_count);
    Serial.printf("Error Count:         %lu\n", (unsigned long)mux_state.error_count);
}
