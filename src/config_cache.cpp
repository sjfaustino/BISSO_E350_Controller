/**
 * @file config_cache.cpp
 * @brief High-performance Typed Configuration Cache Implementation
 */

#include "config_cache.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <string.h>
#include "system_utils.h" // PHASE 8.1

// Global instance
config_cache_t g_config;

void configCacheInit() {
    logModuleInit("CONFIG_CACHE");
    memset(&g_config, 0, sizeof(config_cache_t));
    configCacheRefresh();
}

// Spinlock for thread safety
static portMUX_TYPE config_cache_mux = portMUX_INITIALIZER_UNLOCKED;

// Helper: Validate integer range
static uint32_t validateInt(const char* key, uint32_t val, uint32_t min, uint32_t max, uint32_t def) {
    if (val < min || val > max) {
        logWarning("[CONFIG] Key '%s' value %lu out of range [%lu, %lu]. Using default %lu.", key, (unsigned long)val, (unsigned long)min, (unsigned long)max, (unsigned long)def);
        return def;
    }
    return val;
}

// Helper: Validate float range
static float validateFloat(const char* key, float val, float min, float max, float def) {
    if (val < min || val > max) {
        logWarning("[CONFIG] Key '%s' value %.3f out of range [%.3f, %.3f]. Using default %.3f.", key, val, min, max, def);
        return def;
    }
    return val;
}

void configCacheRefresh() {
    config_cache_t temp_config;
    memset(&temp_config, 0, sizeof(config_cache_t));

    // RS485 Bus
    // Baud: 1200 - 115200 (approx limits)
    temp_config.rs485_baud = validateInt(KEY_RS485_BAUD, (uint32_t)configGetInt(KEY_RS485_BAUD, 9600), 1200, 115200, 9600);
    
    // Spindle / Current Monitor
    temp_config.jxk10_enabled = (configGetInt(KEY_JXK10_ENABLED, 1) != 0);
    // Address: 1 - 247 (Modbus)
    temp_config.jxk10_addr = validateInt(KEY_JXK10_ADDR, (uint32_t)configGetInt(KEY_JXK10_ADDR, 1), 1, 247, 1);
    // Thresholds: 1 - 100 Amps
    temp_config.spindle_threshold = validateInt(KEY_SPINDLE_THRESHOLD, (uint32_t)configGetInt(KEY_SPINDLE_THRESHOLD, 30), 1, 100, 30);
    temp_config.spindle_pause_threshold = validateInt(KEY_SPINDL_PAUSE_THR, (uint32_t)configGetInt(KEY_SPINDL_PAUSE_THR, 25), 1, 100, 25);
    temp_config.spindle_pause_enabled = (configGetInt(KEY_SPINDL_PAUSE_EN, 1) != 0);
    // Rated Amps: 0.1 - 100.0
    temp_config.spindle_rated_amps = validateFloat(KEY_SPINDLE_RATED_AMPS, configGetFloat(KEY_SPINDLE_RATED_AMPS, 24.5f), 0.1f, 100.0f, 24.5f);
    
    // Motion Safety
    temp_config.strict_limits = (configGetInt(KEY_MOTION_STRICT_LIMITS, 1) != 0);
    // Stall Timeout: 100 - 60000 ms
    temp_config.stall_timeout_ms = validateInt(KEY_STALL_TIMEOUT, (uint32_t)configGetInt(KEY_STALL_TIMEOUT, 2000), 100, 60000, 2000);
    // Margins: 0.0 - 50.0 mm
    temp_config.target_margin_mm = validateFloat(KEY_TARGET_MARGIN, configGetFloat(KEY_TARGET_MARGIN, 0.1f), 0.0f, 50.0f, 0.1f);
    temp_config.stall_threshold_mm = validateFloat(KEY_STALL_THRESHOLD, configGetFloat(KEY_STALL_THRESHOLD, 5.0f), 0.1f, 50.0f, 5.0f);
    temp_config.deviation_warning_mm = validateFloat(KEY_DEV_WARN, configGetFloat(KEY_DEV_WARN, 1.0f), 0.1f, 50.0f, 1.0f);
    temp_config.deviation_critical_mm = validateFloat(KEY_DEV_CRIT, configGetFloat(KEY_DEV_CRIT, 2.5f), 0.1f, 50.0f, 2.5f);
    
    // Network
    temp_config.wifi_ap_enabled = (configGetInt(KEY_WIFI_AP_EN, 1) != 0);
    // Port: 1 - 65535
    temp_config.web_port = validateInt(KEY_WEB_PORT, (uint32_t)configGetInt(KEY_WEB_PORT, 80), 1, 65535, 80);
    
    // Hardware Features
    temp_config.lcd_enabled = (configGetInt(KEY_LCD_EN, 1) != 0);
    temp_config.buzzer_enabled = (configGetInt(KEY_BUZZER_EN, 1) != 0);
    
    temp_config.cache_valid = true;

    // Atomic update
    portENTER_CRITICAL(&config_cache_mux);
    memcpy(&g_config, &temp_config, sizeof(config_cache_t));
    portEXIT_CRITICAL(&config_cache_mux);

    logDebug("[CONFIG] Typed cache refreshed");
}

