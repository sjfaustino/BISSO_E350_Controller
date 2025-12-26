/**
 * @file api_ota_updater.cpp
 * @brief OTA Firmware Update Implementation (PHASE 5.1)
 */

#include "api_ota_updater.h"
#include "serial_logger.h"
#include "system_events.h" // PHASE 5.10: Event-driven architecture
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <string.h>
#include <stdio.h>

// OTA state tracking
static struct {
    esp_ota_handle_t update_handle;
    const esp_partition_t* update_partition;
    uint32_t total_size;
    uint32_t bytes_written;
    uint32_t crc32;
    ota_status_t status;
    uint32_t last_error;
    char last_error_msg[128];
    uint64_t start_timestamp;
} ota_state;

// CRC32 calculation
static uint32_t crc32_table[256];
static bool crc_table_initialized = false;

static void initCrc32Table() {
    if (crc_table_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (((crc & 1) ? 0xEDB88320UL : 0));
        }
        crc32_table[i] = crc;
    }
    crc_table_initialized = true;
}

static uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t len) {
    initCrc32Table();
    crc ^= 0xFFFFFFFFUL;
    while (len--) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ *data++) & 0xFF];
    }
    return crc ^ 0xFFFFFFFFUL;
}

void otaUpdaterInit() {
    memset(&ota_state, 0, sizeof(ota_state));
    ota_state.status = OTA_STATUS_IDLE;
    logInfo("[OTA] Initialized");
}

bool otaUpdaterStartUpdate(uint32_t total_size, const char* filename) {
    // Validate size
    if (total_size < 1024 || total_size > 2 * 1024 * 1024) {  // 1KB to 2MB
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "Invalid firmware size: %lu bytes", (unsigned long)total_size);
        ota_state.status = OTA_STATUS_SIZE_ERROR;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    // Get running partition to find update partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "Cannot identify running partition");
        ota_state.status = OTA_STATUS_PARTITION_ERROR;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    // Get next update partition (the other OTA partition)
    ota_state.update_partition = esp_ota_get_next_update_partition(running);
    if (!ota_state.update_partition) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "Cannot find update partition");
        ota_state.status = OTA_STATUS_PARTITION_ERROR;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    // Begin OTA write
    esp_err_t err = esp_ota_begin(ota_state.update_partition, OTA_SIZE_UNKNOWN, &ota_state.update_handle);
    if (err != ESP_OK) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "Failed to begin OTA: %d", (int)err);
        ota_state.status = OTA_STATUS_ERROR;
        ota_state.last_error = err;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    ota_state.total_size = total_size;
    ota_state.bytes_written = 0;
    ota_state.crc32 = 0;
    ota_state.status = OTA_STATUS_IN_PROGRESS;
    ota_state.start_timestamp = millis();

    // PHASE 5.10: Signal OTA update requested event
    systemEventsSystemSet(EVENT_SYSTEM_OTA_REQUESTED);

    logInfo("[OTA] Update started: %lu bytes, partition %s",
            (unsigned long)total_size, ota_state.update_partition->label);

    return true;
}

bool otaUpdaterReceiveChunk(const uint8_t* data, size_t len) {
    if (ota_state.status != OTA_STATUS_IN_PROGRESS) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "OTA not in progress (status: %d)", ota_state.status);
        return false;
    }

    if (!data || len == 0) return false;

    // Check size doesn't exceed expected
    if (ota_state.bytes_written + len > ota_state.total_size) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "Firmware exceeds expected size");
        ota_state.status = OTA_STATUS_SIZE_ERROR;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    // Write data to partition
    esp_err_t err = esp_ota_write(ota_state.update_handle, data, len);
    if (err != ESP_OK) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "OTA write failed: %d", (int)err);
        ota_state.status = OTA_STATUS_ERROR;
        ota_state.last_error = err;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    // Update metrics
    ota_state.bytes_written += len;
    ota_state.crc32 = updateCrc32(ota_state.crc32, data, len);

    // Log progress every 64KB
    if (ota_state.bytes_written % (64 * 1024) == 0) {
        logInfo("[OTA] Progress: %lu / %lu bytes",
                (unsigned long)ota_state.bytes_written,
                (unsigned long)ota_state.total_size);
    }

    return true;
}

