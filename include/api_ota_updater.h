/**
 * @file api_ota_updater.h
 * @brief OTA Firmware Update Handler (PHASE 5.1: Maintenance & Updates)
 * @details Manages secure firmware updates via HTTP/REST API
 * @project BISSO E350 Controller
 */

#ifndef API_OTA_UPDATER_H
#define API_OTA_UPDATER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * OTA Update Status Codes
 */
typedef enum {
    OTA_STATUS_IDLE = 0,
    OTA_STATUS_IN_PROGRESS = 1,
    OTA_STATUS_VALIDATING = 2,
    OTA_STATUS_SUCCESS = 3,
    OTA_STATUS_ERROR = 4,
    OTA_STATUS_CRC_FAILED = 5,
    OTA_STATUS_SIZE_ERROR = 6,
    OTA_STATUS_PARTITION_ERROR = 7
} ota_status_t;

/**
 * OTA Update Statistics
 */
typedef struct {
    ota_status_t status;              // Current OTA status
    uint32_t bytes_received;          // Total bytes received
    uint32_t total_size;              // Expected total size
    uint32_t crc32;                   // CRC32 of received data
    uint32_t last_error;              // Error code from last operation
    const char* last_error_msg;       // Human-readable error message
    uint64_t update_timestamp;        // Timestamp of last update attempt
} ota_status_info_t;

/**
 * Initialize OTA updater
 */
void otaUpdaterInit();

/**
 * Start receiving OTA data chunk
 * @param total_size Expected total firmware size
 * @param filename Optional firmware filename for logging
 * @return true if ready to receive data, false on error
 */
bool otaUpdaterStartUpdate(uint32_t total_size, const char* filename);

/**
 * Receive OTA data chunk
 * @param data Pointer to chunk data
 * @param len Length of chunk
 * @return true if chunk accepted, false on error
 */
bool otaUpdaterReceiveChunk(const uint8_t* data, size_t len);

/**
 * Finalize and validate OTA update
 * @return true if update successful and reboot scheduled, false on error
 */
bool otaUpdaterFinalize();

/**
 * Cancel current OTA operation
 */
void otaUpdaterCancel();

/**
 * Get current OTA status
 * @return OTA status information
 */
ota_status_info_t otaUpdaterGetStatus();

/**
 * Export OTA status as JSON
 * @param buffer Output buffer
 * @param buffer_size Maximum size
 * @return Number of bytes written, 0 on error
 */
size_t otaUpdaterExportJSON(char* buffer, size_t buffer_size);

/**
 * Print OTA diagnostics
 */
void otaUpdaterPrintDiagnostics();

#ifdef __cplusplus
}
#endif

#endif // API_OTA_UPDATER_H
