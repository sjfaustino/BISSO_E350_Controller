/**
 * @file rtc_manager.h
 * @brief DS3231 RTC Manager for KC868-A16 v3.1
 * @project BISSO E350 Controller
 */

#pragma once

#include <Arduino.h>
#include "board_variant.h"

#if BOARD_HAS_RTC_DS3231

/**
 * @brief RTC initialization - call after I2C is initialized
 * @return true if RTC detected and communicating
 */
bool rtcInit();

/**
 * @brief Check if RTC is available
 */
bool rtcIsAvailable();

/**
 * @brief Get current date/time from RTC
 * @param year Full year (e.g., 2026)
 * @param month 1-12
 * @param day 1-31
 * @param hour 0-23
 * @param minute 0-59
 * @param second 0-59
 * @return true if read successful
 */
bool rtcGetDateTime(int* year, int* month, int* day, int* hour, int* minute, int* second);

/**
 * @brief Set RTC date/time
 * @return true if set successful
 */
bool rtcSetDateTime(int year, int month, int day, int hour, int minute, int second);

/**
 * @brief Get formatted date string (YYYY-MM-DD)
 */
void rtcGetDateString(char* buffer, size_t size);

/**
 * @brief Get formatted time string (HH:MM:SS)
 */
void rtcGetTimeString(char* buffer, size_t size);

/**
 * @brief Get RTC temperature (DS3231 has built-in temp sensor)
 * @return Temperature in Celsius
 */
float rtcGetTemperature();

/**
 * @brief Sync system time with RTC
 */
void rtcSyncSystemTime();

/**
 * @brief Get current Unix epoch from RTC
 * @return Epoch time in seconds
 */
uint32_t rtcGetCurrentEpoch();

/**
 * @brief Check if RTC time is valid (not factory default 2000-01-01)
 * @return true if time appears to be set, false if default
 */
bool rtcIsTimeSet();

/**
 * @brief Sync RTC from NTP if WiFi is connected
 * @return true if sync successful
 */
bool rtcSyncFromNTP();

/**
 * @brief Check if time needs sync and perform if internet available
 * Call this after WiFi connects. Shows LCD warning if no sync possible.
 */
void rtcCheckAndSync();

#endif // BOARD_HAS_RTC_DS3231

