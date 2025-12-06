/**
 * @file api_file_manager.h
 * @brief Handles all protected SPIFFS File API Routes
 * @project Gemini v3.1.0
 */

#ifndef API_FILE_MANAGER_H
#define API_FILE_MANAGER_H

#include <ESPAsyncWebServer.h>

void apiRegisterFileRoutes(AsyncWebServer* server, const char* username, const char* password);

#endif // API_FILE_MANAGER_H