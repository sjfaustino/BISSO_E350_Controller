/**
 * Mock Data Generator for Offline Development
 * Simulates realistic device data for UI testing and visualization
 * Enhanced with work cycles, load patterns, and occasional issues
 */

class MockDataGenerator {
    constructor() {
        this.startTime = Date.now();
        this.cycleTime = 0;
        this.workCycle = 0; // 0-100% through a work cycle
        this.temperature = 35; // Thermal mass simulation
        this.spindleSpeed = 0; // Smooth ramping
        this.alarmState = null;
        this.alarmTime = 0;
    }

    /**
     * Generate complete mock state
     */
    generateState() {
        this.cycleTime = (Date.now() - this.startTime) / 1000;

        // Work cycle: 0-30s cutting, 30-40s idle, repeat
        const cyclePhase = this.cycleTime % 40;
        this.workCycle = cyclePhase < 30 ? (cyclePhase / 30) * 100 : 0;
        const isCutting = cyclePhase < 30;

        return {
            system: this.generateSystemMetrics(isCutting),
            motion: this.generateMotionStatus(isCutting),
            safety: this.generateSafetyStatus(),
            vfd: this.generateVFDStatus(isCutting),
            network: this.generateNetworkStatus(),
            axis: this.generateAxisMetrics(isCutting)
        };
    }

    /**
     * System metrics (CPU, memory, temperature)
     */
    generateSystemMetrics(isCutting) {
        // CPU load varies with cutting activity
        const baseCpu = isCutting ? 60 : 20;
        const cpuVariance = isCutting ? 15 : 5;
        const cpuNoise = (Math.random() - 0.5) * cpuVariance;
        const cpuLoad = Math.max(5, Math.min(95, baseCpu + Math.sin(this.cycleTime * 0.5) * 10 + cpuNoise));

        // Memory usage with gradual growth
        const baseMemory = 180000 + (this.cycleTime * 100); // Slow leak simulation
        const memoryNoise = (Math.random() - 0.5) * 8000;
        const freeHeap = Math.max(100000, Math.min(350000, baseMemory + memoryNoise));

        // Temperature with thermal mass - heats up during cutting, cools during idle
        const targetTemp = isCutting ? 55 : 38;
        const tempRate = 0.05; // Thermal response rate
        this.temperature += (targetTemp - this.temperature) * tempRate + (Math.random() - 0.5) * 0.5;
        this.temperature = Math.max(25, Math.min(75, this.temperature));

        return {
            cpu_percent: cpuLoad,
            free_heap_bytes: freeHeap,
            temperature: this.temperature,
            uptime_ms: this.cycleTime * 1000,
            fw_version: '3.1.0',
            hw_version: 'E350 Rev A'
        };
    }

    /**
     * Motion status (moving/stopped, quality, position)
     */
    generateMotionStatus(isCutting) {
        // Simulate realistic cutting patterns
        const quality = isCutting
            ? 80 + Math.sin(this.cycleTime * 2) * 10 + (Math.random() - 0.5) * 5
            : 95 + (Math.random() - 0.5) * 3;

        const jitter = isCutting
            ? 0.3 + (Math.random() - 0.5) * 0.2
            : 0.05 + (Math.random() - 0.5) * 0.03;

        return {
            moving: isCutting,
            status: isCutting ? 'cutting' : 'idle',
            quality: Math.max(0, Math.min(100, quality)),
            jitter: Math.max(0, jitter),
            feed_rate: isCutting ? 100 + (Math.random() - 0.5) * 20 : 0
        };
    }

    /**
     * Safety status (e-stop, alarms) - occasionally triggers warnings
     */
    generateSafetyStatus() {
        // Trigger occasional warnings for testing (5% chance every 10 seconds)
        if (!this.alarmState && Math.floor(this.cycleTime) % 10 === 0 && Math.random() < 0.05) {
            const warnings = [
                { code: 'WARN_01', message: 'High vibration detected on Y axis', severity: 'warning' },
                { code: 'WARN_02', message: 'Spindle temperature elevated', severity: 'warning' },
                { code: 'INFO_01', message: 'Maintenance due in 10 hours', severity: 'info' }
            ];
            this.alarmState = warnings[Math.floor(Math.random() * warnings.length)];
            this.alarmTime = this.cycleTime;
        }

        // Clear alarm after 15 seconds
        if (this.alarmState && (this.cycleTime - this.alarmTime) > 15) {
            this.alarmState = null;
        }

        return {
            estop: false,
            alarm: this.alarmState !== null,
            alarm_code: this.alarmState?.code || null,
            alarm_message: this.alarmState?.message || null,
            alarm_severity: this.alarmState?.severity || null,
            door_open: false,
            status: this.alarmState ? this.alarmState.severity.toUpperCase() : 'OK'
        };
    }

