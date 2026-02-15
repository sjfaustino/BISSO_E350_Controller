/**
 * @file cli_fs.cpp
 * @brief Filesystem CLI Commands (Diagnostic)
 */

#include "cli.h"
#include <LittleFS.h>
#include <SD.h>
#include "sd_card_manager.h"
#include "serial_logger.h"
#include "psram_web_cache.h"

// Helper for directory stats (-d)
void get_dir_stats(const char* path, size_t& file_count, size_t& dir_count, size_t& total_size) {
    File root = LittleFS.open(path);
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

// Helper for recursive listing (-R)
void ls_recursive(const char* path) {
    logPrintf("\n%s:\n", path);
    File root = LittleFS.open(path);
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            logPrintf("%-10s %-11s %s\n", "[DIR]", "-", file.name());
             // Construct full path for recursion
            char subpath[128];
            if (strcmp(path, "/") == 0) snprintf(subpath, sizeof(subpath), "/%s", file.name());
            else snprintf(subpath, sizeof(subpath), "%s/%s", path, file.name());
            
            // Store current position? No, modify algorithm:
            // Recursion is tricky with openNextFile iterator state on ESP32 LittleFS implementation.
            // Better to list all first, then recurse? 
            // ESP32 LittleFS Dir iteration consumes memory. Simple recursion might fail depth.
            // But let's stick to simple listing for now.
            // Actually, we can't easily recurse *while* iterating unless we close/reopen or store paths.
            // Storing paths is safer.
        } else {
            logPrintf("%-10s %-11zu %s\n", "[FILE]", file.size(), file.name());
        }
        file = root.openNextFile();
    }
    
    // Pass 2: Recurse into directories
    root.rewindDirectory();
    file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subpath[128];
            if (strcmp(path, "/") == 0) snprintf(subpath, sizeof(subpath), "/%s", file.name());
            else snprintf(subpath, sizeof(subpath), "%s/%s", path, file.name());
            ls_recursive(subpath);
        }
        file = root.openNextFile();
    }
}

void cmd_fs_ls(int argc, char** argv) {
    char path_buf[64];
    const char* path = "/";
    bool flag_d = false;
    bool flag_R = false;

    // Argument Parsing
    for (int i = 1; i < argc; i++) {
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
        get_dir_stats(path, f_count, d_count, t_size);
        logPrintf("Directory: %s\n", path);
        logPrintf("  Files: %zu\n", f_count);
        logPrintf("  Dirs:  %zu\n", d_count);
        logPrintf("  Size:  %zu bytes\n", t_size);
        return;
    }

    if (flag_R) {
        ls_recursive(path);
    } else {
        logPrintf("\nListing [LittleFS]: %s\n", path);
        logPrintln("-------------------------------------------------------------");
        logPrintln("Type       Size        Name");
        logPrintln("-------------------------------------------------------------");

        File root = LittleFS.open(path);
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
                logPrintf("%-10s %-11zu %s\n", "[FILE]", file.size(), file.name());
            }
            count++;
            file = root.openNextFile();
        }
        root.close();

        logPrintln("-------------------------------------------------------------");
        logPrintf("Total: %d items\n\n", count);
    }
}

void cmd_fs_df(int argc, char** argv) {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    logPrintf("LittleFS Partition Status:\n");
    logPrintf("  Total: %8zu bytes\n", total);
    
    if (total > 0) {
        logPrintf("  Used:  %8zu bytes (%d%%)\n", used, (int)((used * 100) / total));
        logPrintf("  Free:  %8zu bytes\n", total - used);
    } else {
        logError("Partition not mounted or empty - check filesystem!");
    }
}

void cmd_fs_cat(int argc, char** argv) {
    if (argc < 2) {
        CLI_USAGE("cat", "<filename>");
        return;
    }

    char path_buf[64];
    const char* path = argv[1];
    if (argv[1][0] != '/') {
        snprintf(path_buf, sizeof(path_buf), "/%s", argv[1]);
        path = path_buf;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        logError("Failed to open file: %s", path);
        return;
    }

    logPrintf("--- Reading [LittleFS]: %s (%lu bytes) ---\n", path, (unsigned long)file.size());
    while (file.available()) {
        CLI_SERIAL.write(file.read());
    }
    logPrintf("\n--- END ---\n");
    file.close();
}

void cmd_fs_cache(int argc, char** argv) {
    PsramWebCache::getInstance().dumpCacheInfo();
}

void cmd_fs_dmesg(int argc, char** argv) {
    const char* path = "/var/log/boot.log";
    if (!sdCardIsMounted()) {
        logError("SD card not mounted. Persistent logs unavailable.");
        return;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        logError("Log file not found: %s", path);
        return;
    }

    logPrintf("--- DMESG: %s START ---\n", path);
    while (file.available()) {
        CLI_SERIAL.write(file.read());
    }
    logPrintf("\n--- DMESG: %s END ---\n", path);
    file.close();
}
