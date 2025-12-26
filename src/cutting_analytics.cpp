/**
 * @file cutting_analytics.cpp
 * @brief Stone Cutting Analytics Implementation
 * @project BISSO E350 Controller
 * @details Computes power, SCE, blade health from motor/encoder data.
 */

#include "cutting_analytics.h"
#include "jxk10_modbus.h"
#include "yhtc05_modbus.h"
#include "motion.h"
#include "serial_logger.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
// MODULE STATE
// ============================================================================

static cutting_analytics_state_t analytics = {
    .config = {
        .motor_voltage_v = 230.0f,
        .motor_efficiency = 0.85f,
        .blade_width_mm = 3.0f,
        .cut_depth_mm = 20.0f,
        .power_factor = 0.8f
    },
    .current_amps = 0.0f,
    .rpm = 0.0f,
    .feed_rate_mms = 0.0f,
    .power_watts = 0.0f,
    .mrr_mm3s = 0.0f,
    .sce_jmm3 = 0.0f,
    .avg_current_amps = 0.0f,
    .avg_power_watts = 0.0f,
    .avg_sce_jmm3 = 0.0f,
    .peak_current_amps = 0.0f,
    .peak_power_watts = 0.0f,
    .baseline_sce = 50.0f,  // Default baseline SCE (J/mm³)
    .sce_deviation_pct = 0.0f,
    .blade_alert = false,
    .session_active = false,
    .session = {
        .start_time_ms = 0,
        .end_time_ms = 0,
        .total_energy_joules = 0.0f,
        .total_material_mm3 = 0.0f,
        .avg_sce = 0.0f,
        .peak_current_amps = 0.0f,
        .peak_power_watts = 0.0f,
        .sample_count = 0
    },
    .update_count = 0,
    .last_update_ms = 0,
    .enabled = true
};

// Rolling average alpha (0.1 = 10% weighting for new samples)
#define ROLLING_ALPHA 0.1f

// Blade alert threshold (25% above baseline)
#define BLADE_ALERT_THRESHOLD_PCT 25.0f

// ============================================================================
// INITIALIZATION
// ============================================================================

void cuttingAnalyticsInit(void) {
    analytics.update_count = 0;
    analytics.last_update_ms = millis();
    analytics.session_active = false;
    memset(&analytics.session, 0, sizeof(analytics.session));
    
    logInfo("[CUTTING] Analytics initialized (V=%.0f, η=%.0f%%, blade=%.1fmm)",
            analytics.config.motor_voltage_v,
            analytics.config.motor_efficiency * 100.0f,
            analytics.config.blade_width_mm);
}

// ============================================================================
// CORE UPDATE LOGIC
// ============================================================================

