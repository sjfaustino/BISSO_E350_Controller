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
    
    logPrintln("\n=== SD Card Status ===");
    
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

// Helper for SD directory stats (-d)
static void sd_get_dir_stats(const char* path, size_t& file_count, size_t& dir_count, size_t& total_size) {
    File root = SD.open(path);
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            dir_count++;
        } else {
            file_count++;
            total_size += file.size();
        }
        file = root.openNextFile();
    }
}

// Helper for SD recursive listing (-R)
static void sd_ls_recursive(const char* path) {
    logPrintf("\n%s:\n", path);
    File root = SD.open(path);
    if (!root || !root.isDirectory()) return;

    // Pass 1: List files and dirs in this directory
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            logPrintf("%-10s %-11s %s\n", "[DIR]", "-", file.name());
        } else {
            logPrintf("%-10s %-11lu %s\n", "[FILE]", (unsigned long)file.size(), file.name());
        }
        file = root.openNextFile();
    }

    // Pass 2: Recurse into subdirectories
    root.rewindDirectory();
    file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subpath[128];
            if (strcmp(path, "/") == 0) snprintf(subpath, sizeof(subpath), "/%s", file.name());
            else snprintf(subpath, sizeof(subpath), "%s/%s", path, file.name());
            sd_ls_recursive(subpath);
        }
        file = root.openNextFile();
    }
}

void cmd_sd_ls(int argc, char** argv) {
    if (!sdCardIsMounted()) {
        logError("SD card not mounted");
        return;
    }

    const char* path = "/";
    char path_buf[64];
    bool flag_d = false;
    bool flag_R = false;

    // Argument parsing (skip argv[0]="sd" and argv[1]="ls")
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j] != '\0'; j++) {
                if (argv[i][j] == 'd') flag_d = true;
                if (argv[i][j] == 'R') flag_R = true;
            }
        } else {
            // Path argument
            if (argv[i][0] != '/') {
                snprintf(path_buf, sizeof(path_buf), "/%s", argv[i]);
                path = path_buf;
            } else {
                path = argv[i];
            }
        }
    }

    if (flag_d) {
        size_t f_count = 0, d_count = 0, t_size = 0;
        sd_get_dir_stats(path, f_count, d_count, t_size);
        logPrintf("Directory: %s\n", path);
        logPrintf("  Files: %zu\n", f_count);
        logPrintf("  Dirs:  %zu\n", d_count);
        logPrintf("  Size:  %zu bytes\n", t_size);
        return;
    }

    if (flag_R) {
        sd_ls_recursive(path);
    } else {
        logPrintf("\nListing [SD]: %s\n"
                  "-------------------------------------------------------------\n"
                  "Type       Size        Name\n"
                  "-------------------------------------------------------------\n", path);

        File root = SD.open(path);
        if (!root) {
            logError("Failed to open directory (check path?)");
            return;
        }
        if (!root.isDirectory()) {
            logError("Not a directory");
            root.close();
            return;
        }

        int count = 0;
        File file = root.openNextFile();
        while (file) {
            if (file.isDirectory()) {
                logPrintf("%-10s %-11s %s\n", "[DIR]", "-", file.name());
            } else {
                logPrintf("%-10s %-11lu %s\n", "[FILE]", (unsigned long)file.size(), file.name());
            }
            count++;
            file = root.openNextFile();
        }
        root.close();

        logPrintf("-------------------------------------------------------------\n"
                  "Total: %d items\n\n", count);
    }
}

// =============================================================================
// DISPLAY FILE CONTENTS
// =============================================================================

