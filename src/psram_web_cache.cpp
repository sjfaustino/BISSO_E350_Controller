/**
 * @file psram_web_cache.cpp
 * @brief PSRAM-based Web UI Cache Manager
 */

#include "psram_web_cache.h"
#include <LittleFS.h>
#include "psram_alloc.h"
#include "serial_logger.h"

PsramWebCache::~PsramWebCache() {
    for (auto const& [path, file] : cache) {
        psramFree(file.data);
    }
    cache.clear();
}

bool PsramWebCache::init(const char* root) {
    logPrintf("[CACHE] Initializing PSRAM Web Cache from %s...\r\n", root);
    
    // Clear and reset before start
    for (auto const& [path, file] : cache) {
        psramFree(file.data);
    }
    cache.clear();
    total_size = 0;

    // Start recursive loading
    bool success = loadFromDir(root);
    
    // Final summary - only once!
    logPrintf("[CACHE] Total Assets: %u | Total Size: %u bytes\r\n", (uint32_t)cache.size(), (uint32_t)total_size);
    return success;
}

bool PsramWebCache::loadFromDir(const char* dirPath) {
    File dir = LittleFS.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        logError("[CACHE] Failed to open directory %s", dirPath);
        return false;
    }

    File entry = dir.openNextFile();
    while (entry) {
        std::string path = entry.path();
        
        if (entry.isDirectory()) {
            // Skip hidden directories (.trash, etc.) - they contain non-web content
            const char* name = entry.name();
            if (name[0] != '.') {
                loadFromDir(path.c_str());
            }
        } else {
            size_t fileSize = entry.size();
            // Skip large binary files (e.g. pwa-icon.png 495KB) — served from flash on demand
            if (fileSize > 102400) {
                logPrintf("[CACHE] Skipped %s (%u bytes, too large)\r\n", path.c_str(), (uint32_t)fileSize);
                entry = dir.openNextFile();
                continue;
            }
            // Allocate in PSRAM
            uint8_t* buffer = (uint8_t*)psramMalloc(fileSize);
            
            if (buffer) {
                size_t readSize = entry.read(buffer, fileSize);
                if (readSize == fileSize) {
                    cached_file_t file;
                    file.data = buffer;
                    file.size = fileSize;
                    file.content_type = getContentType(path);
                    
                    cache[path] = file;
                    total_size += fileSize;
                    logPrintf("[CACHE] Loaded %s (%u bytes)\r\n", path.c_str(), (uint32_t)fileSize);
                } else {
                    logError("[CACHE] Failed to read %s", path.c_str());
                    psramFree(buffer);
                }
            } else {
                logError("[CACHE] PSRAM allocation failed for %s (%u bytes)", path.c_str(), (uint32_t)fileSize);
            }
        }
        entry = dir.openNextFile();
    }
    return true;
}

bool PsramWebCache::get(const char* path, const cached_file_t** file) const {
    auto it = cache.find(path);
    if (it != cache.end()) {
        *file = &(it->second);
        return true;
    }
    
    // Check if path is a directory and try /index.html
    std::string index_path = path;
    if (index_path.back() == '/') {
        index_path += "index.html";
    } else if (cache.find(index_path + "/index.html") != cache.end()) {
        index_path += "/index.html";
    }
    
    it = cache.find(index_path);
    if (it != cache.end()) {
        *file = &(it->second);
        return true;
    }

    return false;
}

std::string PsramWebCache::getContentType(const std::string& filename) {
    if (filename.find(".html") != std::string::npos) return "text/html";
    if (filename.find(".css") != std::string::npos) return "text/css";
    if (filename.find(".js") != std::string::npos) return "application/javascript";
    if (filename.find(".json") != std::string::npos) return "application/json";
    if (filename.find(".png") != std::string::npos) return "image/png";
    if (filename.find(".jpg") != std::string::npos) return "image/jpeg";
    if (filename.find(".ico") != std::string::npos) return "image/x-icon";
    if (filename.find(".svg") != std::string::npos) return "image/svg+xml";
    if (filename.find(".txt") != std::string::npos) return "text/plain";
    return "application/octet-stream";
}