    /**
     * VFD/Spindle status - PLC controls VFD with 3 discrete speed profiles
     * We only read from the VFD, PLC does all control
     */
    generateVFDStatus(isCutting) {
        // PLC selects from 3 discrete speed profiles
        const SPEED_PROFILES = {
            OFF: 0,      // Idle/stopped
            LOW: 12000,  // Low speed cutting (soft materials)
            MED: 15000,  // Medium speed cutting (standard)
            HIGH: 18000  // High speed cutting (hard materials)
        };

        // Simulate PLC selecting speed profile based on work cycle
        let targetProfile;
        if (!isCutting) {
            targetProfile = SPEED_PROFILES.OFF;
        } else {
            // Vary between profiles during cutting
            const phase = Math.floor(this.workCycle / 25); // Change profile every ~7.5s during 30s cut
            switch (phase % 3) {
                case 0: targetProfile = SPEED_PROFILES.MED; break;
                case 1: targetProfile = SPEED_PROFILES.HIGH; break;
                case 2: targetProfile = SPEED_PROFILES.LOW; break;
                default: targetProfile = SPEED_PROFILES.MED;
            }
        }

        // VFD ramps internally (this is VFD behavior, not our control)
        // Typical VFD ramp time: 2-5 seconds for full range
        const rampRate = 200; // Hz per update cycle (~50Hz/sec)
        if (this.spindleSpeed < targetProfile) {
            this.spindleSpeed = Math.min(targetProfile, this.spindleSpeed + rampRate);
        } else if (this.spindleSpeed > targetProfile) {
            this.spindleSpeed = Math.max(targetProfile, this.spindleSpeed - rampRate);
        }

        // Current and voltage vary with load
        const loadFactor = this.spindleSpeed / 18000;
        const current = loadFactor * 10 + (Math.random() - 0.5) * 1;
        const voltage = 380 + (Math.random() - 0.5) * 8;
        const vfdTemp = 40 + (loadFactor * 25) + (Math.random() - 0.5) * 3;

        return {
            frequency_hz: this.spindleSpeed + (Math.random() - 0.5) * 50, // Small fluctuation
            rpm: (this.spindleSpeed * 2) / 60, // Simplified conversion
            current_amps: Math.max(0, current),
            voltage: voltage,
            power_kw: (voltage * current) / 1000,
            temperature: vfdTemp,
            error_count: 0,
            running: this.spindleSpeed > 500,
            // Additional fields showing PLC control
            speed_profile: this.spindleSpeed === 0 ? 'OFF' :
                          Math.abs(this.spindleSpeed - SPEED_PROFILES.LOW) < 1000 ? 'LOW' :
                          Math.abs(this.spindleSpeed - SPEED_PROFILES.MED) < 1000 ? 'MED' : 'HIGH'
        };
    }

    /**
     * Network connectivity status with realistic latency
     */
    generateNetworkStatus() {
        // Latency varies slightly with system load
        const baseLatency = 18;
        const latencySpike = Math.random() < 0.1 ? Math.random() * 30 : 0; // Occasional spike
        const latency = baseLatency + (Math.random() - 0.5) * 5 + latencySpike;

        // Signal strength with minor fluctuation
        const signal = -45 + Math.sin(this.cycleTime * 0.1) * 3 + (Math.random() - 0.5) * 2;

        return {
            wifi_connected: true,
            signal_percent: Math.max(0, Math.min(100, (signal + 100) * 2)),
            rssi: Math.floor(signal),
            latency_ms: Math.max(5, latency),
            ip_address: '192.168.1.100',
            mac_address: 'AA:BB:CC:DD:EE:FF',
            packets_sent: Math.floor(this.cycleTime * 10),
            packets_received: Math.floor(this.cycleTime * 9.8),
            packet_loss: 0.2
        };
    }