bool otaUpdaterFinalize() {
    if (ota_state.status != OTA_STATUS_IN_PROGRESS) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "OTA not in progress");
        return false;
    }

    // Check we received all expected data
    if (ota_state.bytes_written != ota_state.total_size) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "Size mismatch: %lu received, %lu expected",
                 (unsigned long)ota_state.bytes_written,
                 (unsigned long)ota_state.total_size);
        ota_state.status = OTA_STATUS_SIZE_ERROR;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    // Finalize OTA
    ota_state.status = OTA_STATUS_VALIDATING;
    esp_err_t err = esp_ota_end(ota_state.update_handle);
    if (err != ESP_OK) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "OTA validation failed: %d", (int)err);
        ota_state.status = OTA_STATUS_ERROR;
        ota_state.last_error = err;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    // Set new partition as boot partition
    err = esp_ota_set_boot_partition(ota_state.update_partition);
    if (err != ESP_OK) {
        snprintf(ota_state.last_error_msg, sizeof(ota_state.last_error_msg),
                 "Failed to set boot partition: %d", (int)err);
        ota_state.status = OTA_STATUS_PARTITION_ERROR;
        ota_state.last_error = err;
        logError("[OTA] %s", ota_state.last_error_msg);
        return false;
    }

    ota_state.status = OTA_STATUS_SUCCESS;
    logInfo("[OTA] [OK] Firmware validated and installed. Rebooting in 2 seconds...");

    // Schedule reboot (give time for response to be sent)
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP.restart();

    return true;
}

void otaUpdaterCancel() {
    if (ota_state.status == OTA_STATUS_IN_PROGRESS) {
        esp_ota_abort(ota_state.update_handle);
    }
    ota_state.status = OTA_STATUS_IDLE;
    ota_state.bytes_written = 0;
    logInfo("[OTA] Update cancelled");
}

ota_status_info_t otaUpdaterGetStatus() {
    ota_status_info_t info;
    info.status = ota_state.status;
    info.bytes_received = ota_state.bytes_written;
    info.total_size = ota_state.total_size;
    info.crc32 = ota_state.crc32;
    info.last_error = ota_state.last_error;
    info.last_error_msg = ota_state.last_error_msg;
    info.update_timestamp = ota_state.start_timestamp;
    return info;
}

size_t otaUpdaterExportJSON(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 256) return 0;

    ota_status_info_t status = otaUpdaterGetStatus();
    const char* status_str = "UNKNOWN";

    switch (status.status) {
        case OTA_STATUS_IDLE: status_str = "IDLE"; break;
        case OTA_STATUS_IN_PROGRESS: status_str = "IN_PROGRESS"; break;
        case OTA_STATUS_VALIDATING: status_str = "VALIDATING"; break;
        case OTA_STATUS_SUCCESS: status_str = "SUCCESS"; break;
        case OTA_STATUS_ERROR: status_str = "ERROR"; break;
        case OTA_STATUS_CRC_FAILED: status_str = "CRC_FAILED"; break;
        case OTA_STATUS_SIZE_ERROR: status_str = "SIZE_ERROR"; break;
        case OTA_STATUS_PARTITION_ERROR: status_str = "PARTITION_ERROR"; break;
    }

    size_t offset = snprintf(buffer, buffer_size,
        "{\"status\":\"%s\",\"bytes_received\":%lu,\"total_size\":%lu,"
        "\"crc32\":%lu,\"error_code\":%lu,\"error_msg\":\"%s\"}",
        status_str,
        (unsigned long)status.bytes_received,
        (unsigned long)status.total_size,
        (unsigned long)status.crc32,
        (unsigned long)status.last_error,
        status.last_error_msg ? status.last_error_msg : "");

    return offset;
}

void otaUpdaterPrintDiagnostics() {
    ota_status_info_t status = otaUpdaterGetStatus();

    serialLoggerLock();
    Serial.println("\n[OTA] === Firmware Update Status ===");
    Serial.printf("Status: ");

    switch (status.status) {
        case OTA_STATUS_IDLE:
            Serial.println("IDLE (Ready for update)");
            break;
        case OTA_STATUS_IN_PROGRESS:
            Serial.printf("IN_PROGRESS (%lu / %lu bytes)\n",
                         (unsigned long)status.bytes_received,
                         (unsigned long)status.total_size);
            break;
        case OTA_STATUS_VALIDATING:
            Serial.println("VALIDATING (Checking integrity)");
            break;
        case OTA_STATUS_SUCCESS:
            Serial.println("SUCCESS (Waiting for reboot)");
            break;
        case OTA_STATUS_ERROR:
            Serial.printf("ERROR: %s (code: %lu)\n",
                         status.last_error_msg ? status.last_error_msg : "Unknown",
                         (unsigned long)status.last_error);
            break;
        default:
            Serial.printf("UNKNOWN STATUS: %d\n", status.status);
    }

    if (status.total_size > 0) {
        float percent = (float)status.bytes_received * 100.0f / status.total_size;
        Serial.printf("Progress: %.1f%%\n", percent);
        Serial.printf("CRC32: 0x%08lX\n", (unsigned long)status.crc32);
    }

    Serial.println();
    serialLoggerUnlock();
}
