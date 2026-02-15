/**
 * @file sd_card_manager.h
 * @brief SD Card management for KC868-A16 v3.1
 * @project BISSO E350 Controller
 * 
 * Provides SD card initialization, mounting, and file operations.
 * SD card is optional - system works normally without it.
 */

#pragma once

#include <Arduino.h>

#include "system_constants.h"

// SD card initialization and status
result_t sdCardInit();
bool sdCardIsPresent();
bool sdCardIsMounted();
void sdCardUnmount();

// Card information
struct SDCardInfo {
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t freeBytes;
    uint8_t cardType;  // 0=NONE, 1=MMC, 2=SD, 3=SDHC/SDXC
    const char* cardTypeName;
};

result_t sdCardGetInfo(SDCardInfo* info);

// File operations helpers
bool sdCardFileExists(const char* path);
size_t sdCardGetFileSize(const char* path);
result_t sdCardDeleteFile(const char* path);
result_t sdCardCreateDir(const char* path);
result_t sdCardMkdirRecursive(const char* path);
bool sdCardListDir(const char* path);

// Mount status string for CLI/logging
const char* sdCardGetStatusString();

// Health check results
enum SDCardHealth {
    SD_HEALTH_OK = 0,           // Card is healthy
    SD_HEALTH_READ_ONLY,        // Card is write-protected
    SD_HEALTH_WRITE_FAILED,     // Write operation failed
    SD_HEALTH_READ_FAILED,      // Read operation failed  
    SD_HEALTH_VERIFY_FAILED,    // Data verification failed (corruption)
    SD_HEALTH_DELETE_FAILED,    // Delete operation failed
    SD_HEALTH_NOT_MOUNTED       // Card not mounted
};

/**
 * @brief Perform quick health check on SD card
 * 
 * Creates a test file, writes test data, reads back and verifies,
 * then deletes the file. This catches:
 * - Write-protected cards
 * - Failing/worn-out cards
 * - Basic FAT corruption
 * 
 * @return SDCardHealth enum indicating result
 */
SDCardHealth sdCardHealthCheck();

/**
 * @brief Get human-readable health check result string
 */
const char* sdCardHealthString(SDCardHealth result);

/**
 * @brief Get the last health check result
 */
SDCardHealth sdCardGetLastHealth();

/**
 * @brief Format/wipe SD card (delete all files and directories)
 * 
 * Recursively deletes all files and directories on the SD card.
 * This is equivalent to a "quick format" for user data purposes.
 * 
 * @return true if successful, false on error
 */
result_t sdCardFormat();
