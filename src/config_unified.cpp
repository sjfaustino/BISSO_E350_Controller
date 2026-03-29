/**
 * @file config_unified.cpp
 * @brief Unified Configuration Manager (NVS) v3.5.20
 * @details Implements Input Validation, Hardened String Pool, and Dump Utility.
 * @author Sergio Faustino
 */

#include "config_unified.h"
#include "cli.h" // Support table-driven dump
#include "config_keys.h"
#include "config_cache.h"
#include "serial_logger.h"
#include "system_tuning.h"
#include "system_constants.h"
#include "system_events.h" // Event-driven architecture
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>
#include <nvs_flash.h>
#include "system_utils.h" // Safe reboot helper
#include "string_safety.h"
#include <esp_mac.h> // esp_read_mac() for device-unique defaults


// NVS Persistence Object
static Preferences prefs;

// Internal Cache Table
static config_entry_t config_table[CONFIG_MAX_KEYS];
static int config_count = 0;

// Thread Safety: Mutex for config_table access
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static SemaphoreHandle_t config_cache_mutex = NULL;
#define CONFIG_MUTEX_TIMEOUT_MS 100

// RAII Mutex Guard — automatically releases mutex on scope exit (including early returns)
// Eliminates all manual xSemaphoreTakeRecursive/xSemaphoreGiveRecursive pairs.
class ConfigMutexGuard {
public:
 explicit ConfigMutexGuard(SemaphoreHandle_t mtx, uint32_t timeout_ms = CONFIG_MUTEX_TIMEOUT_MS)
 : m_mutex(mtx), m_acquired(false) {
 if (m_mutex != NULL) {
 m_acquired = (xSemaphoreTakeRecursive(m_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
 }
 }
 ~ConfigMutexGuard() {
 if (m_acquired && m_mutex != NULL) {
 xSemaphoreGiveRecursive(m_mutex);
 }
 }
 bool acquired() const { return m_acquired || m_mutex == NULL; } // NULL = early boot, no mutex needed
 // Non-copyable
 ConfigMutexGuard(const ConfigMutexGuard&) = delete;
 ConfigMutexGuard& operator=(const ConfigMutexGuard&) = delete;
private:
 SemaphoreHandle_t m_mutex;
 bool m_acquired;
};

// State Flags
static bool initialized = false;
static bool config_dirty = false;
static uint32_t last_nvs_save = 0;

// Auto-Save Configuration
#define NVS_CONFIG_SAVE_INTERVAL_MS 5000
#define NVS_SAVE_ON_CRITICAL true

// Critical keys trigger immediate write to flash to prevent data loss
static const char *critical_keys[] = {
 KEY_PPM_X,
 KEY_PPM_Y,
 KEY_PPM_Z,
 KEY_PPM_A,
 KEY_X_LIMIT_MIN,
 KEY_X_LIMIT_MAX,
 KEY_Y_LIMIT_MIN,
 KEY_Y_LIMIT_MAX,
 KEY_Z_LIMIT_MIN,
 KEY_Z_LIMIT_MAX,
 KEY_A_LIMIT_MIN,
 KEY_A_LIMIT_MAX,
 KEY_ALARM_PIN,
 KEY_STALL_TIMEOUT,
 KEY_MOTION_STRICT_LIMITS, // Safety Critical
 KEY_HOME_PROFILE_FAST, // Homing
 KEY_HOME_PROFILE_SLOW,
 KEY_DEFAULT_ACCEL, // Float
 KEY_DEFAULT_SPEED, // Float
 KEY_WEB_USERNAME, // Security Critical
 KEY_WEB_PASSWORD, // Security Critical
 KEY_WEB_PW_CHANGED, // Security Critical
 KEY_WIFI_AP_EN, // WiFi AP Mode
 KEY_WIFI_AP_SSID,
 KEY_WIFI_AP_PASS,
 KEY_STATUS_LIGHT_EN, // Status Indication
 KEY_STATUS_LIGHT_GREEN,
 KEY_STATUS_LIGHT_YELLOW,
 KEY_STATUS_LIGHT_RED,
 KEY_BUZZER_EN, // Audible Alarm
 KEY_BUZZER_PIN,
 KEY_LCD_EN, // Display Enable
 KEY_YHTC05_ENABLED // Tachometer Enable
};

static bool isCriticalKey(const char *key) {
 for (uint8_t i = 0; i < sizeof(critical_keys) / sizeof(critical_keys[0]);
 i++) {
 if (strcmp(key, critical_keys[i]) == 0)
 return true;
 }
 return false;
}

// ----------------------------------------------------------------------------
// STRING BUFFER POOL
// ----------------------------------------------------------------------------
// Uses a rotating pool of 16 buffers so that up to 16 pointers returned by
// configGetString() can coexist safely. After 16 calls the oldest buffer is
// reused. Prefer configGetStringSafe() for any pointer that must outlive the
// current expression (e.g. stored in a struct, passed to a task, etc.).
// Pool enlarged from 8→16 to halve the chance of premature reuse in complex
// logging sequences (e.g. a single log line that formats 4+ string keys).
#define CONFIG_STRING_BUFFER_COUNT 16
#define CONFIG_STRING_BUFFER_SIZE 256

static struct {
 char buffers[CONFIG_STRING_BUFFER_COUNT][CONFIG_STRING_BUFFER_SIZE];
 uint8_t current_buffer = 0;
} string_return_pool = {};

/**
 * @brief Gets next buffer from rotating pool.
 * @note Prefer configGetStringSafe() for long-lived string pointers.
 * @return Pointer to buffer — valid until 16 more configGetString() calls.
 */
static char *configGetStringBuffer() {
 char *buffer = string_return_pool.buffers[string_return_pool.current_buffer];
 string_return_pool.current_buffer =
 (string_return_pool.current_buffer + 1) % CONFIG_STRING_BUFFER_COUNT;
 return buffer;
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

// Thread-safe config entry lookup with mutex protection
static int findConfigEntry(const char *key) {
 if (!key)
 return -1;

 ConfigMutexGuard lock(config_cache_mutex);
 if (!lock.acquired()) {
 logError("[CONFIG] Mutex timeout in findConfigEntry(%s)", key);
 return -1;
 }

 for (int i = 0; i < config_count; i++) {
 if (strcmp(config_table[i].key, key) == 0)
 return i;
 }
 return -1;
}

static void addToCacheInt(const char *key, int32_t val) {
 if (config_count >= CONFIG_MAX_KEYS)
 return;
 ConfigMutexGuard lock(config_cache_mutex);
 int idx = config_count++;
 SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
 config_table[idx].type = CONFIG_INT32;
 config_table[idx].value.int_val = val;
 config_table[idx].is_set = true;
}

static void addToCacheFloat(const char *key, float val) {
 if (config_count >= CONFIG_MAX_KEYS)
 return;
 ConfigMutexGuard lock(config_cache_mutex);
 int idx = config_count++;
 SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
 config_table[idx].type = CONFIG_FLOAT;
 config_table[idx].value.float_val = val;
 config_table[idx].is_set = true;
}

// ----------------------------------------------------------------------------
// VALIDATION LOGIC (Safety Fix)
// ----------------------------------------------------------------------------

static result_t validateInt(const char *key, int32_t *value) {
 int32_t val = *value;
 result_t res = RESULT_OK;

 // PPM — Pulses Per MM (50–500)
 if (strcmp(key, KEY_PPM_X) == 0 || strcmp(key, KEY_PPM_Y) == 0 ||
 strcmp(key, KEY_PPM_Z) == 0 || strcmp(key, KEY_PPM_A) == 0) {
 if (val < 50) { *value = 50; res = RESULT_INVALID_PARAM; }
 else if (val > 500) { *value = 500; res = RESULT_INVALID_PARAM; }

 // Soft limits (-1,000,000 to 1,000,000)
 } else if (strcmp(key, KEY_X_LIMIT_MIN) == 0 || strcmp(key, KEY_X_LIMIT_MAX) == 0 ||
 strcmp(key, KEY_Y_LIMIT_MIN) == 0 || strcmp(key, KEY_Y_LIMIT_MAX) == 0 ||
 strcmp(key, KEY_Z_LIMIT_MIN) == 0 || strcmp(key, KEY_Z_LIMIT_MAX) == 0 ||
 strcmp(key, KEY_A_LIMIT_MIN) == 0 || strcmp(key, KEY_A_LIMIT_MAX) == 0) {
 if (val < -1000000) { *value = -1000000; res = RESULT_INVALID_PARAM; }
 else if (val > 1000000) { *value = 1000000; res = RESULT_INVALID_PARAM; }

 // Motion timeouts (100ms – 60s)
 } else if (strcmp(key, KEY_STALL_TIMEOUT) == 0 || strcmp(key, KEY_STOP_TIMEOUT) == 0) {
 if (val < 100) { *value = 100; res = RESULT_INVALID_PARAM; }
 else if (val > 60000) { *value = 60000; res = RESULT_INVALID_PARAM; }

 // Encoder deviation alarm timeout (100ms – 10s)
 } else if (strcmp(key, KEY_ENC_DEV_TIMEOUT) == 0) {
 if (val < 100) { *value = 100; res = RESULT_INVALID_PARAM; }
 else if (val > 10000) { *value = 10000; res = RESULT_INVALID_PARAM; }

 // Homing profiles (0–2)
 } else if (strcmp(key, KEY_HOME_PROFILE_FAST) == 0 || strcmp(key, KEY_HOME_PROFILE_SLOW) == 0) {
 if (val < 0) { *value = 0; res = RESULT_INVALID_PARAM; }
 else if (val > 2) { *value = 2; res = RESULT_INVALID_PARAM; }

 // Modbus addresses (1–247)
 } else if (strcmp(key, KEY_VFD_ADDR) == 0 || strcmp(key, KEY_VFD2_ADDR) == 0 ||
 strcmp(key, KEY_JXK10_ADDR) == 0 || strcmp(key, KEY_YHTC05_ADDR) == 0 ||
 strcmp(key, KEY_ENC_ADDR) == 0) {
 if (val < 1) { *value = 1; res = RESULT_INVALID_PARAM; }
 else if (val > 247) { *value = 247; res = RESULT_INVALID_PARAM; }

 // GPIO pins (1–39 on ESP32-S3, broad range check)
 } else if (strcmp(key, KEY_ALARM_PIN) == 0 || strcmp(key, KEY_BUZZER_PIN) == 0 ||
 strcmp(key, KEY_STATUS_LIGHT_GREEN) == 0 || strcmp(key, KEY_STATUS_LIGHT_YELLOW) == 0 ||
 strcmp(key, KEY_STATUS_LIGHT_RED) == 0) {
 if (val < 1) { *value = 1; res = RESULT_INVALID_PARAM; }
 else if (val > 39) { *value = 39; res = RESULT_INVALID_PARAM; }
 }

 return res;
}

static result_t validateFloat(const char *key, float *value) {
 float val = *value;
 result_t res = RESULT_OK;

 // Acceleration and speed must be positive
 if (strcmp(key, KEY_DEFAULT_ACCEL) == 0 || strcmp(key, KEY_DEFAULT_SPEED) == 0) {
 if (val < 0.1f) { *value = 0.1f; res = RESULT_INVALID_PARAM; }

 // Target position margin (0.001mm – 10mm)
 } else if (strcmp(key, KEY_TARGET_MARGIN) == 0) {
 if (val < 0.001f) { *value = 0.001f; res = RESULT_INVALID_PARAM; }
 else if (val > 10.0f) { *value = 10.0f; res = RESULT_INVALID_PARAM; }
 }

 return res;
}

static result_t validateString(const char *key, const char *value, size_t len) {
 const size_t MIN_PASSWORD_LENGTH = 8;

 // Web credentials: enforce minimum lengths
 if (strcmp(key, KEY_WEB_PASSWORD) == 0 || strcmp(key, KEY_WIFI_AP_PASS) == 0) {
 if (value[0] == '\0') {
 logError("[CONFIG] %s cannot be empty", key);
 return RESULT_INVALID_PARAM;
 }
 if (strlen(value) < MIN_PASSWORD_LENGTH) {
 logError("[CONFIG] %s too short (min %d chars, got %d)", key, MIN_PASSWORD_LENGTH, strlen(value));
 return RESULT_INVALID_PARAM;
 }
 }

 if (strcmp(key, KEY_WEB_USERNAME) == 0) {
 if (value[0] == '\0' || strlen(value) < 4) {
 logError("[CONFIG] Username too short (min 4 chars)");
 return RESULT_INVALID_PARAM;
 }
 }

 (void)len; // Used by callers for buffer sizing
 return RESULT_OK;
}

// ============================================================================
// INITIALIZATION & DEFAULTS
// ============================================================================

void configSetDefaults() {
 if (!initialized)
 return;

 logInfo("Applying Factory Defaults...");

 // SAFETY: Default to Strict Limits (1 = E-Stop on any drift)
 if (!prefs.isKey(KEY_MOTION_STRICT_LIMITS))
 prefs.putInt(KEY_MOTION_STRICT_LIMITS, 1);

 // HOMING
 if (!prefs.isKey(KEY_HOME_PROFILE_FAST))
 prefs.putInt(KEY_HOME_PROFILE_FAST, 2);
 if (!prefs.isKey(KEY_HOME_PROFILE_SLOW))
 prefs.putInt(KEY_HOME_PROFILE_SLOW, 0);

 // MOTION
 if (!prefs.isKey(KEY_MOTION_BUFFER_ENABLE))
 prefs.putInt(KEY_MOTION_BUFFER_ENABLE, 1);

 // AXIS
 if (!prefs.isKey(KEY_X_APPROACH))
 prefs.putInt(KEY_X_APPROACH, 5); // Final approach (SLOW) at 5mm
 if (!prefs.isKey(KEY_X_APPROACH_MED))
 prefs.putInt(KEY_X_APPROACH_MED, 20); // Medium approach at 20mm
 if (!prefs.isKey(KEY_TARGET_MARGIN))
 prefs.putFloat(KEY_TARGET_MARGIN, 0.1f); // Target position margin 0.1mm
 if (!prefs.isKey(KEY_DEFAULT_ACCEL))
 prefs.putFloat(KEY_DEFAULT_ACCEL, 100.0f);

 // HARDWARE
 if (!prefs.isKey(KEY_ALARM_PIN))
 prefs.putInt(KEY_ALARM_PIN, 2);
 if (!prefs.isKey(KEY_STALL_TIMEOUT))
 prefs.putInt(KEY_STALL_TIMEOUT, 2000);
 if (!prefs.isKey(KEY_SPINDL_TOOLBREAK_THR))
 prefs.putFloat(KEY_SPINDL_TOOLBREAK_THR, 5.0f);
 if (!prefs.isKey(KEY_SPINDL_PAUSE_THR))
 prefs.putInt(KEY_SPINDL_PAUSE_THR, 25);
 // Consolidated Spindle/JXK10 Defaults
 if (!prefs.isKey(KEY_JXK10_ADDR))
 prefs.putInt(KEY_JXK10_ADDR, 1);
 if (!prefs.isKey(KEY_JXK10_ENABLED))
 prefs.putInt(KEY_JXK10_ENABLED, 1);
 if (!prefs.isKey(KEY_ENC_DEV_TIMEOUT))
 prefs.putInt(KEY_ENC_DEV_TIMEOUT, ENCODER_DEVIATION_TIMEOUT_DEFAULT_MS);
 if (!prefs.isKey(KEY_SPINDLE_THRESHOLD))
 prefs.putInt(KEY_SPINDLE_THRESHOLD, 30);

 // WEB SERVER CREDENTIALS — unique per device using MAC address
 if (!prefs.isKey(KEY_WEB_USERNAME))
 prefs.putString(KEY_WEB_USERNAME, "admin");
 if (!prefs.isKey(KEY_WEB_PASSWORD)) {
 // Generate device-unique password from MAC address last 4 hex chars
 uint8_t mac[6];
 esp_read_mac(mac, ESP_MAC_WIFI_STA);
 char default_pw[16];
 snprintf(default_pw, sizeof(default_pw), "BISSO-%02X%02X", mac[4], mac[5]);
 prefs.putString(KEY_WEB_PASSWORD, default_pw);
 logWarning("[CONFIG] Generated unique default password: %s", default_pw);
 logWarning("[CONFIG] *** CHANGE THIS PASSWORD via web UI or 'web_setpass' ***");
 }
 if (!prefs.isKey(KEY_WEB_PW_CHANGED))
 prefs.putInt(KEY_WEB_PW_CHANGED, 0); // 0 = default, needs change

 // WIFI AP MODE
 if (!prefs.isKey(KEY_WIFI_AP_EN))
 prefs.putInt(KEY_WIFI_AP_EN, 1); // ENABLED BY DEFAULT
 if (!prefs.isKey(KEY_WIFI_AP_SSID))
 prefs.putString(KEY_WIFI_AP_SSID, "BISSO-E350-Setup");
 if (!prefs.isKey(KEY_WIFI_AP_PASS)) {
 // AP password also unique per device
 uint8_t mac[6];
 esp_read_mac(mac, ESP_MAC_WIFI_STA);
 char ap_pw[16];
 snprintf(ap_pw, sizeof(ap_pw), "BISSO-%02X%02X", mac[4], mac[5]);
 prefs.putString(KEY_WIFI_AP_PASS, ap_pw);
 }
 if (!prefs.isKey(KEY_LCD_EN))
 prefs.putInt(KEY_LCD_EN, 1);
}

void configUnifiedLoad() {
 logInfo("Pre-loading Cache...");

 for (uint8_t i = 0; i < sizeof(critical_keys) / sizeof(critical_keys[0]);
 i++) {
 const char *key = critical_keys[i];

 if (prefs.isKey(key)) {
 // Heuristic for types based on key name content
 if (strstr(key, "accel") || strstr(key, "speed")) {
 float val = prefs.getFloat(key, 0.0f);
 addToCacheFloat(key, val);
 } else {
 int32_t val = prefs.getInt(key, 0);
 addToCacheInt(key, val);
 }
 }
 }
}

result_t configUnifiedInit() {
 logModuleInit("CONFIG");

 // Create mutex for thread-safe cache access
 if (config_cache_mutex == NULL) {
 config_cache_mutex = xSemaphoreCreateRecursiveMutex();
 if (config_cache_mutex == NULL) {
 logError("[CRITICAL] Failed to create cache mutex!");
 }
 }

 memset(config_table, 0, sizeof(config_table));
 config_count = 0;

 if (!prefs.begin("PosiPro_cfg", false)) {
 logError("NVS Mount Failed!");
 return RESULT_ERROR_STORAGE;
 }

 initialized = true;
 configSetDefaults();
 configUnifiedLoad();
 logInfo("Ready. Loaded %d entries.", config_count);
 
 // Log NVS space usage on boot
 configLogNvsStats();

 // Initialize Typed Cache
 configCacheInit();

 return RESULT_OK;
}

/**
 * @brief Cleanup configuration system resources
 *
 * PHASE 5.10: Resource Leak Fix - Properly cleanup mutex and NVS
 * Should be called before system shutdown/reboot
 */
void configUnifiedCleanup() {
 logInfo("Cleaning up resources...");

 // Close NVS preferences
 if (initialized) {
 prefs.end();
 initialized = false;
 }

 // Delete mutex
 if (config_cache_mutex != NULL) {
 vSemaphoreDelete(config_cache_mutex);
 config_cache_mutex = NULL;
 logInfo("Mutex deleted");
 }

 // Clear cache
 config_count = 0;
 logInfo("Cleanup complete");
}

// ============================================================================
// GETTERS
// ============================================================================

int32_t configGetInt(const char *key, int32_t default_val) {
 if (!initialized)
 return default_val;
 int idx = findConfigEntry(key);
 if (idx >= 0 && config_table[idx].type == CONFIG_INT32 &&
 config_table[idx].is_set) {
 return config_table[idx].value.int_val;
 }
 // CRITICAL FIX: Check if key exists before calling getInt()
 // Prevents ESP32 Preferences library from logging ERROR messages for missing
 // keys
 if (prefs.isKey(key)) {
 return prefs.getInt(key, default_val);
 }
 return default_val;
}

float configGetFloat(const char *key, float default_val) {
 if (!initialized)
 return default_val;
 int idx = findConfigEntry(key);
 if (idx >= 0 && config_table[idx].type == CONFIG_FLOAT &&
 config_table[idx].is_set) {
 return config_table[idx].value.float_val;
 }
 // CRITICAL FIX: Check if key exists before calling getFloat()
 // This prevents ESP32 Preferences library from logging ERROR messages
 // when loading WCS offsets (g54_x, g54_y, etc.) that don't exist yet
 if (prefs.isKey(key)) {
 return prefs.getFloat(key, default_val);
 }
 return default_val;
}

const char *configGetString(const char *key, const char *default_val) {
 if (!initialized)
 return default_val ? default_val : "";
 int idx = findConfigEntry(key);
 if (idx >= 0 && config_table[idx].type == CONFIG_STRING &&
 config_table[idx].is_set) {
 return config_table[idx].value.str_val;
 }

 if (prefs.isKey(key)) {
 char *buffer = configGetStringBuffer();
 if (prefs.getString(key, buffer, CONFIG_STRING_BUFFER_SIZE) > 0) {
 return buffer;
 }
 }
 return default_val ? default_val : "";
}

uint64_t configGetUInt64(const char *key, uint64_t default_val) {
 if (!initialized)
 return default_val;
 int idx = findConfigEntry(key);
 if (idx >= 0 && config_table[idx].type == CONFIG_UINT64 &&
 config_table[idx].is_set) {
 return config_table[idx].value.uint64_val;
 }
 if (prefs.isKey(key)) {
 return prefs.getULong64(key, default_val);
 }
 return default_val;
}

// ============================================================================
// SETTERS (With Validation)
// ============================================================================

result_t configSetInt(const char *key, int32_t value) {
 if (!initialized)
 return RESULT_NOT_READY;

 result_t val_res = validateInt(key, &value);
 if (val_res != RESULT_OK) {
 logWarning("Validation clamped %s to %ld", key, (long)value);
 }

 {
 ConfigMutexGuard lock(config_cache_mutex);
 if (!lock.acquired()) return RESULT_TIMEOUT;

 int idx = findConfigEntry(key);
 if (idx >= 0 && config_table[idx].type != CONFIG_INT32)
 return RESULT_INVALID_PARAM;
 if (idx < 0) {
 if (config_count >= CONFIG_MAX_KEYS) return RESULT_ERROR_MEMORY;
 idx = config_count++;
 SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
 config_table[idx].type = CONFIG_INT32;
 }

 if (config_table[idx].is_set && config_table[idx].value.int_val == value)
 return RESULT_OK;

 config_table[idx].value.int_val = value;
 config_table[idx].is_set = true;
 config_dirty = true;
 last_nvs_save = millis();

 if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
 prefs.putInt(key, value);
 config_dirty = false;
 }
 } // lock released

 systemEventsSystemSet(EVENT_SYSTEM_CONFIG_CHANGED);
 configCacheUpdate(key);
 return RESULT_OK;
}

result_t configSetFloat(const char *key, float value) {
 if (!initialized)
 return RESULT_NOT_READY;

 result_t val_res = validateFloat(key, &value);
 if (val_res != RESULT_OK) {
 logWarning("Validation clamped %s to %.3f", key, value);
 }

 {
 ConfigMutexGuard lock(config_cache_mutex);
 if (!lock.acquired()) return RESULT_TIMEOUT;

 int idx = findConfigEntry(key);
 if (idx >= 0 && config_table[idx].type != CONFIG_FLOAT)
 return RESULT_INVALID_PARAM;
 if (idx < 0) {
 if (config_count >= CONFIG_MAX_KEYS) return RESULT_ERROR_MEMORY;
 idx = config_count++;
 SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
 config_table[idx].type = CONFIG_FLOAT;
 }

 if (config_table[idx].is_set &&
 fabsf(config_table[idx].value.float_val - value) < 0.0001f)
 return RESULT_OK;

 config_table[idx].value.float_val = value;
 config_table[idx].is_set = true;
 config_dirty = true;
 last_nvs_save = millis();

 if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
 prefs.putFloat(key, value);
 config_dirty = false;
 }
 } // lock released

 systemEventsSystemSet(EVENT_SYSTEM_CONFIG_CHANGED);
 configCacheUpdate(key);
 return RESULT_OK;
}

result_t configSetString(const char *key, const char *value) {
 if (!initialized)
 return RESULT_NOT_READY;
 if (!value)
 return RESULT_INVALID_PARAM;

 // Reject invalid values (e.g., short passwords) before taking mutex
 result_t val_res = validateString(key, value, CONFIG_VALUE_LEN);
 if (val_res != RESULT_OK)
 return val_res;

 char validated_value[CONFIG_VALUE_LEN];
 SAFE_STRCPY(validated_value, value, CONFIG_VALUE_LEN);

 {
 ConfigMutexGuard lock(config_cache_mutex);
 if (!lock.acquired()) return RESULT_TIMEOUT;

 int idx = findConfigEntry(key);
 if (idx >= 0 && config_table[idx].type != CONFIG_STRING)
 return RESULT_INVALID_PARAM;
 if (idx < 0) {
 if (config_count >= CONFIG_MAX_KEYS) return RESULT_ERROR_MEMORY;
 idx = config_count++;
 SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
 config_table[idx].type = CONFIG_STRING;
 }

 if (config_table[idx].is_set &&
 strncmp(config_table[idx].value.str_val, validated_value, CONFIG_VALUE_LEN) == 0)
 return RESULT_OK;

 SAFE_STRCPY(config_table[idx].value.str_val, validated_value, CONFIG_VALUE_LEN);
 config_table[idx].is_set = true;
 config_dirty = true;
 last_nvs_save = millis();

 if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
 prefs.putString(key, validated_value);
 config_dirty = false;
 }
 } // lock released

