/**
 * @file shared/state.js
 * @brief Shared application state for all pages
 * @details Central data store with reactive updates
 */

class AppState {
    static data = {
        system: {
            status: 'INITIALIZING',
            health: 'unknown',
            cpu_percent: 0,
            free_heap_bytes: 0,
            firmware_version: '--',
            uptime_seconds: 0
        },
        motion: {
            position: { x: 0, y: 0, z: 0, a: 0 },
            moving: false,
            status: 'STOPPED'
        },
        safety: {
            estop: false,
            alarm: false
        },
        vfd: {
            current_amps: 0,
            frequency_hz: 0,
            thermal_percent: 0,
            fault_code: 0,
            stall_threshold: 0,
            calibration_valid: false
        },
        axis: {
            x: { quality: 0, jitter_mms: 0, stalled: false, vfd_error_percent: 0 },
            y: { quality: 0, jitter_mms: 0, stalled: false, vfd_error_percent: 0 },
            z: { quality: 0, jitter_mms: 0, stalled: false, vfd_error_percent: 0 }
        },
        encoders: [],
        network: {
            wifi_connected: false,
            signal_percent: 0
        },
        load_state: 0,
        performance: { tasks: [] }
    };

    static listeners = [];
    static history = [];
    static maxHistory = 1440; // 24 hours @ 1 sample/min

    static update(newData) {
        // Deep merge
        this.data = this.deepMerge(this.data, newData);

        // Record history (sample every 10 updates to save memory)
        if (Math.random() < 0.1) {
            this.recordHistory();
        }

        this.notifyListeners('state-changed');
    }

    static get(path) {
        return this.getNestedValue(this.data, path);
    }

    static set(path, value) {
        this.setNestedValue(this.data, path, value);
        this.notifyListeners('state-changed');
    }

    static subscribe(callback) {
        this.listeners.push(callback);
        return () => {
            this.listeners = this.listeners.filter(l => l !== callback);
        };
    }

    static deepMerge(target, source) {
        const result = { ...target };
        for (const key in source) {
            if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
                result[key] = this.deepMerge(target[key] || {}, source[key]);
            } else {
                result[key] = source[key];
            }
        }
        return result;
    }

    static getNestedValue(obj, path) {
        return path.split('.').reduce((curr, prop) => curr?.[prop], obj);
    }

    static setNestedValue(obj, path, value) {
        const keys = path.split('.');
        const lastKey = keys.pop();
        const target = keys.reduce((curr, prop) => curr[prop] = curr[prop] || {}, obj);
        target[lastKey] = value;
    }

    static recordHistory() {
        this.history.push({
            timestamp: Date.now(),
            data: JSON.parse(JSON.stringify(this.data))
        });

        if (this.history.length > this.maxHistory) {
            this.history.shift();
        }
    }

    static getHistory(minutes = 60) {
        const cutoff = Date.now() - minutes * 60 * 1000;
        return this.history.filter(h => h.timestamp > cutoff);
    }

    static notifyListeners(event) {
        window.dispatchEvent(new CustomEvent(event, { detail: this.data }));
    }

    static reset() {
        this.data = { ...this.constructor.data };
        this.history = [];
        this.notifyListeners('state-reset');
    }
}

// Listen for telemetry updates
window.addEventListener('telemetry', (event) => {
    AppState.update(event.detail);
});
