/**
 * @file test/mocks/vfd_mock.cpp
 * @brief Mock implementation of Altivar 31 VFD for testing
 */

#include "vfd_mock.h"
#include <cmath>
#include <cstdio>
#include <cstring>

vfd_mock_state_t vfd_mock_init(void)
{
    vfd_mock_state_t vfd;
    std::memset(&vfd, 0, sizeof(vfd));

    // Default Altivar 31 configuration
    vfd.hsp = 105;                     // High Speed Preset
    vfd.lsp = 1;                       // Low Speed Preset
    vfd.acc_decel_time_10s = 6;        // 0.6s acceleration
    vfd.dec_decel_time_10s = 4;        // 0.4s deceleration
    vfd.frequency_hz = 0;              // Start at idle
    vfd.target_frequency_hz = 0;
    vfd.motor_current_amps = 0.0f;
    vfd.motor_temperature_c = 25.0f;
    vfd.is_running = 0;
    vfd.has_fault = 0;
    vfd.fault_code = 0;

    // Calculate ramp rates based on acceleration times
    float acc_time_sec = vfd.acc_decel_time_10s * 0.1f;
    vfd.acceleration_hz_per_ms = 105.0f / (acc_time_sec * 1000.0f);

    float dec_time_sec = vfd.dec_decel_time_10s * 0.1f;
    vfd.deceleration_hz_per_ms = 105.0f / (dec_time_sec * 1000.0f);

    return vfd;
}

void vfd_mock_set_frequency(vfd_mock_state_t* vfd, uint16_t target_hz)
{
    if (!vfd || vfd->has_fault) return;

    // Clamp to HSP/LSP limits
    if (target_hz > vfd->hsp) {
        target_hz = vfd->hsp;
    }
    if (target_hz > 0 && target_hz < vfd->lsp) {
        target_hz = vfd->lsp;
    }

    vfd->target_frequency_hz = target_hz;
    vfd->is_running = (target_hz > 0) ? 1 : 0;
}

void vfd_mock_advance_time(vfd_mock_state_t* vfd, uint32_t time_ms)
{
    if (!vfd || vfd->has_fault) return;

    float time_sec = time_ms / 1000.0f;
    float current_freq = (float)vfd->frequency_hz;
    float target_freq = (float)vfd->target_frequency_hz;

    if (current_freq < target_freq) {
        // Accelerating
        float max_step = vfd->acceleration_hz_per_ms * time_ms;
        current_freq += max_step;
        if (current_freq > target_freq) {
            current_freq = target_freq;
        }
    } else if (current_freq > target_freq) {
        // Decelerating
        float max_step = vfd->deceleration_hz_per_ms * time_ms;
        current_freq -= max_step;
        if (current_freq < target_freq) {
            current_freq = target_freq;
        }
    }

    vfd->frequency_hz = (uint16_t)current_freq;

    // Update motor current based on frequency
    vfd->motor_current_amps = vfd_mock_get_motor_current(vfd);

    // Update motor temperature
    vfd_mock_update_temperature(vfd, time_ms);
}

float vfd_mock_get_motor_current(vfd_mock_state_t* vfd)
{
    if (!vfd) return 0.0f;

    // Realistic 3-phase induction motor current curve
    // At 0 Hz: 0 A, at HSP (105 Hz): 5.5 A typical
    float freq = (float)vfd->frequency_hz;
    float hsp = (float)vfd->hsp;

    if (freq < 1.0f) return 0.0f;

    // Approximate motor current vs frequency relationship
    // Starts at base current, increases roughly linearly with frequency
    float base_current = 0.2f;  // Idle current
    float max_current = 5.5f;   // Max at HSP
    float current = base_current + (freq / hsp) * (max_current - base_current);

    return current;
}

void vfd_mock_update_temperature(vfd_mock_state_t* vfd, uint32_t time_ms)
{
    if (!vfd) return;

    float time_sec = time_ms / 1000.0f;
    float ambient_temp = 25.0f;

    // Temperature rises with I²R heating
    float current = vfd->motor_current_amps;
    float heating_rate = current * current * 0.001f;  // Watts per amp²

    // Temperature falls exponentially to ambient when idle
    float temp_diff = vfd->motor_temperature_c - ambient_temp;
    float cooling_rate = 0.01f;  // Degrees per second per degree difference

    vfd->motor_temperature_c += (heating_rate - cooling_rate * temp_diff) * time_sec;

    // Clamp to reasonable ranges
    if (vfd->motor_temperature_c < ambient_temp) {
        vfd->motor_temperature_c = ambient_temp;
    }
    if (vfd->motor_temperature_c > 85.0f) {
        vfd->motor_temperature_c = 85.0f;  // Thermal cutoff
        vfd_mock_inject_fault(vfd, 13);    // Fault code 13: thermal
    }
}

void vfd_mock_inject_fault(vfd_mock_state_t* vfd, uint8_t fault_code)
{
    if (!vfd) return;

    vfd->has_fault = 1;
    vfd->fault_code = fault_code;
    vfd->is_running = 0;
    vfd->frequency_hz = 0;
    vfd->target_frequency_hz = 0;
}

void vfd_mock_clear_fault(vfd_mock_state_t* vfd)
{
    if (!vfd) return;

    vfd->has_fault = 0;
    vfd->fault_code = 0;
}

uint8_t vfd_mock_is_at_frequency(vfd_mock_state_t* vfd, uint16_t tolerance_hz)
{
    if (!vfd) return 0;

    int16_t diff = (int16_t)vfd->frequency_hz - (int16_t)vfd->target_frequency_hz;
    return (diff >= -tolerance_hz && diff <= tolerance_hz) ? 1 : 0;
}

void vfd_mock_get_status(vfd_mock_state_t* vfd, char* buffer, size_t buffer_size)
{
    if (!vfd || !buffer) return;

    const char* fault_str = vfd->has_fault ? "FAULT" : "OK";
    const char* running_str = vfd->is_running ? "RUN" : "STOP";

    snprintf(buffer, buffer_size,
        "VFD[%s] Freq:%uHz Target:%uHz Current:%.1fA Temp:%.0fC",
        fault_str,
        vfd->frequency_hz,
        vfd->target_frequency_hz,
        vfd->motor_current_amps,
        vfd->motor_temperature_c);
}
