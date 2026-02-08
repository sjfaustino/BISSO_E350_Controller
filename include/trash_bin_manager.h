/**
 * @file trash_bin_manager.h
 * @brief Trash Bin Management with 30-day auto-delete for BISSO E350
 */

#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>

/**
 * @brief Initialize trash bin directories on all mounted filesystems
 */
void trashBinInit();

/**
 * @brief Move a file or directory to the trash bin
 * @param fs Pointer to the filesystem (LittleFS or SD)
 * @param path Canonical path of the item to delete
 * @return true if moved successfully
 */
bool trashBinMoveToTrash(FS* fs, const char* path);

/**
 * @brief Manually trigger a purge of trash items older than 30 days
 */
void trashBinAutoPurge();

/**
 * @brief Restore a file or directory from the trash bin
 * @param fs Pointer to the filesystem (LittleFS or SD)
 * @param trashPath Full path of the item in .trash (e.g. "/.trash/file.ext.1234567890")
 * @return true if restored successfully
 */
bool trashBinRestore(FS* fs, const char* trashPath);

/**
 * @brief Background task for periodic trash cleanup
 */
void trashBinStartBackgroundHandler();