    /**
     * Axis metrics (X, Y, Z positions, quality, jitter)
     * Simulates realistic cutting paths
     */
    generateAxisMetrics(isCutting) {
        let xPos, yPos, zPos, aPos;

        if (isCutting) {
            // Simulate a rectangular cutting pattern
            const pathProgress = (this.workCycle / 100) * 4; // 0-4 for 4 sides
            const side = Math.floor(pathProgress);
            const sideProgress = pathProgress - side;

            switch(side) {
                case 0: // Moving right
                    xPos = 50 + sideProgress * 200;
                    yPos = 50;
                    zPos = 10;
                    break;
                case 1: // Moving up
                    xPos = 250;
                    yPos = 50 + sideProgress * 150;
                    zPos = 10;
                    break;
                case 2: // Moving left
                    xPos = 250 - sideProgress * 200;
                    yPos = 200;
                    zPos = 10;
                    break;
                default: // Moving down
                    xPos = 50;
                    yPos = 200 - sideProgress * 150;
                    zPos = 10;
            }
            aPos = (this.cycleTime * 20) % 360; // Slow rotation during cut
        } else {
            // Return to home position when idle
            xPos = 50;
            yPos = 50;
            zPos = 50; // Raised
            aPos = 0;
        }

        const generateAxis = (pos, targetPos, moving) => ({
            position_mm: pos + (Math.random() - 0.5) * (moving ? 0.3 : 0.05),
            target_position_mm: targetPos,
            velocity_mms: moving ? 50 + (Math.random() - 0.5) * 10 : 0,
            quality: moving ? 82 + (Math.random() - 0.5) * 8 : 95,
            jitter_mms: moving ? 0.25 + (Math.random() - 0.5) * 0.15 : 0.05,
            stalled: false,
            following_error: (Math.random() - 0.5) * 0.08,
            load_percent: moving ? 45 + (Math.random() - 0.5) * 15 : 5
        });

        return {
            x: generateAxis(xPos, xPos, isCutting),
            y: generateAxis(yPos, yPos, isCutting),
            z: generateAxis(zPos, zPos, false), // Z doesn't move in this pattern
            a: generateAxis(aPos, aPos, isCutting)
        };
    }

    /**
     * Reset the time counter
     */
    reset() {
        this.startTime = Date.now();
        this.cycleTime = 0;
        this.workCycle = 0;
        this.temperature = 35;
        this.spindleSpeed = 0;
        this.alarmState = null;
    }
}

/**
 * Mock WebSocket replacement
 */
class MockWebSocket {
    constructor() {
        this.dataGenerator = new MockDataGenerator();
        this.listeners = {
            message: []
        };
        this.isConnected = true;
        this.updateInterval = null;
        this.updateRate = 100; // ms between updates
    }

    /**
     * Start sending mock data
     */
    start() {
        console.log('[MockWebSocket] Starting mock data stream');

        // Send initial state immediately
        const initialState = this.dataGenerator.generateState();
        const initialEvent = new CustomEvent('message', {
            detail: {
                data: JSON.stringify({
                    type: 'state_update',
                    data: initialState
                })
            }
        });

        // Emit on next tick to ensure listeners are registered
        setTimeout(() => {
            this.listeners.message.forEach(listener => {
                listener.call(this, initialEvent);
            });

            // Emit connection event after initial state
            window.dispatchEvent(new Event('ws-connected'));

            // Start regular updates
            this.updateInterval = setInterval(() => {
                const state = this.dataGenerator.generateState();

                // Emit as if it were a real WebSocket message
                const event = new CustomEvent('message', {
                    detail: {
                        data: JSON.stringify({
                            type: 'state_update',
                            data: state
                        })
                    }
                });

                this.listeners.message.forEach(listener => {
                    listener.call(this, event);
                });
            }, this.updateRate);
        }, 0);
    }

    /**
     * Stop sending mock data
     */
    stop() {
        console.log('[MockWebSocket] Stopping mock data stream');
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }
        window.dispatchEvent(new Event('ws-disconnected'));
    }

    /**
     * Add event listener
     */
    addEventListener(event, callback) {
        if (event === 'message') {
            this.listeners.message.push(callback);
        }
    }

    /**
     * Remove event listener
     */
    removeEventListener(event, callback) {
        if (event === 'message') {
            this.listeners.message = this.listeners.message.filter(l => l !== callback);
        }
    }

    /**
     * Send message (no-op for mock)
     */
    send(data) {
        console.log('[MockWebSocket] Mock send:', data);
    }

    /**
     * Close connection
     */
    close() {
        this.stop();
    }
}

/**
 * Global mock mode control
 */
