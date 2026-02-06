/**
 * @file oled_dashboard.h
 * @brief Local SSD1306 OLED Dashboard
 */

#ifndef OLED_DASHBOARD_H
#define OLED_DASHBOARD_H

#include <Arduino.h>

/**
 * @brief Initialize the OLED dashboard
 * @return true if successful
 */
bool oledDashboardInit();

/**
 * @brief Update the dashboard data
 * Called periodically from a background task
 */
void oledDashboardUpdate();

#endif // OLED_DASHBOARD_H