 systemEventsSystemSet(EVENT_SYSTEM_CONFIG_CHANGED);
 configCacheUpdate(key);
 return RESULT_OK;
}


result_t configSetUInt64(const char *key, uint64_t value) {
 if (!initialized)
 return RESULT_NOT_READY;

 {
 ConfigMutexGuard lock(config_cache_mutex);
 if (!lock.acquired()) return RESULT_TIMEOUT;

 int idx = findConfigEntry(key);
 if (idx >= 0 && config_table[idx].type != CONFIG_UINT64)
 return RESULT_INVALID_PARAM;
 if (idx < 0) {
 if (config_count >= CONFIG_MAX_KEYS) return RESULT_ERROR_MEMORY;
 idx = config_count++;
 SAFE_STRCPY(config_table[idx].key, key, CONFIG_KEY_LEN);
 config_table[idx].type = CONFIG_UINT64;
 }

 if (config_table[idx].is_set && config_table[idx].value.uint64_val == value)
 return RESULT_OK;

 config_table[idx].value.uint64_val = value;
 config_table[idx].is_set = true;
 config_dirty = true;
 last_nvs_save = millis();

 if (isCriticalKey(key) && NVS_SAVE_ON_CRITICAL) {
 prefs.putULong64(key, value);
 config_dirty = false;
 }
 } // lock released

 systemEventsSystemSet(EVENT_SYSTEM_CONFIG_CHANGED);
 configCacheUpdate(key);
 return RESULT_OK;
}

