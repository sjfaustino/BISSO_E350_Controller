/**
 * @file api_routes.h
 * @brief Web API Route Registration Functions
 * @details Declares route registration functions for modular API organization.
 */

#ifndef API_ROUTES_H
#define API_ROUTES_H

#include <PsychicHttp.h>

// Route group registration functions
void registerTelemetryRoutes(PsychicHttpServer& server);
void registerGcodeRoutes(PsychicHttpServer& server);
void registerMotionRoutes(PsychicHttpServer& server);
void registerNetworkRoutes(PsychicHttpServer& server);
void registerHardwareRoutes(PsychicHttpServer& server);
void registerSystemRoutes(PsychicHttpServer& server);

// Helper function from web_server.cpp (shared)
esp_err_t sendJsonResponse(PsychicResponse* response, JsonDocument& doc, int status = 200);

#endif // API_ROUTES_H