void cuttingAnalyticsUpdate(void) {
    if (!analytics.enabled) return;
    
    uint32_t now = millis();
    analytics.last_update_ms = now;
    analytics.update_count++;
    
    // 1. Read sensor values
    analytics.current_amps = jxk10GetCurrentAmps();
    analytics.rpm = yhtc05GetRPM();  // 0 if sensor not enabled
    
    // Get feed rate from motion system
    // Use the active axis velocity, or compute magnitude from all axes
    uint8_t active_axis = motionGetActiveAxis();
    if (active_axis < 4) {
        analytics.feed_rate_mms = fabs(motionGetVelocity(active_axis));
    } else {
        // No active axis - compute magnitude from X, Y, Z velocities
        float vx = motionGetVelocity(0);
        float vy = motionGetVelocity(1);
        float vz = motionGetVelocity(2);
        analytics.feed_rate_mms = sqrtf(vx*vx + vy*vy + vz*vz);
    }
    
    // 2. Calculate power
    // P = V × I × PF × η (for single-phase approximation)
    // For 3-phase: P = √3 × V × I × PF × η
    float voltage = analytics.config.motor_voltage_v;
    float pf = analytics.config.power_factor;
    float eff = analytics.config.motor_efficiency;
    analytics.power_watts = 1.732f * voltage * analytics.current_amps * pf * eff;
    
    // 3. Calculate Material Removal Rate (MRR)
    // MRR = feed_rate × cut_depth × blade_width
    float depth = analytics.config.cut_depth_mm;
    float width = analytics.config.blade_width_mm;
    analytics.mrr_mm3s = analytics.feed_rate_mms * depth * width;
    
    // 4. Calculate Specific Cutting Energy (SCE)
    // SCE = Power / MRR (Joules per mm³)
    if (analytics.mrr_mm3s > 0.1f) {
        analytics.sce_jmm3 = analytics.power_watts / analytics.mrr_mm3s;
    } else {
        analytics.sce_jmm3 = 0.0f;  // Not cutting or feed too slow
    }
    
    // 5. Update rolling averages
    if (analytics.update_count == 1) {
        // First sample - initialize
        analytics.avg_current_amps = analytics.current_amps;
        analytics.avg_power_watts = analytics.power_watts;
        analytics.avg_sce_jmm3 = analytics.sce_jmm3;
    } else {
        // Exponential moving average
        analytics.avg_current_amps = ROLLING_ALPHA * analytics.current_amps + 
                                     (1.0f - ROLLING_ALPHA) * analytics.avg_current_amps;
        analytics.avg_power_watts = ROLLING_ALPHA * analytics.power_watts + 
                                    (1.0f - ROLLING_ALPHA) * analytics.avg_power_watts;
        if (analytics.sce_jmm3 > 0.0f) {
            analytics.avg_sce_jmm3 = ROLLING_ALPHA * analytics.sce_jmm3 + 
                                     (1.0f - ROLLING_ALPHA) * analytics.avg_sce_jmm3;
        }
    }
    
    // 6. Update peaks
    if (analytics.current_amps > analytics.peak_current_amps) {
        analytics.peak_current_amps = analytics.current_amps;
    }
    if (analytics.power_watts > analytics.peak_power_watts) {
        analytics.peak_power_watts = analytics.power_watts;
    }
    
    // 7. Blade health check
    if (analytics.baseline_sce > 0.0f && analytics.avg_sce_jmm3 > 0.0f) {
        analytics.sce_deviation_pct = 
            ((analytics.avg_sce_jmm3 - analytics.baseline_sce) / analytics.baseline_sce) * 100.0f;
        analytics.blade_alert = (analytics.sce_deviation_pct > BLADE_ALERT_THRESHOLD_PCT);
    }
    
    // 8. Update active session
    if (analytics.session_active) {
        float dt = 0.1f;  // 100ms update interval
        analytics.session.total_energy_joules += analytics.power_watts * dt;
        analytics.session.total_material_mm3 += analytics.mrr_mm3s * dt;
        analytics.session.sample_count++;
        
        if (analytics.current_amps > analytics.session.peak_current_amps) {
            analytics.session.peak_current_amps = analytics.current_amps;
        }
        if (analytics.power_watts > analytics.session.peak_power_watts) {
            analytics.session.peak_power_watts = analytics.power_watts;
        }
        
        // Update session average SCE
        if (analytics.session.total_material_mm3 > 0.0f) {
            analytics.session.avg_sce = 
                analytics.session.total_energy_joules / analytics.session.total_material_mm3;
        }
    }
}

// ============================================================================
// SESSION MANAGEMENT
// ============================================================================

void cuttingStartSession(void) {
    memset(&analytics.session, 0, sizeof(analytics.session));
    analytics.session.start_time_ms = millis();
    analytics.session_active = true;
    
    logInfo("[CUTTING] Session started");
}

void cuttingEndSession(void) {
    if (analytics.session_active) {
        analytics.session.end_time_ms = millis();
        analytics.session_active = false;
        
        uint32_t duration_ms = analytics.session.end_time_ms - analytics.session.start_time_ms;
        logInfo("[CUTTING] Session ended - Duration: %.1fs, Energy: %.1f J, Material: %.1f mm³",
                duration_ms / 1000.0f,
                analytics.session.total_energy_joules,
                analytics.session.total_material_mm3);
    }
}

bool cuttingIsSessionActive(void) {
    return analytics.session_active;
}

// ============================================================================
// STATE ACCESSORS
// ============================================================================

const cutting_analytics_state_t* cuttingGetState(void) {
    return &analytics;
}