void cmd_sd_cat(int argc, char** argv) {
    if (argc < 3) {
        logError("Usage: sd cat <filename>");
        return;
    }
    
    if (!sdCardIsMounted()) {
        logError("SD card not mounted");
        return;
    }
    
    const char* filename = argv[2];
    
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        logError("Failed to open: %s", filename);
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

// Recursive helper for rm -r
static bool rm_sd_recurse(const char* path, bool dry_run, int& del_files, int& del_dirs) {
    File root = SD.open(path);
    if (!root || !root.isDirectory()) return false;

    File f = root.openNextFile();
    while (f) {
        char subpath[128];
        if (strcmp(path, "/") == 0) snprintf(subpath, sizeof(subpath), "/%s", f.name());
        else snprintf(subpath, sizeof(subpath), "%s/%s", path, f.name());

        if (f.isDirectory()) {
            rm_sd_recurse(subpath, dry_run, del_files, del_dirs);
            if (dry_run) {
                logPrintf("  rmdir %s\n", subpath);
            } else {
                SD.rmdir(subpath);
            }
            del_dirs++;
        } else {
            if (dry_run) {
                logPrintf("  rm    %s (%lu bytes)\n", subpath, (unsigned long)f.size());
            } else {
                SD.remove(subpath);
            }
            del_files++;
        }
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to prevent watchdog timeout
        f = root.openNextFile();
    }
    return true;
}

void cmd_sd_rm(int argc, char** argv) {
    if (!sdCardIsMounted()) {
        logError("SD card not mounted");
        return;
    }

    bool recursive = false;
    bool dry_run = false;
    const char* path = nullptr;
    char path_buf[64];

    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j] != '\0'; j++) {
                if (argv[i][j] == 'r') recursive = true;
                if (argv[i][j] == 'i') dry_run = true;
            }
        } else {
            if (argv[i][0] != '/') {
                snprintf(path_buf, sizeof(path_buf), "/%s", argv[i]);
                path = path_buf;
            } else {
                path = argv[i];
            }
        }
    }

    if (!path) {
        logError("Usage: sd rm [-r] [-i] <path>");
        return;
    }

    if (!SD.exists(path)) {
        logError("Not found: %s", path);
        return;
    }

    File file = SD.open(path);
    if (!file) {
        logError("Cannot open: %s", path);
        return;
    }
    bool isDir = file.isDirectory();
    file.close();

    if (isDir && !recursive) {
        logError("'%s' is a directory — use -r to delete recursively", path);
        return;
    }

    if (isDir) {
        int del_files = 0, del_dirs = 0;
        if (dry_run) {
            logPrintf("Dry run — would delete:\n");
            rm_sd_recurse(path, true, del_files, del_dirs);
            logPrintf("  rmdir %s\n", path);
            del_dirs++;
            logPrintf("Total: %d files, %d directories\n", del_files, del_dirs);
            logPrintf("Run without -i to execute\n");
        } else {
            rm_sd_recurse(path, false, del_files, del_dirs);
            SD.rmdir(path);
            del_dirs++;
            logPrintf("Deleted: %d files, %d directories\n", del_files, del_dirs);
        }
    } else {
        if (dry_run) {
            logPrintf("Dry run — would delete: %s\n", path);
            logPrintf("Run without -i to execute\n");
        } else {
            if (sdCardDeleteFile(path)) {
                logPrintf("Deleted: %s\n", path);
            }
        }
    }
}


// =============================================================================
// CREATE DIRECTORY
// =============================================================================

void cmd_sd_mkdir(int argc, char** argv) {
    if (argc < 3) {
        logError("Usage: sd mkdir <directory>");
        return;
    }
    
    if (!sdCardIsMounted()) {
        logError("SD card not mounted");
        return;
    }
    
    const char* dirname = argv[2];
    
    if (sdCardCreateDir(dirname)) {
        logPrintf("Directory created: %s\n", dirname);
    }
}

// =============================================================================
// EJECT/UNMOUNT
// =============================================================================

void cmd_sd_eject(int argc, char** argv) {
    (void)argc; (void)argv;
    
    if (!sdCardIsMounted()) {
        logError("SD card not mounted");
        return;
    }
    
    sdCardUnmount();
    logPrintf("SD card safely unmounted\n");
    logPrintf("You can now remove the card\n");
}

// =============================================================================
// HEALTH CHECK
// =============================================================================

void cmd_sd_health(int argc, char** argv) {
    (void)argc; (void)argv;
    
    if (!sdCardIsMounted()) {
        logError("SD card not mounted");
        return;
    }
    
    logPrintf("Performing health check...\n");
    SDCardHealth result = sdCardHealthCheck();
    
    if (result == SD_HEALTH_OK) {
        logPrintf("Health check PASSED\n");
    } else {
        logError("Health check FAILED: %s", sdCardHealthString(result));
    }
}

// =============================================================================
// FORMAT SD CARD
// =============================================================================

