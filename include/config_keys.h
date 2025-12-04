#ifndef CONFIG_KEYS_H
#define CONFIG_KEYS_H

/**
 * @file config_keys.h
 * @brief Centralized definition of all NVS configuration keys.
 * @project Gemini v1.0.0
 * @author Sergio Faustino - sjfaustino@gmail.com
 */

// ============================================================================
// MOTION LIMITS (Soft Limits)
// ============================================================================
static const char* const KEY_X_LIMIT_MIN = "x_soft_limit_min";
static const char* const KEY_X_LIMIT_MAX = "x_soft_limit_max";

static const char* const KEY_Y_LIMIT_MIN = "y_soft_limit_min";
static const char* const KEY_Y_LIMIT_MAX = "y_soft_limit_max";

static const char* const KEY_Z_LIMIT_MIN = "z_soft_limit_min";
static const char* const KEY_Z_LIMIT_MAX = "z_soft_limit_max";

static const char* const KEY_A_LIMIT_MIN = "a_soft_limit_min";
static const char* const KEY_A_LIMIT_MAX = "a_soft_limit_max";

// ============================================================================
// ENCODER CALIBRATION (Pulses Per Millimeter/Degree)
// ============================================================================
static const char* const KEY_PPM_X = "encoder_ppm_x";
static const char* const KEY_PPM_Y = "encoder_ppm_y";
static const char* const KEY_PPM_Z = "encoder_ppm_z";
static const char* const KEY_PPM_A = "encoder_ppm_a";

// ============================================================================
// SPEED PROFILES (Calibrated Feed Rates)
// ============================================================================
static const char* const KEY_SPEED_CAL_X = "speed_X_mm_s";
static const char* const KEY_SPEED_CAL_Y = "speed_Y_mm_s";
static const char* const KEY_SPEED_CAL_Z = "speed_Z_mm_s";
static const char* const KEY_SPEED_CAL_A = "speed_A_mm_s";

// ============================================================================
// BACKLASH & PITCH COMPENSATION
// ============================================================================
static const char* const KEY_BACKLASH_X = "backlash_x";
static const char* const KEY_BACKLASH_Y = "backlash_y";
static const char* const KEY_BACKLASH_Z = "backlash_z";
static const char* const KEY_BACKLASH_A = "backlash_a";

static const char* const KEY_PITCH_X = "pitch_x";
static const char* const KEY_PITCH_Y = "pitch_y";
static const char* const KEY_PITCH_Z = "pitch_z";
static const char* const KEY_PITCH_A = "pitch_a";

// ============================================================================
// MOTION BEHAVIOR & TUNING
// ============================================================================
static const char* const KEY_DEFAULT_SPEED = "default_speed_mm_s";
static const char* const KEY_DEFAULT_ACCEL = "default_acceleration";

// Final approach distance for X-axis (mm)
static const char* const KEY_X_APPROACH = "x_approach_mm";

// Encoder count deadband for declaring motion "stopped"
static const char* const KEY_MOTION_DEADBAND = "motion_settle_deadband";

// ============================================================================
// SAFETY & HARDWARE
// ============================================================================
static const char* const KEY_ALARM_PIN = "alarm_pin";
static const char* const KEY_STALL_TIMEOUT = "stall_timeout_ms";

// ============================================================================
// SYSTEM STATE
// ============================================================================
static const char* const KEY_SCHEMA_VERSION = "schema_version";

#endif // CONFIG_KEYS_H