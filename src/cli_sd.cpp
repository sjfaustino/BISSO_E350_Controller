#include "cli.h"
#include "serial_logger.h"
#include "sd_card_manager.h"
#include "watchdog_manager.h"
#include <SD.h>

// =============================================================================
// SD CARD STATUS
// =============================================================================

void cmd_sd_status(int argc, char** argv) {
    (void)argc; (void)argv;
    
    logPrintln("\n[SD] === SD Card Status ===");
    
   logPrintf("  Detected:    %s\n", sdCardIsPresent() ? "YES" : "NO");
    logPrintf("  Status:      %s\n", sdCardGetStatusString());
    
    if (sdCardIsMounted()) {
        SDCardInfo info;
        if (sdCardGetInfo(&info)) {
            uint64_t totalMB = info.totalBytes / (1024 * 1024);
            uint64_t usedMB = info.usedBytes / (1024 * 1024);
            uint64_t freeMB = info.freeBytes / (1024 * 1024);
            int usedPercent = (int)((info.usedBytes * 100) / info.totalBytes);
            
            logPrintf("  Type:        %s\n", info.cardTypeName);
            logPrintf("  Capacity:    %llu MB\n", totalMB);
            logPrintf("  Used:        %llu MB (%d%%)\n", usedMB, usedPercent);
            logPrintf("  Free:        %llu MB\n", freeMB);
        }
    }
    
    logPrintln("");
}

// =============================================================================
// LIST DIRECTORY
// =============================================================================

void cmd_sd_ls(int argc, char** argv) {
    if (!sdCardIsMounted()) {
        logError("[SD] SD card not mounted");
        return;
    }
    
    const char* path = (argc >= 3) ? argv[2] : "/";
    sdCardListDir(path);
}

// =============================================================================
// DISPLAY FILE CONTENTS
// =============================================================================

void cmd_sd_cat(int argc, char** argv) {
    if (argc < 3) {
        logError("[SD] Usage: sd cat <filename>");
        return;
    }
    
    if (!sdCardIsMounted()) {
        logError("[SD] SD card not mounted");
        return;
    }
    
    const char* filename = argv[2];
    
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        logError("[SD] Failed to open: %s", filename);
        return;
    }
    
    logPrintf("--- Reading [SD]: %s (%lu bytes) ---\n", filename, (unsigned long)file.size());
    
    while (file.available()) {
        char c = file.read();
        Serial.write(c);
        watchdogFeed("CLI");
    }
    
    logPrintf("\n--- END ---\n");
    file.close();
}

// =============================================================================
// DELETE FILE
// =============================================================================

void cmd_sd_rm(int argc, char** argv) {
    if (argc < 3) {
        logError("[SD] Usage: sd rm <filename>");
        return;
    }
    
    if (!sdCardIsMounted()) {
        logError("[SD] SD card not mounted");
        return;
    }
    
    const char* path = argv[2];
    
    // Check if path exists
    if (!SD.exists(path)) {
        logError("[SD] Not found: %s", path);
        return;
    }
    
    // Check if it's a directory
    File file = SD.open(path);
    if (!file) {
        logError("[SD] Cannot open: %s", path);
        return;
    }
    
    bool isDir = file.isDirectory();
    file.close();
    
    if (isDir) {
        logError("[SD] '%s' is a directory - use 'sd rmdir' instead", path);
        return;
    }
    
    // Delete file
    if (sdCardDeleteFile(path)) {
        logInfo("[SD] [OK] File deleted: %s", path);
    }
}

// =============================================================================
// DELETE DIRECTORY
// =============================================================================