void cmd_sd_format(int argc, char** argv) {
    if (!sdCardIsMounted()) {
        logError("SD card not mounted");
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
        logPrintf("*** WARNING: This will DELETE ALL DATA on the SD card! ***\n");
        logPrintf("To confirm, run: sd format -y\n");
        return;
    }
    
    // Perform format
    if (sdCardFormat()) {
        logPrintf("Format complete\n");
    } else {
        logError("Format failed");
    }
}

// =============================================================================
// TREE COMMAND (SD)
// =============================================================================

static void tree_sd_recurse(const char* path, const char* prefix, int& dirs, int& files, bool dirs_only, bool show_all, bool show_sizes) {
    File root = SD.open(path);
    if (!root || !root.isDirectory()) return;

    // Count visible entries first to know which is last
    int count = 0;
    File f = root.openNextFile();
    while (f) {
        bool hidden = (f.name()[0] == '.');
        if ((show_all || !hidden) && (!dirs_only || f.isDirectory())) count++;
        f = root.openNextFile();
    }

    // Iterate again with proper connectors
    root.rewindDirectory();
    f = root.openNextFile();
    int idx = 0;
    while (f) {
        bool hidden = (f.name()[0] == '.');
        if ((!show_all && hidden) || (dirs_only && !f.isDirectory())) { f = root.openNextFile(); continue; }
        idx++;
        bool last = (idx == count);
        const char* connector = last ? "`-- " : "|-- ";
        const char* child_prefix = last ? "    " : "|   ";

        if (f.isDirectory()) {
            dirs++;
            logPrintf("%s%s%s\n", prefix, connector, f.name());
            char subpath[128];
            if (strcmp(path, "/") == 0) snprintf(subpath, sizeof(subpath), "/%s", f.name());
            else snprintf(subpath, sizeof(subpath), "%s/%s", path, f.name());
            char new_prefix[128];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, child_prefix);
            tree_sd_recurse(subpath, new_prefix, dirs, files, dirs_only, show_all, show_sizes);
        } else {
            files++;
            if (show_sizes) logPrintf("%s%s%s (%lu)\n", prefix, connector, f.name(), (unsigned long)f.size());
            else logPrintf("%s%s%s\n", prefix, connector, f.name());
        }
        f = root.openNextFile();
    }
}

void cmd_sd_tree(int argc, char** argv) {
    if (!sdCardIsMounted()) {
        logError("SD card not mounted");
        return;
    }

    const char* path = "/";
    char path_buf[64];
    bool dirs_only = false;
    bool show_all = false;
    bool show_sizes = false;
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j] != '\0'; j++) {
                if (argv[i][j] == 'd') dirs_only = true;
                if (argv[i][j] == 'a') show_all = true;
                if (argv[i][j] == 's') show_sizes = true;
            }
        } else {
            if (argv[i][0] != '/') {
                snprintf(path_buf, sizeof(path_buf), "/%s", argv[i]);
                path = path_buf;
            } else {
                path = argv[i];
            }
        }
    }

    logPrintf("%s\n", path);
    int dirs = 0, files = 0;
    tree_sd_recurse(path, "", dirs, files, dirs_only, show_all, show_sizes);
    if (dirs_only) logPrintf("\n%d directories\n", dirs);
    else logPrintf("\n%d directories, %d files\n", dirs, files);
}

// =============================================================================
// MAIN COMMAND DISPATCHER
// =============================================================================

void cmd_sd_main(int argc, char** argv) {
    // Table-driven subcommand dispatch
    static const cli_subcommand_t subcmds[] = {
        {"status",  cmd_sd_status,  "Show SD card status"},
        {"ls",      cmd_sd_ls,      "List directory [-R recursive] [-d stats]"},
        {"cat",     cmd_sd_cat,     "Display file contents"},
        {"rm",      cmd_sd_rm,      "Delete file/dir [-r recursive] [-i dry-run]"},
        {"mkdir",   cmd_sd_mkdir,   "Create directory"},
        {"eject",   cmd_sd_eject,   "Safely unmount SD card"},
        {"health",  cmd_sd_health,  "Run health check"},
        {"format",  cmd_sd_format,  "Format SD card (delete all)"},
        {"tree",    cmd_sd_tree,    "Directory tree [-d dirs] [-a all] [-s sizes]"}
    };

    
    cliDispatchSubcommand("", argc, argv, subcmds, 
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}

void cliRegisterSDCommands() {
    cliRegisterCommand("sd", "SD card management", cmd_sd_main);
}

