/**
 * @file api_routes_gcode.cpp
 * @brief G-code Execution and Queue API Routes
 * @details Handles /api/gcode, /api/gcode/state, /api/gcode/queue/...
 */

#include "api_routes.h"
#include "gcode_parser.h"
#include "gcode_queue.h"
#include "hardware_config.h"
#include "serial_logger.h"
#include <ArduinoJson.h>
#include <math.h>

void registerGcodeRoutes(PsychicHttpServer& server) {
    
    // POST /api/gcode - Execute G-code command
    server.on("/api/gcode", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        String body = request->body();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) return response->send(400, "application/json", "{\"success\":false, \"error\":\"Invalid JSON\"}");
        
        const char* cmd = doc["command"];
        if (!cmd || strlen(cmd) == 0) return response->send(400, "application/json", "{\"success\":false, \"error\":\"No command\"}");
        
        // POST /api/gcode - Execute G-code command
        uint16_t job_id = gcodeQueueAdd(cmd);
        gcodeQueueMarkRunning();
        
        // PHASE 5.11: Wait-and-Retry logic if motion buffer is full
        // Prevents "Command rejected" errors during high-cadence small moves.
        bool result = false;
        for (int retry = 0; retry < 10; retry++) {
            result = gcodeParser.processCommand(cmd);
            if (result) break;
            
            // If we failed due to buffer full, wait briefly and try again
            logWarning("[API] Buffer full, retry %d/10 for: %s", retry + 1, cmd);
            vTaskDelay(pdMS_TO_TICKS(200)); 
        }
        
        // Update queue status
        if (result) {
            gcodeQueueMarkCompleted();
        } else {
            gcodeQueueMarkFailed("Command rejected");
        }
        
        JsonDocument resp;
        resp["success"] = result;
        resp["command"] = cmd;
        resp["job_id"] = job_id;
        
        // Calculate ETA using calibration data for G0/G1 commands
        if (result && (strncasecmp(cmd, "G0", 2) == 0 || strncasecmp(cmd, "G1", 2) == 0)) {
            float x = 0, y = 0, z = 0, f = gcodeParser.getCurrentFeedRate();
            
            // Parse axis values from command
            const char* xp = strchr(cmd, 'X'); if (!xp) xp = strchr(cmd, 'x');
            const char* yp = strchr(cmd, 'Y'); if (!yp) yp = strchr(cmd, 'y');
            const char* zp = strchr(cmd, 'Z'); if (!zp) zp = strchr(cmd, 'z');
            const char* fp = strchr(cmd, 'F'); if (!fp) fp = strchr(cmd, 'f');
            
            if (xp) x = fabs(atof(xp + 1));
            if (yp) y = fabs(atof(yp + 1));
            if (zp) z = fabs(atof(zp + 1));
            if (fp) f = atof(fp + 1);
            
            // Use calibration speeds (med speed as default representative)
            float max_axis_speed = machineCal.X.speed_med_mm_min;
            if (y > x && y > z) max_axis_speed = machineCal.Y.speed_med_mm_min;
            if (z > x && z > y) max_axis_speed = machineCal.Z.speed_med_mm_min;
            
            // Use minimum of feedrate and calibrated speed
            float effective_speed = (f > 0 && f < max_axis_speed) ? f : max_axis_speed;
            if (effective_speed <= 0) effective_speed = 300.0f; // Fallback
            
            // Calculate distance and ETA
            float distance = sqrtf(x*x + y*y + z*z);
            float eta_seconds = (distance / effective_speed) * 60.0f;
            
            resp["eta_seconds"] = eta_seconds;
            resp["distance_mm"] = distance;
            resp["speed_mm_min"] = effective_speed;
        }
        
        return sendJsonResponse(response, resp);
    });

    // GET /api/gcode/state (OPTIMIZED: snprintf, no heap)
    server.on("/api/gcode/state", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        char state_str[64];
        gcodeParser.getParserState(state_str, sizeof(state_str));
        
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
            "{\"success\":true,\"absolute_mode\":%s,\"feedrate\":%.1f,\"state_str\":\"%s\"}",
            (gcodeParser.getDistanceMode() == G_MODE_ABSOLUTE) ? "true" : "false",
            gcodeParser.getCurrentFeedRate(),
            state_str
        );

        return response->send(200, "application/json", buffer);
    });
    
    // GET /api/gcode/queue (FIXED: Overflow-safe consolidated chunked streaming)
    server.on("/api/gcode/queue", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) -> esp_err_t {
        gcode_queue_state_t state = gcodeQueueGetState();
        gcode_job_t jobs[10];
        uint16_t count = gcodeQueueGetAll(jobs, 10);

        response->setContentType("application/json");
        response->sendHeaders();

        char header[256];
        size_t h_len = snprintf(header, sizeof(header), 
            "{\"success\":true,\"queue\":{\"total\":%u,\"pending\":%d,\"completed\":%d,\"failed\":%d,\"current_job_id\":%d,\"paused\":%s},\"jobs\":[",
            state.total_jobs, state.pending_count, state.completed_count, state.failed_count, 
            state.current_job_id, state.paused ? "true" : "false");
        response->sendChunk((uint8_t*)header, h_len);

        char chunk[1024];
        char* p = chunk;
        size_t rem = sizeof(chunk);

        auto flushIfFull = [&](size_t needed) {
            if (needed >= rem) {
                response->sendChunk((uint8_t*)chunk, p - chunk);
                p = chunk;
                rem = sizeof(chunk);
            }
        };

        for (uint16_t i = 0; i < count; i++) {
            char job_buf[512];
            int j_len = snprintf(job_buf, sizeof(job_buf), 
                "{\"id\":%u,\"command\":\"%s\",\"status\":%d,\"queued_time\":%lu,\"start_time\":%lu,\"end_time\":%lu",
                jobs[i].id, jobs[i].command, (int)jobs[i].status, 
                (unsigned long)jobs[i].queued_time_ms, (unsigned long)jobs[i].start_time_ms, (unsigned long)jobs[i].end_time_ms);
            
            if (jobs[i].status == JOB_FAILED) {
                int e_len = snprintf(job_buf + j_len, sizeof(job_buf) - j_len, ",\"error\":\"%s\"", jobs[i].error);
                j_len += e_len;
            }

            int s_len = snprintf(job_buf + j_len, sizeof(job_buf) - j_len, "}%s", (i < count - 1) ? "," : "");
            j_len += s_len;
            
            flushIfFull(j_len);
            memcpy(p, job_buf, j_len);
            p += j_len; rem -= j_len;
        }
        response->sendChunk((uint8_t*)chunk, p - chunk);

        const char* footer = "]}";
        response->sendChunk((uint8_t*)footer, strlen(footer));
        
        return response->finishChunking();
    });
    
    // POST /api/gcode/queue/retry
    server.on("/api/gcode/queue/retry", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        if (gcodeQueueRetry()) {
            return response->send(200, "application/json", "{\"success\":true,\"action\":\"retry\"}");
        }
        return response->send(400, "application/json", "{\"success\":false,\"error\":\"No failed job to retry\"}");
    });
    
    // POST /api/gcode/queue/skip
    server.on("/api/gcode/queue/skip", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        if (gcodeQueueSkip()) {
            return response->send(200, "application/json", "{\"success\":true,\"action\":\"skip\"}");
        }
        return response->send(400, "application/json", "{\"success\":false,\"error\":\"No failed job to skip\"}");
    });
    
    // POST /api/gcode/queue/resume
    server.on("/api/gcode/queue/resume", HTTP_POST, [](PsychicRequest *request, PsychicResponse *response) {
        if (gcodeQueueResume()) {
            return response->send(200, "application/json", "{\"success\":true,\"action\":\"resume\"}");
        }
        return response->send(400, "application/json", "{\"success\":false,\"error\":\"No failed job to resume\"}");
    });
    
    // DELETE /api/gcode/queue - Clear queue
    server.on("/api/gcode/queue", HTTP_DELETE, [](PsychicRequest *request, PsychicResponse *response) {
        gcodeQueueClear();
        return response->send(200, "application/json", "{\"success\":true}");
    });
    
    logDebug("[WEB] G-code routes registered");
}