const cutting_session_t* cuttingGetSession(void) {
    return &analytics.session;
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void cuttingSetDepth(float depth_mm) {
    if (depth_mm > 0.0f && depth_mm <= 500.0f) {
        analytics.config.cut_depth_mm = depth_mm;
        logInfo("[CUTTING] Cut depth set to %.1f mm", depth_mm);
    }
}

void cuttingSetBladeWidth(float width_mm) {
    if (width_mm > 0.0f && width_mm <= 50.0f) {
        analytics.config.blade_width_mm = width_mm;
        logInfo("[CUTTING] Blade width set to %.1f mm", width_mm);
    }
}

void cuttingSetMotorParams(float voltage_v, float efficiency, float power_factor) {
    if (voltage_v > 0.0f && voltage_v <= 480.0f) {
        analytics.config.motor_voltage_v = voltage_v;
    }
    if (efficiency > 0.0f && efficiency <= 1.0f) {
        analytics.config.motor_efficiency = efficiency;
    }
    if (power_factor > 0.0f && power_factor <= 1.0f) {
        analytics.config.power_factor = power_factor;
    }
    logInfo("[CUTTING] Motor params: V=%.0f, η=%.0f%%, PF=%.2f",
            analytics.config.motor_voltage_v,
            analytics.config.motor_efficiency * 100.0f,
            analytics.config.power_factor);
}

void cuttingSetSCEBaseline(float baseline_sce) {
    if (baseline_sce > 0.0f && baseline_sce <= 500.0f) {
        analytics.baseline_sce = baseline_sce;
        logInfo("[CUTTING] SCE baseline set to %.1f J/mm³", baseline_sce);
    }
}

void cuttingSetEnabled(bool enable) {
    analytics.enabled = enable;
    logInfo("[CUTTING] Analytics %s", enable ? "ENABLED" : "DISABLED");
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void cuttingResetStats(void) {
    analytics.avg_current_amps = 0.0f;
    analytics.avg_power_watts = 0.0f;
    analytics.avg_sce_jmm3 = 0.0f;
    analytics.peak_current_amps = 0.0f;
    analytics.peak_power_watts = 0.0f;
    analytics.sce_deviation_pct = 0.0f;
    analytics.blade_alert = false;
    analytics.update_count = 0;
    memset(&analytics.session, 0, sizeof(analytics.session));
    analytics.session_active = false;
    
    logInfo("[CUTTING] Statistics reset");
}

void cuttingPrintDiagnostics(void) {
    serialLoggerLock();
    Serial.println("\n[CUTTING] === Stone Cutting Analytics ===");
    Serial.printf("Status:        %s\n", analytics.enabled ? "ENABLED" : "DISABLED");
    Serial.printf("Updates:       %lu\n", (unsigned long)analytics.update_count);
    
    Serial.println("\n[Real-Time]");
    Serial.printf("Current:       %.2f A (avg: %.2f, peak: %.2f)\n",
                  analytics.current_amps, analytics.avg_current_amps, analytics.peak_current_amps);
    Serial.printf("Power:         %.0f W (avg: %.0f, peak: %.0f)\n",
                  analytics.power_watts, analytics.avg_power_watts, analytics.peak_power_watts);
    Serial.printf("Feed Rate:     %.1f mm/s\n", analytics.feed_rate_mms);
    Serial.printf("MRR:           %.2f mm³/s\n", analytics.mrr_mm3s);
    Serial.printf("SCE:           %.1f J/mm³ (avg: %.1f)\n", 
                  analytics.sce_jmm3, analytics.avg_sce_jmm3);
    
    Serial.println("\n[Blade Health]");
    Serial.printf("Baseline SCE:  %.1f J/mm³\n", analytics.baseline_sce);
    Serial.printf("Deviation:     %.1f%%\n", analytics.sce_deviation_pct);
    Serial.printf("Alert:         %s\n", analytics.blade_alert ? "BLADE WORN!" : "OK");
    
    Serial.println("\n[Configuration]");
    Serial.printf("Voltage:       %.0f V\n", analytics.config.motor_voltage_v);
    Serial.printf("Efficiency:    %.0f%%\n", analytics.config.motor_efficiency * 100.0f);
    Serial.printf("Blade Width:   %.1f mm\n", analytics.config.blade_width_mm);
    Serial.printf("Cut Depth:     %.1f mm\n", analytics.config.cut_depth_mm);
    
    if (analytics.session_active || analytics.session.start_time_ms > 0) {
        Serial.println("\n[Session]");
        Serial.printf("Active:        %s\n", analytics.session_active ? "YES" : "NO");
        uint32_t end_ts = analytics.session_active ? millis() : analytics.session.end_time_ms;
        float duration_s = (end_ts - analytics.session.start_time_ms) / 1000.0f;
        Serial.printf("Duration:      %.1f s\n", duration_s);
        Serial.printf("Total Energy:  %.1f J (%.3f kWh)\n", 
                      analytics.session.total_energy_joules,
                      analytics.session.total_energy_joules / 3600000.0f);
        Serial.printf("Material Cut:  %.1f mm³\n", analytics.session.total_material_mm3);
        Serial.printf("Session SCE:   %.1f J/mm³\n", analytics.session.avg_sce);
    }
    
    Serial.println();
    serialLoggerUnlock();
}

size_t cuttingExportJSON(char* buffer, size_t buffer_size) {
    return snprintf(buffer, buffer_size,
        "{"
        "\"enabled\":%s,"
        "\"current_amps\":%.2f,"
        "\"power_watts\":%.0f,"
        "\"feed_rate_mms\":%.1f,"
        "\"mrr_mm3s\":%.2f,"
        "\"sce_jmm3\":%.1f,"
        "\"avg_sce\":%.1f,"
        "\"blade_health\":{\"baseline\":%.1f,\"deviation_pct\":%.1f,\"alert\":%s},"
        "\"session\":{\"active\":%s,\"energy_j\":%.1f,\"material_mm3\":%.1f}"
        "}",
        analytics.enabled ? "true" : "false",
        analytics.current_amps,
        analytics.power_watts,
        analytics.feed_rate_mms,
        analytics.mrr_mm3s,
        analytics.sce_jmm3,
        analytics.avg_sce_jmm3,
        analytics.baseline_sce,
        analytics.sce_deviation_pct,
        analytics.blade_alert ? "true" : "false",
        analytics.session_active ? "true" : "false",
        analytics.session.total_energy_joules,
        analytics.session.total_material_mm3
    );
}