// ============================================================================
// UTILITIES
// ============================================================================

void configUnifiedFlush() {
 if (!config_dirty)
 return;
 // Wraparound-safe timeout comparison
 if ((uint32_t)(millis() - last_nvs_save) > NVS_CONFIG_SAVE_INTERVAL_MS) {
 configUnifiedSave();
 config_dirty = false;
 }
}

result_t configUnifiedSave() {
 if (!initialized || !config_dirty)
 return RESULT_OK;

 logInfo("Saving to NVS...");

 // Protect config_table read during save
 if (config_cache_mutex != NULL) {
 if (xSemaphoreTakeRecursive(config_cache_mutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
 logWarning("Mutex timeout in configUnifiedSave");
 return RESULT_TIMEOUT;
 }
 }

 bool success = true;
 for (int i = 0; i < config_count; i++) {
 if (!config_table[i].is_set)
 continue;
 // Skip if critical key (already write-through)
 if (config_table[i].type == CONFIG_INT32 &&
 isCriticalKey(config_table[i].key))
 continue;

 size_t result = 0;
 switch (config_table[i].type) {
 case CONFIG_INT32:
 result = prefs.putInt(config_table[i].key, config_table[i].value.int_val);
 break;
 case CONFIG_FLOAT:
 result = prefs.putFloat(config_table[i].key, config_table[i].value.float_val);
 break;
 case CONFIG_STRING:
 result = prefs.putString(config_table[i].key, config_table[i].value.str_val);
 break;
 case CONFIG_UINT64:
 result = prefs.putULong64(config_table[i].key, config_table[i].value.uint64_val);
 break;
 }
 if (result == 0) success = false;
 
 // Prevent task starvation during long save operations
 if (i % 10 == 0) taskYIELD();
 }
 config_dirty = false;
 
 if (config_cache_mutex != NULL) {
 xSemaphoreGiveRecursive(config_cache_mutex);
 }
 
 if (success) {
 logInfo("Save Complete.");
 return RESULT_OK;
 } else {
 logError("Some keys failed to save");
 return RESULT_ERROR_STORAGE;
 }
}

result_t configUnifiedReset() {
 logWarning("Resetting to Factory Defaults...");

 // Clear in-memory cache before reset to prevent stale values
 configUnifiedClear();

 if (!prefs.clear()) {
 logError("Failed to clear NVS storage");
 return RESULT_ERROR_STORAGE;
 }
 
 configSetDefaults();

 // Safe defaults
 configSetInt(KEY_X_LIMIT_MIN, -500000);
 configSetInt(KEY_X_LIMIT_MAX, 500000);

 logInfo("Reset Complete. Reboot recommended.");
 return RESULT_OK;
}

void configUnifiedClear() {
 memset(config_table, 0, sizeof(config_table));
 config_count = 0;
}

int configGetKeyCount() { return config_count; }

void configUnifiedDiagnostics() {
 serialLoggerLock();
 logPrintln("\n=== CONFIG DIAGNOSTICS ===");
 logPrintf("Strict Limits: %s\r\n", configGetInt(KEY_MOTION_STRICT_LIMITS, 1)
 ? "ON (Safe)"
 : "OFF (Recovery)");
 logPrintf("Home Fast Profile: %ld\r\n",
 (long)configGetInt(KEY_HOME_PROFILE_FAST, 2));
 logPrintf("Buffer Enable: %s\r\n",
 configGetInt(KEY_MOTION_BUFFER_ENABLE, 1) ? "YES" : "NO");
 logPrintf("Total Keys: %d\r\n", config_count);
 logPrintln("==========================\n");
 serialLoggerUnlock();
}

// Validated getters with bounds checking
int32_t configGetIntValidated(const char *key, int32_t default_val,
 int32_t min_val, int32_t max_val) {
 if (!key)
 return default_val;

 int32_t value = configGetInt(key, default_val);

 // Apply validation bounds
 if (min_val >= 0 && value < min_val) {
 logWarning(
 "Value %ld below minimum %ld for key '%s', using minimum",
 (long)value, (long)min_val, key);
 return min_val;
 }

 if (max_val >= 0 && value > max_val) {
 logWarning(
 "Value %ld exceeds maximum %ld for key '%s', using maximum",
 (long)value, (long)max_val, key);
 return max_val;
 }

 return value;
}

float configGetFloatValidated(const char *key, float default_val, float min_val,
 float max_val) {
 if (!key)
 return default_val;

 float value = configGetFloat(key, default_val);

 // Apply validation bounds
 if (min_val >= 0.0f && value < min_val) {
 logWarning(
 "Value %.2f below minimum %.2f for key '%s', using minimum",
 (double)value, (double)min_val, key);
 return min_val;
 }

 if (max_val >= 0.0f && value > max_val) {
 logWarning(
 "Value %.2f exceeds maximum %.2f for key '%s', using maximum",
 (double)value, (double)max_val, key);
 return max_val;
 }

 return value;
}

// Added for CLI 'config dump' command
void configUnifiedPrintAll() {
 // Single-Buffer Strategy (same as cliPrintHelp)
 // ESP32-S3 USB CDC drops data when Serial.print() is called many times.
 // Serial.flush() does NOT reliably block until the host reads.
 // Solution: Build entire output into one PSRAM buffer, print once.

 const size_t BUF_SIZE = 4096;
 char* buf = (char*)malloc(BUF_SIZE);
 if (!buf) {
 if (serialLoggerLock()) {
 Serial.print("(config dump: alloc failed)\r\n");
 Serial.flush();
 serialLoggerUnlock();
 }
 return;
 }

 int pos = 0;
 char val_str[64];

 // Header
 pos += snprintf(buf + pos, BUF_SIZE - pos,
 "\r\n=== FULL CONFIGURATION DUMP ===\r\n"
 "+--------------------------------+----------------------+--+\r\n"
 "| KEY | VALUE | |\r\n"
 "+--------------------------------+----------------------+--+\r\n");

 // Snapshot all set entries under cache lock
 for (int i = 0; i < config_count && pos < (int)(BUF_SIZE - 80); i++) {
 bool valid = false;
 config_entry_t entry;
 if (config_cache_mutex != NULL) {
 if (xSemaphoreTakeRecursive(config_cache_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
 if (config_table[i].is_set) {
 entry = config_table[i];
 valid = true;
 }
 xSemaphoreGiveRecursive(config_cache_mutex);
 }
 } else {
 if (config_table[i].is_set) {
 entry = config_table[i];
 valid = true;
 }
 }

 if (valid) {
 switch (entry.type) {
 case CONFIG_INT32: snprintf(val_str, sizeof(val_str), "%ld", (long)entry.value.int_val); break;
 case CONFIG_FLOAT: snprintf(val_str, sizeof(val_str), "%.3f", entry.value.float_val); break;
 case CONFIG_STRING: snprintf(val_str, sizeof(val_str), "%s", entry.value.str_val); break;
 default: strncpy(val_str, "?", sizeof(val_str)); break;
 }
 pos += snprintf(buf + pos, BUF_SIZE - pos, "| %-30s | %-20s | |\r\n", entry.key, val_str);
 }
 }

 // Footer
 pos += snprintf(buf + pos, BUF_SIZE - pos, "+--------------------------------+----------------------+--+\r\n");

 // Single atomic print — acquire mutex, output, flush, release
 if (serialLoggerLock()) {
 Serial.print(buf);
 Serial.flush();
 serialLoggerUnlock();
 }

 free(buf);
}

void* configGetMutex() {
 return (void*)config_cache_mutex;
}

// ============================================================================
// NVS SPACE MANAGEMENT
// ============================================================================

void configLogNvsStats() {
 nvs_stats_t nvs_stats;
 esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
 if (err == ESP_OK) {
 uint32_t used_pct = (nvs_stats.used_entries * 100) / nvs_stats.total_entries;
 logInfo("Entries: %d/%d used (%d%%), Free: %d", 
 nvs_stats.used_entries, nvs_stats.total_entries, 
 used_pct, nvs_stats.free_entries);
 
 if (used_pct > 80) {
 logWarning("WARNING: Storage >80%% full! Consider erasing unused keys.");
 }
 } else {
 logError("Failed to get stats: %d", err);
 }
}

void configDumpNvsContents() {
 logInfo("Starting NVS Dump...");
 
 // Iterator for all namespaces and keys
 nvs_iterator_t it = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY);
 
 if (it == NULL) {
 logInfo("No entries found or iterator failed.");
 return;
 }

 logInfo("\n=== NVS CONTENT DUMP ===");
 logPrintf("%-12s | %-20s | %-4s | %s\n", "Namespace", "Key", "Type", "Value");
 logPrintf("-------------|----------------------|------|--------------------------------\n");

 while (it != NULL) {
 nvs_entry_info_t info;
 nvs_entry_info(it, &info);
 
 // Open handle to read value
 nvs_handle_t handle;
 esp_err_t err = nvs_open(info.namespace_name, NVS_READONLY, &handle);
 
 char value_str[128] = "ERR";
 
 if (err == ESP_OK) {
 switch(info.type) {
 case NVS_TYPE_U8: {
 uint8_t v; nvs_get_u8(handle, info.key, &v);
 snprintf(value_str, sizeof(value_str), "%u", v);
 break;
 }
 case NVS_TYPE_I8: {
 int8_t v; nvs_get_i8(handle, info.key, &v);
 snprintf(value_str, sizeof(value_str), "%d", v);
 break;
 }
 case NVS_TYPE_U16: {
 uint16_t v; nvs_get_u16(handle, info.key, &v);
 snprintf(value_str, sizeof(value_str), "%u", v);
 break;
 }
 case NVS_TYPE_I16: {
 int16_t v; nvs_get_i16(handle, info.key, &v);
 snprintf(value_str, sizeof(value_str), "%d", v);
 break;
 }
 case NVS_TYPE_U32: {
 uint32_t v; nvs_get_u32(handle, info.key, &v);
 snprintf(value_str, sizeof(value_str), "%u", v);
 break;
 }
 case NVS_TYPE_I32: {
 int32_t v; nvs_get_i32(handle, info.key, &v);
 snprintf(value_str, sizeof(value_str), "%d", v);
 break;
 }
 case NVS_TYPE_U64: {
 uint64_t v; nvs_get_u64(handle, info.key, &v);
 snprintf(value_str, sizeof(value_str), "%llu", v);
 break;
 }
 case NVS_TYPE_I64: {
 int64_t v; nvs_get_i64(handle, info.key, &v);
 snprintf(value_str, sizeof(value_str), "%lld", v);
 break;
 }
 case NVS_TYPE_STR: {
 size_t len = 0;
 nvs_get_str(handle, info.key, NULL, &len);
 if (len < sizeof(value_str)) {
 nvs_get_str(handle, info.key, value_str, &len);
 } else {
 snprintf(value_str, sizeof(value_str), "[STR too long: %u]", len);
 }
 break;
 }
 case NVS_TYPE_BLOB: {
 size_t len = 0;
 nvs_get_blob(handle, info.key, NULL, &len);
 snprintf(value_str, sizeof(value_str), "[BLOB: %u bytes]", len);
 break;
 }
 default:
 snprintf(value_str, sizeof(value_str), "?");
 }
 nvs_close(handle);
 } else {
 snprintf(value_str, sizeof(value_str), "OPEN ERR");
 }

 // Short type string
 const char* type_str = "UNK";
 switch(info.type) {
 case NVS_TYPE_U8: type_str = "U8"; break;
 case NVS_TYPE_I8: type_str = "I8"; break;
 case NVS_TYPE_U16: type_str = "U16"; break;
 case NVS_TYPE_I16: type_str = "I16"; break;
 case NVS_TYPE_U32: type_str = "U32"; break;
 case NVS_TYPE_I32: type_str = "I32"; break;
 case NVS_TYPE_U64: type_str = "U64"; break;
 case NVS_TYPE_I64: type_str = "I64"; break;
 case NVS_TYPE_STR: type_str = "STR"; break;
 case NVS_TYPE_BLOB: type_str = "BLOB"; break;
 case NVS_TYPE_ANY: type_str = "ANY"; break;
 }

 logPrintf("%-12s | %-20s | %-4s | %s\n", info.namespace_name, info.key, type_str, value_str);
 
 it = nvs_entry_next(it);
 }
 
 logInfo("=== END DUMP ===\n");
}

void configEraseNamespace(const char* ns) {
 if (ns == NULL || strlen(ns) == 0) return;

 logInfo("Erasing namespace '%s'...", ns);
 
 nvs_handle_t handle;
 esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
 
 if (err == ESP_OK) {
 err = nvs_erase_all(handle);
 if (err == ESP_OK) {
 nvs_commit(handle);
 logInfo("Namespace '%s' erased.", ns);
 } else {
 logError("Failed to erase: %d", err);
 }
 nvs_close(handle);
 } else {
 logError("Failed to open namespace '%s': %d", ns, err);
 }
}

bool configEraseNvs() {
 logWarning("Erasing all NVS data...");
 prefs.end();
 esp_err_t err = nvs_flash_erase();
 if (err == ESP_OK) {
 logInfo("Erase complete. Rebooting in 2 seconds...");
 delay(2000);
 systemSafeReboot("NVS erase complete");

 return true;
 } else {
 logError("Erase failed: %d", err);
 return false;
 }
}
