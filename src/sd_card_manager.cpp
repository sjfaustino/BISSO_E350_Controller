#include "sd_card_manager.h"
#include "serial_logger.h"
#include "board_variant.h"
#include <SD.h>
#include <SPI.h>

// SD Card state
static bool sd_initialized = false;
static bool sd_mounted = false;
static SDCardHealth last_health = SD_HEALTH_NOT_MOUNTED;
static SPIClass sd_spi(HSPI);

/**
 * @brief Initialize SD card with custom SPI pins
 */
result_t sdCardInit() {
    // Only initialize if board has SD card support
    #if !BOARD_HAS_SDCARD
        logWarning("[SD] SD card not supported on this board variant");
        return RESULT_ERROR_HARDWARE;
    #endif
    
    if (sd_initialized) {
        logDebug("[SD] Already initialized");
        return sd_mounted ? RESULT_OK : RESULT_ERROR;
    }
    
    logInfo("[SD] Initializing SD card...");
    
    // Check card detect pin first
    pinMode(PIN_SD_CD, INPUT_PULLUP);
    delay(10);
    
    if (!sdCardIsPresent()) {
        logInfo("[SD] No card detected (card detect pin is HIGH)");
        sd_initialized = true;
        sd_mounted = false;
        return RESULT_ERROR;
    }
    
    logInfo("[SD] Card detected, initializing SPI...");
    
    // Initialize custom SPI for SD card
    sd_spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    
    // Try to mount SD card
    if (!SD.begin(PIN_SD_CS, sd_spi, 4000000)) {  // 4MHz SPI speed
        logError("[SD] Failed to mount SD card");
        sd_initialized = true;
        sd_mounted = false;
        return RESULT_ERROR_HARDWARE;
    }
    
    // Get card type
    uint8_t cardType = SD.cardType();
    const char* typeStr = "UNKNOWN";
    
    switch (cardType) {
        case CARD_NONE:
            typeStr = "NONE";
            logError("[SD] No SD card attached");
            sd_mounted = false;
            break;
        case CARD_MMC:
            typeStr = "MMC";
            sd_mounted = true;
            break;
        case CARD_SD:
            typeStr = "SDSC";
            sd_mounted = true;
            break;
        case CARD_SDHC:
            typeStr = "SDHC/SDXC";
            sd_mounted = true;
            break;
        default:
            typeStr = "UNKNOWN";
            sd_mounted = false;
            break;
    }
    
    if (sd_mounted) {
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);  // MB
        uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);  // MB
        uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);  // MB
        
        logInfo("[SD] Card Type: %s", typeStr);
        logInfo("[SD] Card Size: %llu MB", cardSize);
        logInfo("[SD] Total Space: %llu MB", totalBytes);
        logInfo("[SD] Used Space: %llu MB", usedBytes);
        logInfo("[SD] [OK] SD card mounted successfully");
        
        // Create default directories if they don't exist
        SD.mkdir("/gcode");
        SD.mkdir("/logs");
        SD.mkdir("/backups");
        SD.mkdir("/jobs");
        
        // Perform health check to detect card issues early
        last_health = sdCardHealthCheck();
        if (last_health != SD_HEALTH_OK) {
            logWarning("[SD] Health check FAILED: %s", sdCardHealthString(last_health));
            // Continue anyway - card may still be usable for reads
        }
    }
    
    sd_initialized = true;
    return sd_mounted ? RESULT_OK : RESULT_ERROR;
}


/**
 * @brief Check if SD card is physically present (using card detect pin)
 */
bool sdCardIsPresent() {
    #if !BOARD_HAS_SDCARD
        return false;
    #endif
    
    // Card detect pin is LOW when card is inserted
    return digitalRead(PIN_SD_CD) == LOW;
}

/**
 * @brief Check if SD card is mounted and ready
 */
bool sdCardIsMounted() {
    return sd_mounted;
}

/**
 * @brief Safely unmount SD card
 */
void sdCardUnmount() {
    if (sd_mounted) {
        SD.end();
        sd_mounted = false;
        logInfo("[SD] SD card unmounted");
    }
}

/**
 * @brief Get SD card information
 */
result_t sdCardGetInfo(SDCardInfo* info) {
    if (!info) return RESULT_INVALID_PARAM;
    if (!sd_mounted) return RESULT_NOT_READY;
    
    info->totalBytes = SD.totalBytes();
    info->usedBytes = SD.usedBytes();
    info->freeBytes = info->totalBytes - info->usedBytes;
    info->cardType = SD.cardType();
    
    switch (info->cardType) {
        case CARD_NONE: info->cardTypeName = "NONE"; break;
        case CARD_MMC: info->cardTypeName = "MMC"; break;
        case CARD_SD: info->cardTypeName = "SDSC"; break;
        case CARD_SDHC: info->cardTypeName = "SDHC/SDXC"; break;
        default: info->cardTypeName = "UNKNOWN"; break;
    }
    
    return RESULT_OK;
}

