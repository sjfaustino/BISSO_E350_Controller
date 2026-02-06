/**
 * @file rtc_manager.cpp
 * @brief DS3231 RTC Manager for KC868-A16 v3.1
 * @project BISSO E350 Controller
 */

#include "rtc_manager.h"
#include "board_variant.h"
#include "serial_logger.h"
#include <Wire.h>
#include <time.h>
#include <WiFi.h>


#if BOARD_HAS_RTC_DS3231

static bool rtc_available = false;

// DS3231 Register addresses
#define DS3231_ADDR       I2C_ADDR_RTC_DS3231  // 0x68
#define DS3231_REG_SEC    0x00
#define DS3231_REG_MIN    0x01
#define DS3231_REG_HOUR   0x02
#define DS3231_REG_DAY    0x03
#define DS3231_REG_DATE   0x04
#define DS3231_REG_MONTH  0x05
#define DS3231_REG_YEAR   0x06
#define DS3231_REG_TEMP   0x11

// BCD conversion helpers
static uint8_t bcdToDec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static uint8_t decToBcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

bool rtcInit() {
    // Retry detection up to 3 times - I2C bus may be unstable at boot
    for (int retry = 0; retry < 3; retry++) {
        if (retry > 0) {
            delay(50);  // Give bus time to settle between retries
        }
        
        Wire.beginTransmission(DS3231_ADDR);
        if (Wire.endTransmission() == 0) {
            rtc_available = true;
            logInfo("[RTC] DS3231 detected at 0x%02X", DS3231_ADDR);
            
            // Read and log current time
            char dateStr[16], timeStr[16];
            rtcGetDateString(dateStr, sizeof(dateStr));
            rtcGetTimeString(timeStr, sizeof(timeStr));
            logInfo("[RTC] Current: %s %s", dateStr, timeStr);
            
            return true;
        }
        
        if (retry < 2) {
            logDebug("[RTC] Detection attempt %d/3 failed, retrying...", retry + 1);
        }
    }
    
    rtc_available = false;
    logWarning("[RTC] DS3231 not found at 0x%02X after 3 attempts", DS3231_ADDR);
    return false;
}


bool rtcIsAvailable() {
    return rtc_available;
}

bool rtcGetDateTime(int* year, int* month, int* day, int* hour, int* minute, int* second) {
    if (!rtc_available) return false;
    
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(DS3231_REG_SEC);
    if (Wire.endTransmission() != 0) return false;
    
    Wire.requestFrom(DS3231_ADDR, 7);
    if (Wire.available() < 7) return false;
    
    *second = bcdToDec(Wire.read() & 0x7F);
    *minute = bcdToDec(Wire.read());
    *hour = bcdToDec(Wire.read() & 0x3F);  // 24-hour format
    Wire.read();  // Skip day of week
    *day = bcdToDec(Wire.read());
    *month = bcdToDec(Wire.read() & 0x1F);
    *year = 2000 + bcdToDec(Wire.read());
    
    return true;
}

bool rtcSetDateTime(int year, int month, int day, int hour, int minute, int second) {
    if (!rtc_available) return false;
    
    // Validate ranges
    if (year < 2000 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > 31) return false;
    if (hour < 0 || hour > 23) return false;
    if (minute < 0 || minute > 59) return false;
    if (second < 0 || second > 59) return false;
    
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(DS3231_REG_SEC);
    Wire.write(decToBcd(second));
    Wire.write(decToBcd(minute));
    Wire.write(decToBcd(hour));
    Wire.write(1);  // Day of week (unused, default to 1)
    Wire.write(decToBcd(day));
    Wire.write(decToBcd(month));
    Wire.write(decToBcd(year - 2000));
    
    if (Wire.endTransmission() == 0) {
        logInfo("[RTC] Time set to: %04d-%02d-%02d %02d:%02d:%02d", 
                year, month, day, hour, minute, second);
        return true;
    }
    return false;
}

void rtcGetDateString(char* buffer, size_t size) {
    int y, m, d, h, min, s;
    if (rtcGetDateTime(&y, &m, &d, &h, &min, &s)) {
        snprintf(buffer, size, "%04d-%02d-%02d", y, m, d);
    } else {
        snprintf(buffer, size, "----/--/--");
    }
}

void rtcGetTimeString(char* buffer, size_t size) {
    int y, m, d, h, min, s;
    if (rtcGetDateTime(&y, &m, &d, &h, &min, &s)) {
        snprintf(buffer, size, "%02d:%02d:%02d", h, min, s);
    } else {
        snprintf(buffer, size, "--:--:--");
    }
}

float rtcGetTemperature() {
    if (!rtc_available) return -999.0f;
    
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(DS3231_REG_TEMP);
    if (Wire.endTransmission() != 0) return -999.0f;
    
    Wire.requestFrom(DS3231_ADDR, 2);
    if (Wire.available() < 2) return -999.0f;
    
    int8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    
    // Temperature = MSB + (LSB >> 6) * 0.25
    return (float)msb + ((lsb >> 6) * 0.25f);
}

void rtcSyncSystemTime() {
    int y, m, d, h, min, s;
    if (rtcGetDateTime(&y, &m, &d, &h, &min, &s)) {
        struct tm tm_time;
        tm_time.tm_year = y - 1900;
        tm_time.tm_mon = m - 1;
        tm_time.tm_mday = d;
        tm_time.tm_hour = h;
        tm_time.tm_min = min;
        tm_time.tm_sec = s;
        tm_time.tm_isdst = -1;
        
        time_t t = mktime(&tm_time);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        
        logInfo("[RTC] System time synced from RTC");
    }
}

bool rtcIsTimeSet() {
    int y, m, d, h, min, s;
    if (!rtcGetDateTime(&y, &m, &d, &h, &min, &s)) {
        return false;
    }
    // Factory default is 2000-01-01
    return (y > 2000);
}

bool rtcSyncFromNTP() {
    if (!rtc_available) return false;
    
    // Configure NTP - use pool.ntp.org for reliable time
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
    
    // Wait for NTP sync (max 10 seconds)
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 20) {
        delay(500);
        retry++;
    }
    
    if (retry >= 20) {
        logWarning("[RTC] NTP sync timeout - no internet?");
        return false;
    }
    
    // Got NTP time - set the RTC
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    int second = timeinfo.tm_sec;
    
    if (rtcSetDateTime(year, month, day, hour, minute, second)) {
        logInfo("[RTC] Synced from NTP: %04d-%02d-%02d %02d:%02d:%02d", 
                year, month, day, hour, minute, second);
        return true;
    }
    
    logError("[RTC] Failed to write NTP time to RTC");
    return false;
}

void rtcCheckAndSync() {
    if (!rtc_available) return;
    
    // Check if time is set (not factory default 2000-01-01)
    if (rtcIsTimeSet()) {
        // Time already set - sync system time from RTC
        rtcSyncSystemTime();
        return;
    }
    
    logWarning("[RTC] Time not set (factory default 2000-01-01)");
    
    // Try NTP sync if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        logInfo("[RTC] Attempting NTP sync...");
        if (rtcSyncFromNTP()) {
            rtcSyncSystemTime();  // Also update system time
            return;
        }
    }
    
    // No internet or NTP failed - show LCD warning
    logWarning("[RTC] Cannot sync - no internet. Use 'rtc set YYYY-MM-DD HH:MM:SS'");
    
    // Show warning on LCD (non-blocking)
    extern void lcdMessageSet(const char* message, uint32_t duration_ms);
    lcdMessageSet("RTC TIME NOT SET!", 5000);
}


#endif // BOARD_HAS_RTC_DS3231

