/**
 * @file config_cache.cpp
 * @brief High-performance Typed Configuration Cache Implementation
 */

#include "config_cache.h"
#include "config_unified.h"
#include "config_keys.h"
#include "serial_logger.h"
#include <string.h>

// Global instance
config_cache_t g_config;

void configCacheInit() {
    logInfo("[CONFIG] Initializing Typed Cache...");
    memset(&g_config, 0, sizeof(config_cache_t));
    configCacheRefresh();
}

void configCacheRefresh() {
    // RS485 Bus
    g_config.rs485_baud = (uint32_t)configGetInt(KEY_RS485_BAUD, 9600);
    
    // Spindle / Current Monitor
    g_config.jxk10_enabled = (configGetInt(KEY_JXK10_ENABLED, 1) != 0);
    g_config.jxk10_addr = (uint32_t)configGetInt(KEY_JXK10_ADDR, 1);
    g_config.spindle_threshold = (uint32_t)configGetInt(KEY_SPINDLE_THRESHOLD, 30);
    g_config.spindle_pause_threshold = (uint32_t)configGetInt(KEY_SPINDL_PAUSE_THR, 25);
    g_config.spindle_rated_amps = configGetFloat(KEY_SPINDLE_RATED_AMPS, 24.5f);
    
    // Motion Safety
    g_config.strict_limits = (configGetInt(KEY_MOTION_STRICT_LIMITS, 1) != 0);
    g_config.stall_timeout_ms = (uint32_t)configGetInt(KEY_STALL_TIMEOUT, 2000);
    g_config.target_margin_mm = configGetFloat(KEY_TARGET_MARGIN, 0.1f);
    g_config.stall_threshold_mm = configGetFloat(KEY_STALL_THRESHOLD, 5.0f);
    g_config.deviation_warning_mm = configGetFloat(KEY_DEV_WARN, 1.0f);
    g_config.deviation_critical_mm = configGetFloat(KEY_DEV_CRIT, 2.5f);
    
    // Network
    g_config.wifi_ap_enabled = (configGetInt(KEY_WIFI_AP_EN, 1) != 0);
    g_config.web_port = (uint32_t)configGetInt(KEY_WEB_PORT, 80);
    
    // Hardware Features
    g_config.lcd_enabled = (configGetInt(KEY_LCD_EN, 1) != 0);
    g_config.buzzer_enabled = (configGetInt(KEY_BUZZER_EN, 1) != 0);
    
    g_config.cache_valid = true;
    logDebug("[CONFIG] Typed cache refreshed");
}

void configCacheUpdate(const char* key) {
    if (!key) return;

    if (strcmp(key, KEY_RS485_BAUD) == 0) {
        g_config.rs485_baud = (uint32_t)configGetInt(key, 9600);
    } else if (strcmp(key, KEY_JXK10_ENABLED) == 0) {
        g_config.jxk10_enabled = (configGetInt(key, 1) != 0);
    } else if (strcmp(key, KEY_JXK10_ADDR) == 0) {
        g_config.jxk10_addr = (uint32_t)configGetInt(key, 1);
    } else if (strcmp(key, KEY_SPINDLE_THRESHOLD) == 0) {
        g_config.spindle_threshold = (uint32_t)configGetInt(key, 30);
    } else if (strcmp(key, KEY_SPINDL_PAUSE_THR) == 0) {
        g_config.spindle_pause_threshold = (uint32_t)configGetInt(key, 25);
    } else if (strcmp(key, KEY_SPINDLE_RATED_AMPS) == 0) {
        g_config.spindle_rated_amps = configGetFloat(key, 24.5f);
    } else if (strcmp(key, KEY_MOTION_STRICT_LIMITS) == 0) {
        g_config.strict_limits = (configGetInt(key, 1) != 0);
    } else if (strcmp(key, KEY_STALL_TIMEOUT) == 0) {
        g_config.stall_timeout_ms = (uint32_t)configGetInt(key, 2000);
    } else if (strcmp(key, KEY_TARGET_MARGIN) == 0) {
        g_config.target_margin_mm = configGetFloat(key, 0.1f);
    } else if (strcmp(key, KEY_STALL_THRESHOLD) == 0) {
        g_config.stall_threshold_mm = configGetFloat(key, 5.0f);
    } else if (strcmp(key, KEY_DEV_WARN) == 0) {
        g_config.deviation_warning_mm = configGetFloat(key, 1.0f);
    } else if (strcmp(key, KEY_DEV_CRIT) == 0) {
        g_config.deviation_critical_mm = configGetFloat(key, 2.5f);
    } else if (strcmp(key, KEY_WIFI_AP_EN) == 0) {
        g_config.wifi_ap_enabled = (configGetInt(key, 1) != 0);
    } else if (strcmp(key, KEY_WEB_PORT) == 0) {
        g_config.web_port = (uint32_t)configGetInt(key, 80);
    } else if (strcmp(key, KEY_LCD_EN) == 0) {
        g_config.lcd_enabled = (configGetInt(key, 1) != 0);
    } else if (strcmp(key, KEY_BUZZER_EN) == 0) {
        g_config.buzzer_enabled = (configGetInt(key, 1) != 0);
    }
}
