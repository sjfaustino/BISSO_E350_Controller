/**
 * @file config_keys.h
 * @brief Central registry of NVS Configuration Keys (Gemini v3.5.22)
 */

#ifndef CONFIG_KEYS_H
#define CONFIG_KEYS_H

/**
 * ⚠️ NVS KEY LENGTH LIMIT WARNING ⚠️
 *
 * ESP-IDF NVS (Non-Volatile Storage) has a strict 15-character limit for key
 * names. All keys defined in this file MUST be strictly < 15 characters to
 * avoid silent truncation or NVS errors.
 *
 * Current longest keys (14 chars - at safe limit):
 * - "home_prof_fast" (14 chars)
 * - "home_prof_slow" (14 chars)
 * - "vfd_stall_marg" (14 chars)
 * - "mot_app_mode"   (12 chars) ✓
 *
 * When adding new keys:
 * 1. Keep names descriptive but concise
 * 2. Use abbreviations (e.g., "thr" for threshold, "en" for enable, "prof" for
 * profile)
 * 3. Verify length before committing: strlen("your_key_name") < 15
 * 4. Test with actual NVS storage (nvs_set_* will fail silently if key too
 * long)
 *
 * Example safe abbreviations:
 * - "threshold" → "thr" or "thresh"
 * - "enable" → "en"
 * - "profile" → "prof"
 * - "position" → "pos"
 * - "calibration" → "cal"
 * - "spindle" → "spindl"
 * - "temperature" → "temp"
 */

// --- WIFI CONFIGURATION ---
#define KEY_WIFI_SSID "wifi_ssid"       // Station SSID to connect to
#define KEY_WIFI_PASS "wifi_pass"       // Station password
#define KEY_WIFI_AP_EN "wifi_ap_en"     // AP mode enable (1=on, 0=off)
#define KEY_WIFI_AP_SSID "wifi_ap_ssid" // AP broadcast SSID
#define KEY_WIFI_AP_PASS "wifi_ap_pass" // AP password (min 8 chars)

// --- ETHERNET CONFIGURATION (KC868-A16 LAN8720) ---
#define KEY_ETH_ENABLED "eth_en"        // Ethernet enable (1=on, 0=off, default 1)
#define KEY_ETH_DHCP "eth_dhcp"         // Use DHCP (1=dhcp, 0=static, default 1)
#define KEY_ETH_IP "eth_ip"             // Static IP address (e.g. "192.168.1.100")
#define KEY_ETH_GW "eth_gw"             // Gateway address (e.g. "192.168.1.1")
#define KEY_ETH_MASK "eth_mask"         // Subnet mask (e.g. "255.255.255.0")
#define KEY_ETH_DNS "eth_dns"           // DNS server (e.g. "8.8.8.8")

// --- POWER LOSS RECOVERY ---
#define KEY_RECOV_EN "recov_en"         // Enable job recovery (1=on, 0=off, default 1)
#define KEY_RECOV_INTERVAL "recov_intv" // Save every N lines (default 50)

// --- AUDIBLE ALARM (Buzzer) ---
#define KEY_BUZZER_EN "buzzer_en"       // Enable buzzer (1=on, 0=off, default 1)
#define KEY_BUZZER_PIN "buzzer_pin"     // Output pin number (1-16, default 16)

// --- TOWER LIGHT ---
#define KEY_TOWER_EN "tower_en"         // Enable tower light (1=on, 0=off, default 0)
#define KEY_TOWER_GREEN "tower_grn"     // Green output pin (1-16, default 13)
#define KEY_TOWER_YELLOW "tower_yel"    // Yellow output pin (1-16, default 14)
#define KEY_TOWER_RED "tower_red"       // Red output pin (1-16, default 15)

// --- SPINDLE AUTO-PAUSE ---
#define KEY_SPINDL_PAUSE_EN "sp_pause"  // Auto-pause on overload (1=on, 0=off, default 1)
#define KEY_SPINDL_PAUSE_THR "sp_pthr"  // Pause threshold amps (default 25, less than shutdown)