void configCacheUpdate(const char* key) {
    if (!key) return;

    if (strcmp(key, KEY_RS485_BAUD) == 0) {
        uint32_t val = validateInt(key, (uint32_t)configGetInt(key, 9600), 1200, 115200, 9600);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.rs485_baud = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_JXK10_ENABLED) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.jxk10_enabled = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_JXK10_ADDR) == 0) {
        uint32_t val = validateInt(key, (uint32_t)configGetInt(key, 1), 1, 247, 1);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.jxk10_addr = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_SPINDLE_THRESHOLD) == 0) {
        uint32_t val = validateInt(key, (uint32_t)configGetInt(key, 30), 1, 100, 30);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.spindle_threshold = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_SPINDL_PAUSE_THR) == 0) {
        uint32_t val = validateInt(key, (uint32_t)configGetInt(key, 25), 1, 100, 25);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.spindle_pause_threshold = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_SPINDL_PAUSE_EN) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.spindle_pause_enabled = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_SPINDLE_RATED_AMPS) == 0) {
        float val = validateFloat(key, configGetFloat(key, 24.5f), 0.1f, 100.0f, 24.5f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.spindle_rated_amps = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_MOTION_STRICT_LIMITS) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.strict_limits = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_STALL_TIMEOUT) == 0) {
        uint32_t val = validateInt(key, (uint32_t)configGetInt(key, 2000), 100, 60000, 2000);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.stall_timeout_ms = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_TARGET_MARGIN) == 0) {
        float val = validateFloat(key, configGetFloat(key, 0.1f), 0.0f, 50.0f, 0.1f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.target_margin_mm = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_STALL_THRESHOLD) == 0) {
        float val = validateFloat(key, configGetFloat(key, 5.0f), 0.1f, 50.0f, 5.0f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.stall_threshold_mm = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_DEV_WARN) == 0) {
        float val = validateFloat(key, configGetFloat(key, 1.0f), 0.1f, 50.0f, 1.0f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.deviation_warning_mm = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_DEV_CRIT) == 0) {
        float val = validateFloat(key, configGetFloat(key, 2.5f), 0.1f, 50.0f, 2.5f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.deviation_critical_mm = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_WIFI_AP_EN) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.wifi_ap_enabled = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_WEB_PORT) == 0) {
        uint32_t val = validateInt(key, (uint32_t)configGetInt(key, 80), 1, 65535, 80);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.web_port = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_LCD_EN) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.lcd_enabled = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_BUZZER_EN) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.buzzer_enabled = val;
        portEXIT_CRITICAL(&config_cache_mux);
    }
}
