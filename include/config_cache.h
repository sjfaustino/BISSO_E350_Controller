/**
 * @file config_cache.h
 * @brief High-performance Typed Configuration Cache (PHASE 6.7)
 * @details Provides O(1) access to commonly used configuration parameters.
 */

#ifndef CONFIG_CACHE_H
#define CONFIG_CACHE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief System-wide configuration cache struct
 * @details All fields are O(1) access and type-safe.
 */
typedef struct {
    // RS485 Bus
    uint32_t rs485_baud;
    
    // Spindle / Current Monitor
    bool jxk10_enabled;
    uint32_t jxk10_addr;
    uint32_t spindle_threshold;
    uint32_t spindle_pause_threshold;
    float spindle_rated_amps;
    
    // Motion Safety
    bool strict_limits;
    uint32_t stall_timeout_ms;
    float target_margin_mm;
    float stall_threshold_mm;
    float deviation_warning_mm;
    float deviation_critical_mm;
    
    // Network
    bool wifi_ap_enabled;
    uint32_t web_port;
    
    // Hardware Features
    bool lcd_enabled;
    bool buzzer_enabled;
    
    bool cache_valid;
} config_cache_t;

/**
 * @brief Global configuration cache instance
 */
extern config_cache_t g_config;

/**
 * @brief Initialize the configuration cache
 * @details Loads all fields from the unified configuration system.
 */
void configCacheInit();

/**
 * @brief Refresh the entire configuration cache
 */
void configCacheRefresh();

/**
 * @brief Update a specific field in the cache based on key
 * @param key The configuration key that changed
 */
void configCacheUpdate(const char* key);

#endif // CONFIG_CACHE_H