// --- VFD (ALTIVAR31) CONFIGURATION ---
#define KEY_VFD_EN "vfd_en"             // Enable VFD communication (1=on, 0=off, default 1)
#define KEY_VFD_ADDR "vfd_addr"         // Modbus slave address (1-247, default 2)

// --- JXK-10 CURRENT MONITOR ---
#define KEY_JXK10_EN "jxk10_en"         // Enable JXK-10 (1=on, 0=off, default 1)
#define KEY_JXK10_ADDR "jxk10_addr"     // Modbus slave address (1-247, default 1)

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
#define KEY_ENC_FEEDBACK "enc_fb_en"  // Encoder feedback enable (1=on, 0=off)
#define KEY_ENC_INTERFACE                                                      \
  "enc_iface" // WJ66 interface type (0=RS232_HT, 1=RS485_RXD2, 255=CUSTOM)
#define KEY_ENC_BAUD "enc_baud" // WJ66 baud rate

// --- SPINDLE CURRENT SENSOR ---
#define KEY_SPINDLE_ENABLED                                                    \
  "spindl_en" // Enable/disable spindle current monitoring (0=disable, 1=enable)
#define KEY_SPINDLE_ADDRESS                                                    \
  "spindl_addr" // JXK-10 Modbus slave address (1-247, default 1)
#define KEY_SPINDLE_THRESHOLD                                                  \
  "spindl_thr" // Overcurrent threshold in integer amperes (30 = 30A)
#define KEY_SPINDLE_POLL_MS                                                    \
  "spindl_poll" // Poll interval in milliseconds (default 1000)
#define KEY_SPINDLE_RATED_RPM                                                  \
  "spindl_rpm"  // Rated Spindle RPM (default 1400)
#define KEY_BLADE_DIAMETER_MM                                                  \
  "blade_dia"   // Saw Blade Diameter in mm (default 350)

// --- VFD CURRENT CALIBRATION (PHASE 5.5) ---
#define KEY_VFD_IDLE_RMS                                                       \
  "vfd_idle_rms" // Idle baseline RMS current (amps × 100)
#define KEY_VFD_IDLE_PEAK                                                      \
  "vfd_idle_pk" // Idle baseline peak current (amps × 100)
#define KEY_VFD_STD_CUT_RMS                                                    \
  "vfd_std_rms" // Standard cut RMS current (amps × 100)
#define KEY_VFD_STD_CUT_PEAK                                                   \
  "vfd_std_pk" // Standard cut peak current (amps × 100)
#define KEY_VFD_HEAVY_RMS "vfd_heavy_rms" // Heavy load RMS current (amps × 100)
#define KEY_VFD_HEAVY_PEAK                                                     \
  "vfd_heavy_pk" // Heavy load peak current (amps × 100)
#define KEY_VFD_STALL_THR                                                      \
  "vfd_stall_thr" // Stall threshold current (amps × 100)
#define KEY_VFD_STALL_MARGIN "vfd_stall_marg" // Stall margin percent (20 = 20%)
#define KEY_VFD_CALIB_VALID                                                    \
  "vfd_calib_ok" // Calibration valid flag (1=yes, 0=no)
#define KEY_VFD_TEMP_WARN                                                      \
  "vfd_temp_warn" // Temperature warning threshold (°C, default 85)
#define KEY_VFD_TEMP_CRIT                                                      \
  "vfd_temp_crit" // Temperature critical threshold (°C, default 90)

// --- RUNTIME/MAINTENANCE TRACKING ---
#define KEY_RUNTIME_MINS "rt_mins"       // Total runtime in minutes
#define KEY_CYCLE_COUNT "cycles"         // Total job cycles completed
#define KEY_LAST_MAINT_MINS "maint_mins" // Runtime at last maintenance
#define KEY_BACKUP_TS "backup_ts"        // Last config backup timestamp (Unix epoch)
#define KEY_DIST_X_M "dist_x_m"          // Total X axis distance traveled (meters)
#define KEY_DIST_Y_M "dist_y_m"          // Total Y axis distance traveled (meters)
#define KEY_DIST_Z_M "dist_z_m"          // Total Z axis distance traveled (meters)