/**
 * @brief Check if file exists on SD card
 */
bool sdCardFileExists(const char* path) {
    if (!sd_mounted) return false;
    return SD.exists(path);
}

/**
 * @brief Get file size
 */
size_t sdCardGetFileSize(const char* path) {
    if (!sd_mounted || !path) return 0;
    
    File file = SD.open(path, FILE_READ);
    if (!file) return 0;
    
    size_t size = file.size();
    file.close();
    return size;
}

/**
 * @brief Delete file
 */
result_t sdCardDeleteFile(const char* path) {
    if (!path) return RESULT_INVALID_PARAM;
    if (!sd_mounted) return RESULT_NOT_READY;
    
    if (!SD.exists(path)) {
        logError("[SD] File not found: %s", path);
        return RESULT_ERROR;
    }
    
    if (SD.remove(path)) {
        logInfo("[SD] Deleted: %s", path);
        return RESULT_OK;
    }
    
    logError("[SD] Failed to delete: %s", path);
    return RESULT_ERROR;
}

/**
 * @brief Create directory
 */
result_t sdCardCreateDir(const char* path) {
    if (!path) return RESULT_INVALID_PARAM;
    if (!sd_mounted) return RESULT_NOT_READY;
    
    if (SD.exists(path)) {
        logDebug("[SD] Directory already exists: %s", path);
        return RESULT_OK;
    }
    
    if (SD.mkdir(path)) {
        logInfo("[SD] Created directory: %s", path);
        return RESULT_OK;
    }
    
    logError("[SD] Failed to create directory: %s", path);
    return RESULT_ERROR;
}

/**
 * @brief List directory contents
 */
bool sdCardListDir(const char* path) {
    if (!sd_mounted || !path) return false;
    
    File root = SD.open(path);
    if (!root) {
        logError("[SD] Failed to open directory: %s", path);
        return false;
    }
    
    if (!root.isDirectory()) {
        logError("[SD] Not a directory: %s", path);
        root.close();
        return false;
    }
    
    logPrintln("");
    logPrintf("[SD] Contents of: %s\n", path);
    logPrintln("-------------------------------------------------------------");
    logPrintln("Type       Size        Name");
    logPrintln("-------------------------------------------------------------");
    
    File file = root.openNextFile();
    int count = 0;
    
    while (file) {
        const char* type = file.isDirectory() ? "[DIR]" : "[FILE]";
        
        if (file.isDirectory()) {
            logPrintf("%-10s %-11s %s\n", type, "-", file.name());
        } else {
            logPrintf("%-10s %-11lu %s\n", type, (unsigned long)file.size(), file.name());
        }
        
        count++;
        file = root.openNextFile();
    }
    
    logPrintln("-------------------------------------------------------------");
    logPrintf("Total: %d items\n", count);
    logPrintln("");
    
    root.close();
    return true;
}

/**
 * @brief Get status string for CLI
 */
const char* sdCardGetStatusString() {
    if (!sd_initialized) return "Not initialized";
    if (!sdCardIsPresent()) return "No card detected";
    if (!sd_mounted) return "Card present but not mounted";
    return "Mounted and ready";
}

/**
 * @brief Perform quick health check on SD card
 * 
 * Creates a test file, writes test pattern, reads back and verifies,
 * then deletes the file. Catches common issues:
 * - Write-protected cards
 * - Failing/worn-out sectors
 * - FAT corruption affecting file operations
 * 
 * Takes approximately 50-150ms depending on card speed.
 */
SDCardHealth sdCardHealthCheck() {
    if (!sd_mounted) {
        last_health = SD_HEALTH_NOT_MOUNTED;
        return last_health;
    }
    
    const char* testPath = "/.sd_health_check";
    
    // Test pattern with varying bytes to catch stuck bits
    const uint8_t testPattern[] = {
        0x55, 0xAA, 0x00, 0xFF,  // Alternating bit patterns
        0x12, 0x34, 0x56, 0x78,  // Sequential values
        0xDE, 0xAD, 0xBE, 0xEF,  // Magic pattern
        0x42, 0x49, 0x53, 0x53   // "BISS"
    };
    const size_t patternSize = sizeof(testPattern);
    
    // Step 1: Try to write test file
    File writeFile = SD.open(testPath, FILE_WRITE);
    if (!writeFile) {
        logDebug("[SD] Health: Failed to create test file");
        return SD_HEALTH_WRITE_FAILED;
    }
    
    size_t written = writeFile.write(testPattern, patternSize);
    writeFile.close();
    
    if (written != patternSize) {
        logDebug("[SD] Health: Write size mismatch (%u != %u)", written, patternSize);
        SD.remove(testPath);  // Clean up
        return SD_HEALTH_WRITE_FAILED;
    }
    
    // Step 2: Read back and verify
    File readFile = SD.open(testPath, FILE_READ);
    if (!readFile) {
        logDebug("[SD] Health: Failed to open test file for read");
        SD.remove(testPath);  // Clean up
        return SD_HEALTH_READ_FAILED;
    }
    
    uint8_t readBuffer[sizeof(testPattern)];
    size_t bytesRead = readFile.read(readBuffer, patternSize);
    readFile.close();
    
    if (bytesRead != patternSize) {
        logDebug("[SD] Health: Read size mismatch (%u != %u)", bytesRead, patternSize);
        SD.remove(testPath);  // Clean up
        return SD_HEALTH_READ_FAILED;
    }
    
    // Step 3: Verify data integrity
    if (memcmp(testPattern, readBuffer, patternSize) != 0) {
        logDebug("[SD] Health: Data verification failed");
        SD.remove(testPath);  // Clean up
        return SD_HEALTH_VERIFY_FAILED;
    }
    
    // Step 4: Delete test file
    if (!SD.remove(testPath)) {
        logDebug("[SD] Health: Failed to delete test file");
        return SD_HEALTH_DELETE_FAILED;
    }
    
    logDebug("[SD] Health check passed");
    last_health = SD_HEALTH_OK;
    return last_health;
}

