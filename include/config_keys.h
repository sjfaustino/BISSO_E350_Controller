/**
 * @file config_keys.h
 * @brief Central registry of NVS Configuration Keys (Gemini v3.5.22)
 */

#ifndef CONFIG_KEYS_H
#define CONFIG_KEYS_H

// --- MOTION LIMITS ---
#define KEY_X_LIMIT_MIN "x_limit_min"
#define KEY_X_LIMIT_MAX "x_limit_max"
#define KEY_Y_LIMIT_MIN "y_limit_min"
#define KEY_Y_LIMIT_MAX "y_limit_max"
#define KEY_Z_LIMIT_MIN "z_limit_min"
#define KEY_Z_LIMIT_MAX "z_limit_max"
#define KEY_A_LIMIT_MIN "a_limit_min"
#define KEY_A_LIMIT_MAX "a_limit_max"

// --- ENCODER ---
#define KEY_PPM_X "ppm_x"
#define KEY_PPM_Y "ppm_y"
#define KEY_PPM_Z "ppm_z"
#define KEY_PPM_A "ppm_a"
#define KEY_ENC_ERR_THRESHOLD "enc_thresh"
#define KEY_ENC_INTERFACE "enc_iface"     // WJ66 interface type (0=RS232_HT, 1=RS485_RXD2, 255=CUSTOM)
#define KEY_ENC_BAUD "enc_baud"           // WJ66 baud rate

// --- SPINDLE CURRENT SENSOR ---
#define KEY_SPINDLE_ENABLED "spindl_en"   // Enable/disable spindle current monitoring (0=disable, 1=enable)
#define KEY_SPINDLE_ADDRESS "spindl_addr" // JXK-10 Modbus slave address (1-247, default 1)
#define KEY_SPINDLE_THRESHOLD "spindl_thr"// Overcurrent threshold in integer amperes (30 = 30A)
#define KEY_SPINDLE_POLL_MS "spindl_poll" // Poll interval in milliseconds (default 1000) 

// --- SPEED PROFILES ---
#define KEY_SPEED_CAL_X "spd_x"
#define KEY_SPEED_CAL_Y "spd_y"
#define KEY_SPEED_CAL_Z "spd_z"
#define KEY_SPEED_CAL_A "spd_a"

// --- HOMING ---
#define KEY_HOME_PROFILE_FAST "home_prof_fast" 
#define KEY_HOME_PROFILE_SLOW "home_prof_slow"

// --- MOTION BEHAVIOR ---
#define KEY_DEFAULT_SPEED "def_spd"
#define KEY_DEFAULT_ACCEL "def_acc"
#define KEY_X_APPROACH    "x_appr"
#define KEY_MOTION_APPROACH_MODE "mot_app_mode"
#define KEY_MOTION_DEADBAND "mot_deadband"
#define KEY_MOTION_BUFFER_ENABLE "mot_buf_en"
#define KEY_MOTION_STRICT_LIMITS "mot_strict"
#define KEY_STOP_TIMEOUT "stop_timeout"

// --- SAFETY ---
#define KEY_ALARM_PIN "alarm_pin"
#define KEY_STALL_TIMEOUT "stall_ms"

// --- WCS OFFSETS (Generated Keys) ---
// Base keys, suffixes will be appended (e.g. "g54_x")
#define KEY_WCS_PREFIX "g" 

#endif