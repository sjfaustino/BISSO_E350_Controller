/**
 * @file test/mocks/encoder_mock.cpp
 * @brief Mock implementation of WJ66 optical encoder for testing
 */

#include "encoder_mock.h"
#include <cstdio>
#include <cstring>
#include <cmath>

encoder_mock_state_t encoder_mock_init(void)
{
    encoder_mock_state_t encoder;
    std::memset(&encoder, 0, sizeof(encoder));

    encoder.ppm = 100;                  // Default WJ66 encoding
    encoder.calibrated = 0;             // Needs calibration
    encoder.pulse_count = 0;
    encoder.position_mm = 0.0f;
    encoder.velocity_mms = 0.0f;
    encoder.target_velocity_mms = 0.0f;
    encoder.jitter_amplitude = 0.0f;
    encoder.noise_frequency_hz = 0.1f;
    encoder.last_update_ms = 0;
    encoder.comms_error = 0;
    encoder.comms_timeout = 0;
    encoder.deviation_percent = 0.0f;
    encoder.max_deviation_seen = 0.0f;
    encoder.is_moving = 0;

    return encoder;
}

void encoder_mock_calibrate(encoder_mock_state_t* encoder, uint16_t ppm)
{
    if (!encoder) return;

    encoder->ppm = ppm;
    encoder->calibrated = 1;
}

void encoder_mock_set_target_velocity(encoder_mock_state_t* encoder, float velocity_mms)
{
    if (!encoder) return;

    encoder->target_velocity_mms = velocity_mms;
    encoder->is_moving = (velocity_mms > 0.1f) ? 1 : 0;
}

void encoder_mock_advance_time(encoder_mock_state_t* encoder, uint32_t time_ms)
{
    if (!encoder || encoder->comms_error) return;

    if (!encoder->calibrated) return;

    float time_sec = time_ms / 1000.0f;
    float ppm_float = (float)encoder->ppm;

    // Calculate actual velocity (target velocity minus deviation)
    float deviation_factor = 1.0f - (encoder->deviation_percent / 100.0f);
    float actual_velocity = encoder->target_velocity_mms * deviation_factor;

    // Add jitter (sinusoidal noise on velocity)
    float noise = encoder->jitter_amplitude *
        std::sin(2.0f * 3.14159f * encoder->noise_frequency_hz *
                 encoder->last_update_ms / 1000.0f);
    encoder->velocity_mms = actual_velocity + noise;

    // Update position
    float position_change_mm = encoder->velocity_mms * time_sec;
    int32_t pulse_change = (int32_t)(position_change_mm * ppm_float);

    encoder->pulse_count += pulse_change;
    encoder->position_mm += position_change_mm;

    encoder->last_update_ms += time_ms;

    // Update deviation metric
    if (encoder->target_velocity_mms > 0.1f) {
        encoder->deviation_percent =
            std::fabs(encoder->velocity_mms - encoder->target_velocity_mms) /
            encoder->target_velocity_mms * 100.0f;

        if (encoder->deviation_percent > encoder->max_deviation_seen) {
            encoder->max_deviation_seen = encoder->deviation_percent;
        }
    } else {
        encoder->deviation_percent = 0.0f;
    }
}

void encoder_mock_inject_jitter(encoder_mock_state_t* encoder, float jitter_amplitude_mms)
{
    if (!encoder) return;

    encoder->jitter_amplitude = jitter_amplitude_mms;
}

void encoder_mock_inject_deviation(encoder_mock_state_t* encoder, float deviation_percent)
{
    if (!encoder) return;

    encoder->deviation_percent = deviation_percent;
}

void encoder_mock_inject_comms_error(encoder_mock_state_t* encoder)
{
    if (!encoder) return;

    encoder->comms_error = 1;
    encoder->comms_timeout = 1;
}

void encoder_mock_clear_comms_error(encoder_mock_state_t* encoder)
{
    if (!encoder) return;

    encoder->comms_error = 0;
    encoder->comms_timeout = 0;
}

float encoder_mock_get_position_mm(encoder_mock_state_t* encoder)
{
    if (!encoder) return 0.0f;

    return encoder->position_mm;
}

int32_t encoder_mock_get_position_pulses(encoder_mock_state_t* encoder)
{
    if (!encoder) return 0;

    return encoder->pulse_count;
}

float encoder_mock_get_velocity_mms(encoder_mock_state_t* encoder)
{
    if (!encoder) return 0.0f;

    return encoder->velocity_mms;
}

float encoder_mock_get_jitter_amplitude(encoder_mock_state_t* encoder)
{
    if (!encoder) return 0.0f;

    return encoder->jitter_amplitude;
}

float encoder_mock_get_deviation(encoder_mock_state_t* encoder)
{
    if (!encoder) return 0.0f;

    return encoder->deviation_percent;
}

uint8_t encoder_mock_is_calibrated(encoder_mock_state_t* encoder)
{
    if (!encoder) return 0;

    return encoder->calibrated;
}

uint8_t encoder_mock_has_error(encoder_mock_state_t* encoder)
{
    if (!encoder) return 0;

    return encoder->comms_error || encoder->comms_timeout;
}

void encoder_mock_reset_position(encoder_mock_state_t* encoder)
{
    if (!encoder) return;

    encoder->pulse_count = 0;
    encoder->position_mm = 0.0f;
    encoder->max_deviation_seen = 0.0f;
}

void encoder_mock_get_status(encoder_mock_state_t* encoder, char* buffer, size_t buffer_size)
{
    if (!encoder || !buffer) return;

    const char* cal_str = encoder->calibrated ? "CAL" : "NOT_CAL";
    const char* err_str = encoder->comms_error ? "ERROR" : "OK";
    const char* moving_str = encoder->is_moving ? "MOVING" : "IDLE";

    snprintf(buffer, buffer_size,
        "ENC[%s] PPM:%u Pos:%.1fmm Vel:%.1fmm/s Jitter:%.2fmm/s Dev:%.1f%% %s %s",
        err_str,
        encoder->ppm,
        encoder->position_mm,
        encoder->velocity_mms,
        encoder->jitter_amplitude,
        encoder->deviation_percent,
        moving_str,
        cal_str);
}
