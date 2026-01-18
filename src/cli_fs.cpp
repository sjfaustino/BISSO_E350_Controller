/**
 * @file cli_fs.cpp
 * @brief Filesystem CLI Commands (Diagnostic)
 */

#include "cli.h"
#include <LittleFS.h>
#include "serial_logger.h"

void cmd_fs_ls(int argc, char** argv) {
    const char* path = "/";
    if (argc > 1) path = argv[1];

    logPrintf("Listing directory: %s\n", path);
    File root = LittleFS.open(path);
    if (!root) {
        logError("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        logError("Not a directory");
        root.close();
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            logPrintf("  [DIR]  %s\n", file.name());
        } else {
            logPrintf("  [FILE] %-32s %8zu bytes\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
    root.close();
}

void cmd_fs_df(int argc, char** argv) {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    logPrintf("LittleFS Partition Status:\n");
    logPrintf("  Total: %8zu bytes\n", total);
    logPrintf("  Used:  %8zu bytes (%d%%)\n", used, (int)((used * 100) / total));
    logPrintf("  Free:  %8zu bytes\n", total - used);
}

void cmd_fs_cat(int argc, char** argv) {
    if (argc < 2) {
        logError("Usage: cat <filename>");
        return;
    }

    File file = LittleFS.open(argv[1], "r");
    if (!file) {
        logError("Failed to open file: %s", argv[1]);
        return;
    }

    logPrintf("--- %s START ---\n", argv[1]);
    while (file.available()) {
        CLI_SERIAL.write(file.read());
    }
    logPrintf("\n--- %s END ---\n", argv[1]);
    file.close();
}
