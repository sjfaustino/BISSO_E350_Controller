/**
 * @file auto_report.h
 * @brief Automatic Position Reporting System (M154 G-code support)
 * @details Non-blocking position reporting at configurable intervals
 */

#ifndef AUTO_REPORT_H
#define AUTO_REPORT_H

#include <stdint.h>

/**
 * @brief Initialize auto-report system
 */
void autoReportInit();

/**
 * @brief Set auto-report interval and enable/disable
 * @param interval_sec Interval in seconds (0 = disable)
 * @return true if successful, false otherwise
 */
bool autoReportSetInterval(uint32_t interval_sec);

/**
 * @brief Get current auto-report interval
 * @return Interval in seconds (0 = disabled)
 */
uint32_t autoReportGetInterval();

/**
 * @brief Check if auto-report is enabled
 * @return true if enabled, false otherwise
 */
bool autoReportIsEnabled();

/**
 * @brief Update auto-report (called periodically from motion loop)
 * Non-blocking check: reports position if interval elapsed
 */
void autoReportUpdate();

/**
 * @brief Disable auto-report (called during E-Stop)
 */
void autoReportDisable();

#endif // AUTO_REPORT_H
