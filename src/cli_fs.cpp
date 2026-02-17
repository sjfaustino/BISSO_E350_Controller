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

    // Argument Parsing (skip argv[0]="fs" and argv[1]="ls")
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
        logPrintf("\nListing [LittleFS]: %s\n"
                  "-------------------------------------------------------------\n"
                  "Type       Size        Name\n"
                  "-------------------------------------------------------------\n", path);

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

        logPrintf("-------------------------------------------------------------\n"
                  "Total: %d items\n\n", count);
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
    if (argc < 3) {
        logError("Usage: fs cat <filename>");
        return;
    }

    char path_buf[64];
    const char* path = argv[2];
    if (argv[2][0] != '/') {
        snprintf(path_buf, sizeof(path_buf), "/%s", argv[2]);
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

// =============================================================================
// TREE COMMAND (LittleFS)
// =============================================================================

static void tree_lfs_recurse(const char* path, const char* prefix, int& dirs, int& files, bool dirs_only, bool show_all, bool show_sizes) {
    File root = LittleFS.open(path);
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
            tree_lfs_recurse(subpath, new_prefix, dirs, files, dirs_only, show_all, show_sizes);
        } else {
            files++;
            if (show_sizes) logPrintf("%s%s%s (%lu)\n", prefix, connector, f.name(), (unsigned long)f.size());
            else logPrintf("%s%s%s\n", prefix, connector, f.name());
        }
        f = root.openNextFile();
    }
}

void cmd_fs_tree(int argc, char** argv) {
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
    tree_lfs_recurse(path, "", dirs, files, dirs_only, show_all, show_sizes);
    if (dirs_only) logPrintf("\n%d directories\n", dirs);
    else logPrintf("\n%d directories, %d files\n", dirs, files);
}

// =============================================================================
// RM COMMAND (LittleFS)
// =============================================================================

static bool rm_lfs_recurse(const char* path, bool dry_run, int& del_files, int& del_dirs) {
    File root = LittleFS.open(path);
    if (!root || !root.isDirectory()) return false;

    File f = root.openNextFile();
    while (f) {
        char subpath[128];
        if (strcmp(path, "/") == 0) snprintf(subpath, sizeof(subpath), "/%s", f.name());
        else snprintf(subpath, sizeof(subpath), "%s/%s", path, f.name());

        if (f.isDirectory()) {
            rm_lfs_recurse(subpath, dry_run, del_files, del_dirs);
            if (dry_run) {
                logPrintf("  rmdir %s\n", subpath);
            } else {
                LittleFS.rmdir(subpath);
            }
            del_dirs++;
        } else {
            if (dry_run) {
                logPrintf("  rm    %s (%lu bytes)\n", subpath, (unsigned long)f.size());
            } else {
                LittleFS.remove(subpath);
            }
            del_files++;
        }
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to prevent watchdog timeout
        f = root.openNextFile();
    }
    return true;
}

void cmd_fs_rm(int argc, char** argv) {
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
        logError("Usage: fs rm [-r] [-i] <path>");
        return;
    }

    if (!LittleFS.exists(path)) {
        logError("Not found: %s", path);
        return;
    }

    File file = LittleFS.open(path);
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
            rm_lfs_recurse(path, true, del_files, del_dirs);
            logPrintf("  rmdir %s\n", path);
            del_dirs++;
            logPrintf("Total: %d files, %d directories\n", del_files, del_dirs);
            logPrintf("Run without -i to execute\n");
        } else {
            rm_lfs_recurse(path, false, del_files, del_dirs);
            LittleFS.rmdir(path);
            del_dirs++;
            logPrintf("Deleted: %d files, %d directories\n", del_files, del_dirs);
        }
    } else {
        if (dry_run) {
            logPrintf("Dry run — would delete: %s\n", path);
            logPrintf("Run without -i to execute\n");
        } else {
            if (LittleFS.remove(path)) {
                logPrintf("Deleted: %s\n", path);
            } else {
                logError("Failed to delete: %s", path);
            }
        }
    }
}

// =============================================================================
// MAIN COMMAND DISPATCHER
// =============================================================================

void cmd_fs_main(int argc, char** argv) {
    static const cli_subcommand_t subcmds[] = {
        {"ls",      cmd_fs_ls,      "List directory [-R recursive] [-d stats]"},
        {"df",      cmd_fs_df,      "Show partition status"},
        {"cat",     cmd_fs_cat,     "Display file contents"},
        {"rm",      cmd_fs_rm,      "Delete file/dir [-r recursive] [-i dry-run]"},
        {"tree",    cmd_fs_tree,    "Directory tree [-d dirs] [-a all] [-s sizes]"},
        {"cache",   cmd_fs_cache,   "Show web cache info"},
        {"dmesg",   cmd_fs_dmesg,   "View boot log (from SD)"}
    };

    cliDispatchSubcommand("", argc, argv, subcmds,
                          sizeof(subcmds) / sizeof(subcmds[0]), 1);
}
