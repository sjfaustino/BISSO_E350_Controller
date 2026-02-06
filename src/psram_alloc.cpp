/**
 * @file psram_alloc.cpp
 * @brief PSRAM-aware memory allocation implementation
 * @project BISSO E350 Controller
 */

#include "psram_alloc.h"
#include "serial_logger.h"
#include <esp_heap_caps.h>

// Cache PSRAM availability at first check
static bool psram_checked = false;
static bool psram_available = false;

bool psramIsAvailable() {
    if (!psram_checked) {
        psram_available = (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0);
        psram_checked = true;
    }
    return psram_available;
}

size_t psramGetTotalSize() {
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

size_t psramGetFreeSize() {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

void* psramMalloc(size_t size) {
    if (size == 0) return NULL;
    
    void* ptr = NULL;
    
    // Try PSRAM first if available
    if (psramIsAvailable()) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (ptr) {
            logDebug("[PSRAM] Allocated %u bytes in PSRAM", size);
            return ptr;
        }
        // PSRAM allocation failed, try internal heap
        logDebug("[PSRAM] PSRAM allocation failed for %u bytes, falling back to internal", size);
    }
    
    // Fallback to internal heap
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr) {
        logDebug("[PSRAM] Allocated %u bytes in internal heap", size);
    }
    
    return ptr;
}

void* psramCalloc(size_t count, size_t size) {
    if (count == 0 || size == 0) return NULL;
    
    size_t total = count * size;
    void* ptr = NULL;
    
    // Try PSRAM first if available
    if (psramIsAvailable()) {
        ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM);
        if (ptr) {
            logDebug("[PSRAM] Calloc %u bytes in PSRAM", total);
            return ptr;
        }
    }
    
    // Fallback to internal heap
    ptr = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr) {
        logDebug("[PSRAM] Calloc %u bytes in internal heap", total);
    }
    
    return ptr;
}

void* psramRealloc(void* ptr, size_t size) {
    if (size == 0) {
        psramFree(ptr);
        return NULL;
    }
    
    if (ptr == NULL) {
        return psramMalloc(size);
    }
    
    void* new_ptr = NULL;
    
    // Check if original pointer is in PSRAM
    bool was_in_psram = psramIsPointerInPsram(ptr);
    
    // Try to realloc in same memory type first
    if (was_in_psram && psramIsAvailable()) {
        new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
        if (new_ptr) return new_ptr;
    }
    
    // Try internal heap
    new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (new_ptr) return new_ptr;
    
    // If original was in internal and realloc failed, try PSRAM
    if (!was_in_psram && psramIsAvailable()) {
        new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
    }
    
    return new_ptr;
}

void psramFree(void* ptr) {
    if (ptr != NULL) {
        heap_caps_free(ptr);
    }
}

bool psramIsPointerInPsram(void* ptr) {
    if (ptr == NULL) return false;
    
    // PSRAM address range on ESP32-S3: 0x3C000000 - 0x3DFFFFFF
    // This is the data bus address for PSRAM
    uintptr_t addr = (uintptr_t)ptr;
    return (addr >= 0x3C000000 && addr < 0x3E000000);
}
