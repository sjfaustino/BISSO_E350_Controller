/**
 * @file trash_bin_manager.cpp
 * @brief Trash Bin Management with 30-day auto-delete
 */

#include "trash_bin_manager.h"
#include "rtc_manager.h"
#include "serial_logger.h"
#include "sd_card_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TRASH_DIR "/.trash"
#define PURGE_AGE_SECONDS (30 * 24 * 60 * 60) // 30 days

static void purgeDirectory(FS* fs, const char* dirPath, uint32_t currentEpoch);

void trashBinInit() {
    logInfo("[TRASH] Initializing trash bins...");
    
    // mkdir() is a no-op if directory already exists; avoids VFS error log from exists() -> open()
    if (LittleFS.mkdir(TRASH_DIR)) {
        logInfo("[TRASH] Created LittleFS trash bin");
    }

    if (sdCardIsMounted()) {
        if (!SD.exists(TRASH_DIR)) {
            if (SD.mkdir(TRASH_DIR)) logInfo("[TRASH] Created SD trash bin");
            else logError("[TRASH] Failed to create SD trash bin");
        }
    }
}

bool trashBinMoveToTrash(FS* fs, const char* path) {
    if (!fs->exists(path)) return false;

    // Ensure trash dir exists (card might have been swapped)
    if (!fs->exists(TRASH_DIR)) {
        fs->mkdir(TRASH_DIR);
    }

    // Generate unique trash name: filename.ext.epoch
    uint32_t epoch = 0;
    #if BOARD_HAS_RTC_DS3231
        epoch = rtcGetCurrentEpoch();
    #endif
    String fileName = String(path);
    int lastSlash = fileName.lastIndexOf('/');
    if (lastSlash != -1) {
        fileName = fileName.substring(lastSlash + 1);
    }

    String trashPath = String(TRASH_DIR) + "/" + fileName + "." + String(epoch);
    
    logInfo("[TRASH] Moving %s -> %s", path, trashPath.c_str());
    
    // rename() works for both files and directories if supported by FS
    if (fs->rename(path, trashPath.c_str())) {
        return true;
    }

    logError("[TRASH] Move failed for %s", path);
    return false;
}

bool trashBinRestore(FS* fs, const char* trashPath) {
    if (!fs->exists(trashPath)) return false;

    String name = String(trashPath);
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash != -1) {
        name = name.substring(lastSlash + 1);
    }

    // Strip timestamp suffix (.epoch)
    int lastDot = name.lastIndexOf('.');
    if (lastDot == -1) return false;
    String originalName = name.substring(0, lastDot);

    String targetPath = "/" + originalName;

    // Check if target already exists to avoid overwrite
    if (fs->exists(targetPath.c_str())) {
        logWarning("[TRASH] Restore target %s already exists", targetPath.c_str());
        return false; 
    }

    logInfo("[TRASH] Restoring %s -> %s", trashPath, targetPath.c_str());
    if (fs->rename(trashPath, targetPath.c_str())) {
        return true;
    }

    logError("[TRASH] Restore failed for %s", trashPath);
    return false;
}

void trashBinAutoPurge() {
    uint32_t currentEpoch = 0;
    #if BOARD_HAS_RTC_DS3231
        currentEpoch = rtcGetCurrentEpoch();
    #endif

    if (currentEpoch == 0) {
        logWarning("[TRASH] Skipping auto-purge: RTC not available/set");
        return;
    }

    logInfo("[TRASH] Starting auto-purge (Threshold: 30 days ago)...");

    purgeDirectory(&LittleFS, TRASH_DIR, currentEpoch);
    
    if (sdCardIsMounted()) {
        purgeDirectory(&SD, TRASH_DIR, currentEpoch);
    }
    
    logInfo("[TRASH] Auto-purge complete.");
}

static void purgeDirectory(FS* fs, const char* dirPath, uint32_t currentEpoch) {
    File root = fs->open(dirPath);
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();
    while (file) {
        String name = String(file.name());
        // Extract timestamp from end of filename (suffix .epoch)
        int lastDot = name.lastIndexOf('.');
        if (lastDot != -1) {
            String epochStr = name.substring(lastDot + 1);
            uint32_t fileEpoch = (uint32_t)epochStr.toInt();
            
            if (fileEpoch > 0 && (currentEpoch - fileEpoch > PURGE_AGE_SECONDS)) {
                String fullPath = String(dirPath) + "/" + name;
                logInfo("[TRASH] Purging expired item: %s", fullPath.c_str());
                
                bool is_dir = file.isDirectory();
                file.close(); // Close before delete
                
                if (is_dir) fs->rmdir(fullPath.c_str());
                else fs->remove(fullPath.c_str());
                
                // Re-open root to continue iteration safely after modification
                root.close();
                root = fs->open(dirPath);
                // We need to skip already processed files or restart iteration
                // Simplest for LittleFS is to restart find if structure changed
                file = root.openNextFile();
                continue;
            }
        }
        file = root.openNextFile();
    }
    root.close();
}

static void trashBinTask(void* pvParameters) {
    // Wait for system to settle
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    while (1) {
        trashBinAutoPurge();
        // Run purge every 24 hours
        vTaskDelay(pdMS_TO_TICKS(24 * 60 * 60 * 1000));
    }
}

void trashBinStartBackgroundHandler() {
    // Start background task
    xTaskCreate(trashBinTask, "TrashBinTask", 4096, nullptr, 1, nullptr);
}
