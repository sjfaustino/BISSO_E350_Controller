/**
 * @file api_file_manager.h
 * @brief Handles all protected SPIFFS File API Routes
 * @project Gemini v3.1.0
 */

#ifndef API_FILE_MANAGER_H
#define API_FILE_MANAGER_H

#include <ESPAsyncWebServer.h>

// PHASE 5.10: Removed username/password parameters - auth handled via auth_manager
void apiRegisterFileRoutes(AsyncWebServer* server);

#endif // API_FILE_MANAGER_H