void cmd_sd_rmdir(int argc, char** argv) {
    if (argc < 3) {
        logError("[SD] Usage: sd rmdir <directory>");
        return;
    }
    
    if (!sdCardIsMounted()) {
        logError("[SD] SD card not mounted");
        return;
    }
    
    const char* path = argv[2];
    
    // Check if path exists
    if (!SD.exists(path)) {
        logError("[SD] Not found: %s", path);
        return;
    }
    
    // Check if it's actually a directory
    File file = SD.open(path);
    if (!file) {
        logError("[SD] Cannot open: %s", path);
        return;
    }
    
    if (!file.isDirectory()) {
        file.close();
        logError("[SD] '%s' is a file - use 'sd rm' instead", path);
        return;
    }
    file.close();
    
    // Try to remove directory
    if (SD.rmdir(path)) {
        logInfo("[SD] [OK] Directory deleted: %s", path);
    } else {
        logError("[SD] Failed to delete directory (may not be empty): %s", path);
        logInfo("[SD] TIP: Delete all files inside first");
    }
}


// =============================================================================
// CREATE DIRECTORY
// =============================================================================

void cmd_sd_mkdir(int argc, char** argv) {
    if (argc < 3) {
        logError("[SD] Usage: sd mkdir <directory>");
        return;
    }
    
    if (!sdCardIsMounted()) {
        logError("[SD] SD card not mounted");
        return;
    }
    
    const char* dirname = argv[2];
    
    if (sdCardCreateDir(dirname)) {
        logInfo("[SD] [OK] Directory created: %s", dirname);
    }
}

// =============================================================================
// EJECT/UNMOUNT
// =============================================================================

void cmd_sd_eject(int argc, char** argv) {
    (void)argc; (void)argv;
    
    if (!sdCardIsMounted()) {
        logError("[SD] SD card not mounted");
        return;
    }
    
    sdCardUnmount();
    logInfo("[SD] [OK] SD card safely unmounted");
    logInfo("[SD] You can now remove the card");
}

// =============================================================================
// HEALTH CHECK
// =============================================================================

void cmd_sd_health(int argc, char** argv) {
    (void)argc; (void)argv;
    
    if (!sdCardIsMounted()) {
        logError("[SD] SD card not mounted");
        return;
    }
    
    logInfo("[SD] Performing health check...");
    SDCardHealth result = sdCardHealthCheck();
    
    if (result == SD_HEALTH_OK) {
        logInfo("[SD] [OK] Health check PASSED");
    } else {
        logError("[SD] Health check FAILED: %s", sdCardHealthString(result));
    }
}

// =============================================================================
// FORMAT SD CARD
// =============================================================================

void cmd_sd_format(int argc, char** argv) {
    if (!sdCardIsMounted()) {
        logError("[SD] SD card not mounted");
        return;
    }
    
    // Check for -y flag (skip confirmation)
    bool skipConfirm = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--yes") == 0) {
            skipConfirm = true;
            break;
        }
    }
    
    if (!skipConfirm) {
        // Interactive confirmation
        logWarning("[SD] *** WARNING: This will DELETE ALL DATA on the SD card! ***");
        logWarning("[SD] To confirm, run: sd format -y");
        return;
    }
    
    // Perform format
    if (sdCardFormat()) {
        logInfo("[SD] [OK] Format complete");
    } else {
        logError("[SD] Format failed");
    }
}

// =============================================================================
// MAIN COMMAND DISPATCHER
// =============================================================================

void cmd_sd_main(int argc, char** argv) {
    // Table-driven subcommand dispatch
    static const cli_subcommand_t subcmds[] = {
        {"status",  cmd_sd_status,  "Show SD card status"},
        {"ls",      cmd_sd_ls,      "List directory contents"},
        {"cat",     cmd_sd_cat,     "Display file contents"},
        {"rm",      cmd_sd_rm,      "Delete file"},
        {"rmdir",   cmd_sd_rmdir,   "Delete directory"},
        {"mkdir",   cmd_sd_mkdir,   "Create directory"},
        {"eject",   cmd_sd_eject,   "Safely unmount SD card"},
        {"health",  cmd_sd_health,  "Run health check"},
        {"format",  cmd_sd_format,  "Format SD card (delete all)"}
    };

    
    cliDispatchSubcommand("[SD]", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

void cliRegisterSDCommands() {
    cliRegisterCommand("sd", "SD card management", cmd_sd_main);
}