window.MockMode = {
    enabled: false,
    mockWs: null,

    /**
     * Enable mock mode
     */
    enable(triggerNavigate = true) {
        if (this.enabled) return;

        console.log('[MockMode] Enabling mock data mode');
        this.enabled = true;

        // Check if shared WebSocket already initialized
        if (window.SharedWebSocket && window.SharedWebSocket.ws) {
            // Replace with mock
            const mockWs = new MockWebSocket();
            window.SharedWebSocket.ws = mockWs;
            window.SharedWebSocket.isConnected = true;
            mockWs.start();
            this.mockWs = mockWs;
        } else {
            // Create new mock WebSocket
            const mockWs = new MockWebSocket();
            mockWs.start();
            this.mockWs = mockWs;
        }

        // Pre-populate with historical data for graphs
        // Delay slightly to ensure generator is fully initialized
        setTimeout(() => this.generateHistoricalData(), 100);

        // Show indicator
        const statusDot = document.getElementById('status-dot');
        const statusText = document.getElementById('status-text');
        if (statusDot && statusText) {
            statusDot.style.borderStyle = 'dashed';
            statusText.textContent = 'Mock Mode';
        }

        AlertManager.add('Mock data mode enabled (offline preview)', 'info', 3000);

        // Only trigger navigation if explicitly requested (not during initial page load)
        // During page load, Router.init() will handle navigation
        if (triggerNavigate && window.Router && window.Router.currentPage !== null) {
            console.log('[MockMode] Triggering router navigation');
            Router.navigate();
        }
    },

    /**
     * Disable mock mode
     */
    disable() {
        if (!this.enabled) return;

        console.log('[MockMode] Disabling mock data mode');
        this.enabled = false;

        if (this.mockWs) {
            this.mockWs.stop();
            this.mockWs = null;
        }

        // Clear mock indicator
        const statusDot = document.getElementById('status-dot');
        const statusText = document.getElementById('status-text');
        if (statusDot && statusText) {
            statusDot.style.borderStyle = '';
            statusText.textContent = 'Offline';
        }

        AlertManager.add('Mock data mode disabled', 'info', 2000);

        // Trigger router to reload the page back to normal mode
        if (window.Router) {
            Router.navigate();
        }
    },

    /**
     * Generate historical data to pre-populate charts
     * Creates 60 data points spanning the last minute
     */
    generateHistoricalData() {
        if (!this.mockWs || !this.mockWs.dataGenerator) {
            console.warn('[MockMode] Cannot generate historical data - mock WebSocket not ready');
            return;
        }

        console.log('[MockMode] Generating historical data for charts...');

        // Temporarily adjust start time to simulate past data
        const originalStartTime = this.mockWs.dataGenerator.startTime;
        const now = Date.now();
        const historyDuration = 60000; // 60 seconds of history
        const dataPoints = 60; // One per second
        const interval = historyDuration / dataPoints;

        // Generate and dispatch historical data points
        for (let i = 0; i < dataPoints; i++) {
            // Set generator to simulate past time
            const timeOffset = historyDuration - (i * interval);
            this.mockWs.dataGenerator.startTime = now - timeOffset;

            // Generate state for this point in time
            const state = this.mockWs.dataGenerator.generateState();

            // Dispatch to AppState (will trigger state-changed event)
            if (window.AppState) {
                window.AppState.update(state);
            }
        }

        // Restore original start time
        this.mockWs.dataGenerator.startTime = originalStartTime;

        console.log(`[MockMode] Generated ${dataPoints} historical data points`);
    },

    /**
     * Toggle mock mode
     */
    toggle() {
        if (this.enabled) {
            this.disable();
        } else {
            this.enable();
        }
    }
};

// Check for mock mode in URL and enable synchronously if requested
if (window.location.search.includes('mock=true')) {
    // Mark that mock mode should be enabled
    window.__mockModeRequested = true;
}

// Auto-enable mock mode when loading from file:// protocol
if (window.location.protocol === 'file:') {
    console.log('[MockMode] File protocol detected - auto-enabling mock mode');
    window.__mockModeRequested = true;
}

// Expose global shortcut: press 'M' key to toggle mock mode
window.addEventListener('keydown', (e) => {
    if (e.key.toLowerCase() === 'm' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        const input = document.activeElement;
        // Don't toggle if typing in an input
        if (input.tagName !== 'INPUT' && input.tagName !== 'TEXTAREA') {
            MockMode.toggle();
        }
    }
});