/**
 * @brief Get the last health check result
 */
SDCardHealth sdCardGetLastHealth() {
    return last_health;
}

/**
 * @brief Get human-readable health check result string
 */
const char* sdCardHealthString(SDCardHealth result) {
    switch (result) {
        case SD_HEALTH_OK:            return "OK";
        case SD_HEALTH_READ_ONLY:     return "Write-protected";
        case SD_HEALTH_WRITE_FAILED:  return "Write failed";
        case SD_HEALTH_READ_FAILED:   return "Read failed";
        case SD_HEALTH_VERIFY_FAILED: return "Data verification failed (corruption)";
        case SD_HEALTH_DELETE_FAILED: return "Delete failed";
        case SD_HEALTH_NOT_MOUNTED:   return "Card not mounted";
        default:                      return "Unknown error";
    }
}

/**
 * @brief Recursively delete all files and subdirectories in a directory
 */
static bool deleteRecursive(const char* path) {
    File dir = SD.open(path);
    if (!dir) {
        return false;
    }
    
    if (!dir.isDirectory()) {
        dir.close();
        return SD.remove(path);
    }
    
    // Iterate through all entries
    File entry;
    while ((entry = dir.openNextFile())) {
        String entryPath = String(path);
        if (!entryPath.endsWith("/")) entryPath += "/";
        entryPath += entry.name();
        
        if (entry.isDirectory()) {
            entry.close();
            // Recursively delete subdirectory
            if (!deleteRecursive(entryPath.c_str())) {
                dir.close();
                return false;
            }
        } else {
            entry.close();
            // Delete file
            if (!SD.remove(entryPath.c_str())) {
                logError("[SD] Failed to delete: %s", entryPath.c_str());
                dir.close();
                return false;
            }
        }
    }
    
    dir.close();
    
    // Now remove the empty directory (unless it's root)
    if (strcmp(path, "/") != 0) {
        return SD.rmdir(path);
    }
    
    return true;
}

/**
 * @brief Format/wipe SD card (delete all files and directories)
 * 
 * Recursively deletes all files and directories on the SD card,
 * then recreates the default directory structure.
 */
result_t sdCardFormat() {
    if (!sd_mounted) {
        logError("[SD] SD card not mounted");
        return RESULT_NOT_READY;
    }
    
    logWarning("[SD] Formatting SD card (deleting all data)...");
    
    // Delete all contents of root directory
    File root = SD.open("/");
    if (!root) {
        logError("[SD] Failed to open root directory");
        return RESULT_ERROR;
    }
    
    // Collect all root-level items first (can't delete while iterating safely)
    String items[64];  // Max 64 root items
    int itemCount = 0;
    
    File entry;
    while ((entry = root.openNextFile()) && itemCount < 64) {
        items[itemCount] = String("/") + entry.name();
        itemCount++;
        entry.close();
    }
    root.close();
    
    // Delete each item
    int deletedCount = 0;
    for (int i = 0; i < itemCount; i++) {
        logDebug("[SD] Deleting: %s", items[i].c_str());
        if (deleteRecursive(items[i].c_str())) {
            deletedCount++;
        } else {
            logError("[SD] Failed to delete: %s", items[i].c_str());
        }
    }
    
    logInfo("[SD] Deleted %d/%d items", deletedCount, itemCount);
    
    // Recreate default directories
    SD.mkdir("/gcode");
    SD.mkdir("/logs");
    SD.mkdir("/backups");
    SD.mkdir("/jobs");
    
    logInfo("[SD] [OK] SD card formatted successfully");
    logInfo("[SD] Default directories recreated: /gcode, /logs, /backups, /jobs");
    
    return RESULT_OK;
}
