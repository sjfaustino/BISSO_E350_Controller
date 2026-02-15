/**
 * @file psram_web_cache.h
 * @brief PSRAM-based Web UI Cache Manager
 */

#ifndef PSRAM_WEB_CACHE_H
#define PSRAM_WEB_CACHE_H

#include <Arduino.h>
#include <map>
#include <string>

struct cached_file_t {
    uint8_t* data;
    size_t size;
    std::string content_type;
};

class PsramWebCache {
public:
    static PsramWebCache& getInstance() {
        static PsramWebCache instance;
        return instance;
    }

    /**
     * @brief Recursively load files from LittleFS into PSRAM
     * @param root The directory to start loading from
     * @return true if successful
     */
    bool init(const char* root = "/");

    /**
     * @brief Retrieve a file from the PSRAM cache
     * @param path The URI path (e.g., "/index.html")
     * @param file Output pointer to the cached file structure
     * @return true if found
     */
    bool get(const char* path, const cached_file_t** file) const;

    /**
     * @brief Get total memory used by the cache
     */
    size_t getTotalSize() const { return total_size; }

    /**
     * @brief Get count of cached files
     */
    size_t getFileCount() const { return cache.size(); }

private:
    PsramWebCache() : total_size(0) {}
    ~PsramWebCache();

    std::map<std::string, cached_file_t> cache;
    size_t total_size;

    bool loadFromDir(const char* dirPath);
    std::string getContentType(const std::string& filename);
};

#endif // PSRAM_WEB_CACHE_H
