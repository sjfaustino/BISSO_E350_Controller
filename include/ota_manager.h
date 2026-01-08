#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>

/**
 * @brief Result of an update check
 */
struct UpdateCheckResult {
    bool available;
    char latest_version[32];
    char download_url[256];
    char release_notes[512];
};

/**
 * @brief Initializes the OTA manager
 */
void otaInit(void);

/**
 * @brief Checks GitHub for a newer firmware version
 * @return UpdateCheckResult struct containing findings
 */
UpdateCheckResult otaCheckForUpdate(void);

/**
 * @brief Get the cached result of the boot-time update check
 * @return Pointer to the cached UpdateCheckResult
 */
const UpdateCheckResult* otaGetCachedResult(void);

/**
 * @brief Starts the background firmware update process
 * @param download_url The URL of the binary file on GitHub
 * @return True if update started successfully
 */
bool otaPerformUpdate(const char* download_url);

/**
 * @brief Get the progress of the current update (0-100)
 */
int otaGetProgress(void);

/**
 * @brief Check if an update is currently in progress
 */
bool otaIsUpdating(void);

/**
 * @brief Start background update check (call after WiFi connects)
 * Non-blocking - spawns a FreeRTOS task
 */
void otaStartBackgroundCheck(void);

/**
 * @brief Get the cached update check result
 * @return Pointer to cached result (valid until next check)
 */
const UpdateCheckResult* otaGetCachedResult(void);

/**
 * @brief Check if the background check has completed
 */
bool otaCheckComplete(void);

#endif // OTA_MANAGER_H
