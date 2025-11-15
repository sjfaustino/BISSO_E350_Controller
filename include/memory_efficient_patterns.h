#ifndef MEMORY_EFFICIENT_PATTERNS_H
#define MEMORY_EFFICIENT_PATTERNS_H

#include <Arduino.h>

// ============================================================================
// MEMORY-EFFICIENT PATTERNS FOR EMBEDDED SYSTEMS
// ============================================================================

/**
 * PATTERN 1: PRE-ALLOCATED BUFFERS
 * 
 * Instead of: String result = "Value: " + String(value);
 * Do this:
 * 
 * static char buffer[64];  // Allocated once, reused
 * snprintf(buffer, sizeof(buffer), "Value: %d", value);
 */

/**
 * PATTERN 2: STACK BUFFERS FOR TEMPORARY DATA
 * 
 * void processData() {
 *   char temp_buffer[128];  // Allocated on stack, freed on function exit
 *   snprintf(temp_buffer, sizeof(temp_buffer), "...");
 *   // Use temp_buffer
 * }  // Stack memory automatically freed
 */

/**
 * PATTERN 3: AVOID STRING CLASS IN LOOPS
 * 
 * Instead of:
 * String result = "";
 * for (int i = 0; i < 1000; i++) {
 *   result += String(data[i]);  // Allocates new memory each time!
 * }
 * 
 * Do this:
 * static char buffer[512];
 * int offset = 0;
 * for (int i = 0; i < 1000; i++) {
 *   offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%d,", data[i]);
 * }
 */

/**
 * PATTERN 4: REUSE JSON DOCUMENTS
 * 
 * Instead of creating new JsonDocument each time:
 * 
 * static JsonDocument doc;  // Allocate once
 * void updateConfig(const char* json_str) {
 *   doc.clear();  // Clear previous data
 *   deserializeJson(doc, json_str);  // Reuse buffer
 * }
 */

/**
 * PATTERN 5: LOGGING WITHOUT ALLOCATION
 * 
 * Instead of:
 * String msg = "Status: " + status + " Value: " + String(value);
 * Serial.println(msg);
 * 
 * Do this (no allocation):
 * Serial.printf("Status: %s Value: %d\n", status, value);
 */

/**
 * PATTERN 6: POOL ALLOCATORS FOR FREQUENTLY USED OBJECTS
 * 
 * template<typename T, size_t POOL_SIZE>
 * class ObjectPool {
 * private:
 *   T pool[POOL_SIZE];
 *   bool used[POOL_SIZE];
 * public:
 *   T* acquire() {
 *     for (int i = 0; i < POOL_SIZE; i++) {
 *       if (!used[i]) {
 *         used[i] = true;
 *         return &pool[i];
 *       }
 *     }
 *     return nullptr;
 *   }
 *   
 *   void release(T* obj) {
 *     int idx = obj - pool;
 *     if (idx >= 0 && idx < POOL_SIZE) {
 *       used[idx] = false;
 *     }
 *   }
 * };
 */

/**
 * PATTERN 7: FIXED-SIZE MESSAGE QUEUES
 * 
 * Don't allocate messages dynamically in queues:
 * 
 * // Good: Fixed size
 * typedef struct {
 *   uint8_t type;
 *   uint32_t data;
 *   char payload[64];
 * } Message;
 * QueueHandle_t queue = xQueueCreate(10, sizeof(Message));
 * 
 * // Bad: Variable size
 * QueueHandle_t queue = xQueueCreate(10, sizeof(String*));
 */

/**
 * PATTERN 8: STATIC ALLOCATION FOR PERIPHERALS
 * 
 * Instead of:
 * WebServer* server = new WebServer(80);  // Dynamic allocation
 * 
 * Do this:
 * static WebServer server(80);  // Static allocation, no fragmentation
 */

/**
 * MEMORY MONITORING MACRO
 * 
 * Check if heap fragmentation is an issue:
 */
#define HEAP_CHECK() do { \
  Serial.print("[MEM] Free: "); Serial.print(ESP.getFreeHeap()); \
  Serial.print(" Largest: "); Serial.println(ESP.getMaxAllocHeap()); \
} while(0)

/**
 * HEAP WATCHDOG
 * 
 * Alert if fragmentation is too high:
 */
bool isHeapFragmented() {
  uint32_t free_heap = ESP.getFreeHeap();
  uint32_t max_alloc = ESP.getMaxAllocHeap();
  
  // If max allocable is less than 50% of total free, fragmented
  return (max_alloc < (free_heap / 2));
}

#endif // MEMORY_EFFICIENT_PATTERNS_H
