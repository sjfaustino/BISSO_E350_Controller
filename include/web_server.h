/**
 * @file web_server.h
 * @brief PsychicHttp Web Server manager with WebSockets, Auth, and File Management
 * @project PosiPro
 * @author Sergio Faustino - sjfaustino@gmail.com
 * @note Migrated from ESPAsyncWebServer to PsychicHttp for stability
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <PsychicHttp.h>
#include <SPIFFS.h>
#include "config_unified.h"
#include "system_constants.h" 
#include "system_telemetry.h"

class WebServerManager {
public:
    WebServerManager(uint16_t port = 80);
    ~WebServerManager();

    // Initialization
    void init();
    void begin();

    // Legacy support (No-op in PsychicHttp mode)
    void handleClient();

    // Telemetry & State
    void setSystemStatus(const char* status);
    void setAxisPosition(char axis, float position);
    void setSystemUptime(uint32_t seconds);

    // VFD Telemetry (PHASE 5.5: VFD current-based monitoring)
    void setVFDCurrent(float current_amps);
    void setVFDFrequency(float frequency_hz);
    void setVFDThermalState(int16_t thermal_percent);
    void setVFDFaultCode(uint16_t fault_code);
    void setVFDCalibrationThreshold(float threshold_amps);
    void setVFDCalibrationValid(bool is_valid);
    void setVFDConnected(bool is_connected);
    void setDROConnected(bool is_connected);
    // Spindle Metrics (RPM, Surface Speed)
    void setSpindleRPM(float rpm);
    void setSpindleSpeed(float speed_m_s);

    // Axis Metrics (PHASE 5.6: Per-axis motion validation)
    void setAxisQualityScore(uint8_t axis, uint32_t quality_score);
    void setAxisJitterAmplitude(uint8_t axis, float jitter_mms);
    void setAxisStalled(uint8_t axis, bool is_stalled);
    void setAxisVFDError(uint8_t axis, float error_percent);

    // Push state to all connected WebSocket clients
    void broadcastState();

    // Credentials Management (PHASE 5.1: Security hardening)
    void loadCredentials();
    bool isPasswordChangeRequired();
    void setPassword(const char* new_password);

    // PsychicHttp WebSocket handler (public for callbacks)
    PsychicWebSocketHandler* getWebSocketHandler() { return &wsHandler; }

private:
    PsychicHttpServer server;
    PsychicWebSocketHandler wsHandler;
    uint16_t port;
    
    // Internal State Cache
    struct {
        char status[32];
        float x_pos, y_pos, z_pos, a_pos;
        uint32_t uptime_sec;

        // VFD telemetry (PHASE 5.5)
        float vfd_current_amps;
        float vfd_frequency_hz;
        int16_t vfd_thermal_percent;
        uint16_t vfd_fault_code;
        float vfd_threshold_amps;
        bool vfd_calibration_valid;
        bool vfd_connected;
        bool dro_connected;
        float spindle_rpm;
        float spindle_speed_m_s;

        // Axis metrics (PHASE 5.6) - per-axis
        struct {
            uint32_t quality_score;
            float jitter_mms;
            bool stalled;
            float vfd_error_percent;
        } axis_metrics[3];  // 0=X, 1=Y, 2=Z
    } current_status;

    // PHASE 5.10: Spinlock to protect current_status from concurrent access
    portMUX_TYPE statusSpinlock = portMUX_INITIALIZER_UNLOCKED;

    // Handlers
    void setupRoutes();
    
    // PsychicHttp handler signatures
    esp_err_t handleJogBody(PsychicRequest *request);
    esp_err_t handleFirmwareUpload(PsychicRequest *request, const String& filename, 
                                    uint64_t index, uint8_t *data, size_t len, bool final);
    
    // WebSocket callbacks
    void onWsOpen(PsychicWebSocketClient *client);
    esp_err_t onWsFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame);
    void onWsClose(PsychicWebSocketClient *client);

    // File Manager Handlers
    esp_err_t handleFileList(PsychicRequest *request);
    esp_err_t handleFileDelete(PsychicRequest *request);

    // JSON Builder Helper (Code Audit Refactor)
    void buildTelemetryJson(JsonDocument& doc, const system_telemetry_t& telemetry);
};

extern WebServerManager webServer;

#endif // WEB_SERVER_H