// --- PREDEFINED POSITIONS (G30) ---
#define KEY_POS_SAFE_X "pos_safe_x" // Safe position X coordinate in mm
#define KEY_POS_SAFE_Y "pos_safe_y" // Safe position Y coordinate in mm
#define KEY_POS_SAFE_Z "pos_safe_z" // Safe position Z coordinate in mm
#define KEY_POS_SAFE_A "pos_safe_a" // Safe position A coordinate in degrees
#define KEY_POS_1_X "pos_1_x"       // Predefined position 1 X
#define KEY_POS_1_Y "pos_1_y"       // Predefined position 1 Y
#define KEY_POS_1_Z "pos_1_z"       // Predefined position 1 Z
#define KEY_POS_1_A "pos_1_a"       // Predefined position 1 A

// --- HOMING ---
#define KEY_HOME_ENABLE "home_enable" // Enable homing (1=yes, 0=no)
#define KEY_HOME_PROFILE_FAST "home_prof_fast"
#define KEY_HOME_PROFILE_SLOW "home_prof_slow"

// --- SPEED PROFILES ---
#define KEY_SPEED_CAL_X "spd_x"
#define KEY_SPEED_CAL_Y "spd_y"
#define KEY_SPEED_CAL_Z "spd_z"
#define KEY_SPEED_CAL_A "spd_a"

// --- MOTION BEHAVIOR ---
#define KEY_DEFAULT_SPEED "def_spd"
#define KEY_DEFAULT_ACCEL "def_acc"
#define KEY_X_APPROACH "x_appr"
#define KEY_MOTION_APPROACH_MODE "mot_app_mode"
#define KEY_MOTION_DEADBAND "mot_deadband"
#define KEY_MOTION_BUFFER_ENABLE "mot_buf_en"
#define KEY_MOTION_STRICT_LIMITS "mot_strict"
#define KEY_STOP_TIMEOUT "stop_timeout"

// --- SAFETY ---
#define KEY_ALARM_PIN "alarm_pin"
#define KEY_STALL_TIMEOUT "stall_ms"
#define KEY_BUTTONS_ENABLED                                                    \
  "btn_en" // Enable physical button polling (1=yes, 0=no, default=1)

// --- WCS OFFSETS (Generated Keys) ---
// Base keys, suffixes will be appended (e.g. "g54_x")
#define KEY_WCS_PREFIX "g"

// --- WEB SERVER CREDENTIALS ---
#define KEY_WEB_USERNAME "web_user" // Web server username
#define KEY_WEB_PASSWORD "web_pass" // Web server password
#define KEY_WEB_PORT "web_port"     // Web server port (default 80)
#define KEY_WEB_PW_CHANGED                                                     \
  "web_pw_chg" // Flag: password changed from default (1=changed, 0=default)

// --- WIFI CREDENTIALS ---
#define KEY_WIFI_SSID "wifi_ssid"     // WiFi network name (SSID)
#define KEY_WIFI_PASSWORD "wifi_pass" // WiFi network password
#define KEY_WIFI_AP_EN "wifi_ap_en"   // Enable/disable AP mode (0=disable, 1=enable)
#define KEY_WIFI_AP_SSID "wifi_ap_ssid" // AP mode SSID (default: "BISSO-E350-Setup")
#define KEY_WIFI_AP_PASS "wifi_ap_pass" // AP mode password (default: "password")

// --- OTA (OVER-THE-AIR UPDATE) SECURITY ---
#define KEY_OTA_PASSWORD                                                       \
  "ota_pass" // OTA update password (default: "bisso-ota")
#define KEY_OTA_PW_CHANGED                                                     \
  "ota_pw_chg" // Flag: OTA password changed from default (1=changed, 0=default)


// --- LCD DISPLAY (PHASE 4.0) ---
#define KEY_LCD_EN "lcd_en"             // Enable LCD display (1=on, 0=off, default 1)

#endif
