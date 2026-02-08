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

void configCacheRefresh() {
    config_cache_t temp_config;
    memset(&temp_config, 0, sizeof(config_cache_t));

    // RS485 Bus
    temp_config.rs485_baud = (uint32_t)configGetInt(KEY_RS485_BAUD, 9600);
    
    // Spindle / Current Monitor
    temp_config.jxk10_enabled = (configGetInt(KEY_JXK10_ENABLED, 1) != 0);
    temp_config.jxk10_addr = (uint32_t)configGetInt(KEY_JXK10_ADDR, 1);
    temp_config.spindle_threshold = (uint32_t)configGetInt(KEY_SPINDLE_THRESHOLD, 30);
    temp_config.spindle_pause_threshold = (uint32_t)configGetInt(KEY_SPINDL_PAUSE_THR, 25);
    temp_config.spindle_pause_enabled = (configGetInt(KEY_SPINDL_PAUSE_EN, 1) != 0);
    temp_config.spindle_rated_amps = configGetFloat(KEY_SPINDLE_RATED_AMPS, 24.5f);
    
    // Motion Safety
    temp_config.strict_limits = (configGetInt(KEY_MOTION_STRICT_LIMITS, 1) != 0);
    temp_config.stall_timeout_ms = (uint32_t)configGetInt(KEY_STALL_TIMEOUT, 2000);
    temp_config.target_margin_mm = configGetFloat(KEY_TARGET_MARGIN, 0.1f);
    temp_config.stall_threshold_mm = configGetFloat(KEY_STALL_THRESHOLD, 5.0f);
    temp_config.deviation_warning_mm = configGetFloat(KEY_DEV_WARN, 1.0f);
    temp_config.deviation_critical_mm = configGetFloat(KEY_DEV_CRIT, 2.5f);
    
    // Network
    temp_config.wifi_ap_enabled = (configGetInt(KEY_WIFI_AP_EN, 1) != 0);
    temp_config.web_port = (uint32_t)configGetInt(KEY_WEB_PORT, 80);
    
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
        uint32_t val = (uint32_t)configGetInt(key, 9600);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.rs485_baud = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_JXK10_ENABLED) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.jxk10_enabled = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_JXK10_ADDR) == 0) {
        uint32_t val = (uint32_t)configGetInt(key, 1);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.jxk10_addr = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_SPINDLE_THRESHOLD) == 0) {
        uint32_t val = (uint32_t)configGetInt(key, 30);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.spindle_threshold = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_SPINDL_PAUSE_THR) == 0) {
        uint32_t val = (uint32_t)configGetInt(key, 25);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.spindle_pause_threshold = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_SPINDL_PAUSE_EN) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.spindle_pause_enabled = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_SPINDLE_RATED_AMPS) == 0) {
        float val = configGetFloat(key, 24.5f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.spindle_rated_amps = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_MOTION_STRICT_LIMITS) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.strict_limits = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_STALL_TIMEOUT) == 0) {
        uint32_t val = (uint32_t)configGetInt(key, 2000);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.stall_timeout_ms = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_TARGET_MARGIN) == 0) {
        float val = configGetFloat(key, 0.1f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.target_margin_mm = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_STALL_THRESHOLD) == 0) {
        float val = configGetFloat(key, 5.0f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.stall_threshold_mm = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_DEV_WARN) == 0) {
        float val = configGetFloat(key, 1.0f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.deviation_warning_mm = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_DEV_CRIT) == 0) {
        float val = configGetFloat(key, 2.5f);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.deviation_critical_mm = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_WIFI_AP_EN) == 0) {
        bool val = (configGetInt(key, 1) != 0);
        portENTER_CRITICAL(&config_cache_mux);
        g_config.wifi_ap_enabled = val;
        portEXIT_CRITICAL(&config_cache_mux);
    } else if (strcmp(key, KEY_WEB_PORT) == 0) {
        uint32_t val = (uint32_t)configGetInt(key, 80);
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
