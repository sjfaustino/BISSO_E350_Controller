/**
 * @file api_file_manager.h
 * @brief Handles all protected File API Routes (PsychicHttp)
 * @project Gemini v3.6.0
 */

#ifndef API_FILE_MANAGER_H
#define API_FILE_MANAGER_H

#include <PsychicHttp.h>

// PHASE 5.10: Removed username/password parameters - auth handled via auth_manager
void apiRegisterFileRoutes(PsychicHttpServer& server);

#endif // API_FILE_MANAGER_H