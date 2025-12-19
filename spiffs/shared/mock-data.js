/**
 * Mock Data Generator for Offline Development
 * Simulates realistic device data for UI testing and visualization
 */

class MockDataGenerator {
    constructor() {
        this.startTime = Date.now();
        this.cycleTime = 0;
    }

    /**
     * Generate complete mock state
     */
    generateState() {
        this.cycleTime = (Date.now() - this.startTime) / 1000;

        return {
            system: this.generateSystemMetrics(),
            motion: this.generateMotionStatus(),
            safety: this.generateSafetyStatus(),
            vfd: this.generateVFDStatus(),
            network: this.generateNetworkStatus(),
            axis: this.generateAxisMetrics()
        };
    }

    /**
     * System metrics (CPU, memory, temperature)
     */
    generateSystemMetrics() {
        // Simulate varying CPU usage with some noise
        const baseCpu = 30 + Math.sin(this.cycleTime * 0.5) * 15;
        const cpuNoise = (Math.random() - 0.5) * 5;

        // Memory usage varies slightly
        const baseMemory = 200000 + Math.sin(this.cycleTime * 0.3) * 30000;
        const memoryNoise = (Math.random() - 0.5) * 5000;

        // Temperature increases under load, has thermal mass
        const baseTemp = 35 + Math.sin(this.cycleTime * 0.2) * 8;
        const tempNoise = (Math.random() - 0.5) * 2;

        return {
            cpu_percent: Math.max(5, Math.min(95, baseCpu + cpuNoise)),
            free_heap_bytes: Math.max(100000, baseMemory + memoryNoise),
            temperature: Math.max(25, Math.min(75, baseTemp + tempNoise)),
            uptime_seconds: this.cycleTime
        };
    }

    /**
     * Motion status (moving/stopped, quality)
     */
    generateMotionStatus() {
        // Alternate between moving and stopped every 15 seconds
        const moving = Math.floor(this.cycleTime / 15) % 2 === 0;

        return {
            moving: moving,
            status: moving ? 'cutting' : 'idle'
        };
    }

    /**
     * Safety status (e-stop, alarms)
     */
    generateSafetyStatus() {
        return {
            estop: false,
            alarm: false,
            door_open: false,
            status: 'OK'
        };
    }

    /**
     * VFD/Spindle status
     */
    generateVFDStatus() {
        // Spindle ramps up/down smoothly
        const baseFreq = Math.sin(this.cycleTime * 0.3) > 0.5
            ? 15000 + Math.sin(this.cycleTime * 0.5) * 3000
            : 500;

        return {
            frequency_hz: Math.max(0, baseFreq + (Math.random() - 0.5) * 1000),
            current_amps: Math.max(0, Math.sin(this.cycleTime * 0.3) * 8 + (Math.random() - 0.5) * 1),
            voltage: 380 + (Math.random() - 0.5) * 5,
            temperature: 45 + Math.sin(this.cycleTime * 0.2) * 10,
            error_count: 0
        };
    }

    /**
     * Network connectivity status
     */
    generateNetworkStatus() {
        return {
            wifi_connected: true,
            signal_percent: 85 + (Math.random() - 0.5) * 10,
            signal_dbm: -45 + (Math.random() - 0.5) * 5,
            latency: 20 + (Math.random() - 0.5) * 15,
            ip_address: '192.168.1.100',
            mac_address: 'AA:BB:CC:DD:EE:FF'
        };
    }

    /**
     * Axis metrics (X, Y, Z positions, quality, jitter)
     */
    generateAxisMetrics() {
        // Simulate smooth motion along all axes
        const xPos = 100 + Math.sin(this.cycleTime * 0.3) * 150;
        const yPos = 150 + Math.cos(this.cycleTime * 0.25) * 100;
        const zPos = 20 + Math.sin(this.cycleTime * 0.4) * 15;
        const aPos = (this.cycleTime * 30) % 360;

        const generateAxis = (pos, axisName) => ({
            position_mm: pos + (Math.random() - 0.5) * 2,
            velocity_mms: Math.sin(this.cycleTime * 0.3) * 50,
            quality: 85 + (Math.random() - 0.5) * 20,
            jitter_mms: (Math.random() - 0.5) * 0.5,
            vfd_error_percent: Math.random() * 2,
            stalled: false,
            target_position_mm: pos,
            pid_error: (Math.random() - 0.5) * 0.1
        });

        return {
            x: generateAxis(xPos, 'X'),
            y: generateAxis(yPos, 'Y'),
            z: generateAxis(zPos, 'Z'),
            a: generateAxis(aPos, 'A')
        };
    }

    /**
     * Reset the time counter
     */
    reset() {
        this.startTime = Date.now();
        this.cycleTime = 0;
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
    enable() {
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

        // Show indicator
        const statusDot = document.getElementById('status-dot');
        const statusText = document.getElementById('status-text');
        if (statusDot && statusText) {
            statusDot.style.borderStyle = 'dashed';
            statusText.textContent = 'Mock Mode';
        }

        AlertManager.add('Mock data mode enabled (offline preview)', 'info', 3000);

        // Trigger router to reload the current page now that mock mode is active
        if (window.Router) {
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
