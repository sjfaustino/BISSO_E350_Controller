/**
 * @file shared/alerts.js
 * @brief Alert management system for all pages
 */

class AlertManager {
    static alerts = [];
    static maxAlerts = 100;
    static listeners = [];

    static add(message, type = 'info', duration = null) {
        const alert = {
            id: Date.now(),
            message,
            type, // 'info', 'success', 'warning', 'critical'
            timestamp: new Date(),
            duration
        };

        this.alerts.unshift(alert);

        if (this.alerts.length > this.maxAlerts) {
            this.alerts.pop();
        }

        this.notifyListeners('alert-added', alert);

        // Auto-dismiss if duration specified
        if (duration) {
            setTimeout(() => this.remove(alert.id), duration);
        }

        // Play sound for critical alerts
        if (type === 'critical') {
            this.playSound('critical');
        }

        return alert.id;
    }

    static remove(id) {
        const index = this.alerts.findIndex(a => a.id === id);
        if (index !== -1) {
            const removed = this.alerts.splice(index, 1)[0];
            this.notifyListeners('alert-removed', removed);
        }
    }

    static clear() {
        this.alerts = [];
        this.notifyListeners('alerts-cleared');
    }

    static getAll() {
        return this.alerts;
    }

    static subscribe(callback) {
        this.listeners.push(callback);
        return () => {
            this.listeners = this.listeners.filter(l => l !== callback);
        };
    }

    static notifyListeners(event, data) {
        window.dispatchEvent(new CustomEvent(event, { detail: data }));
    }

    static playSound(type) {
        // Simple beep using Web Audio API
        try {
            const audioContext = new (window.AudioContext || window.webkitAudioContext)();
            const oscillator = audioContext.createOscillator();
            const gainNode = audioContext.createGain();

            oscillator.connect(gainNode);
            gainNode.connect(audioContext.destination);

            const frequency = type === 'critical' ? 1000 : 500;
            const duration = type === 'critical' ? 0.2 : 0.1;

            oscillator.frequency.value = frequency;
            gainNode.gain.setValueAtTime(0.3, audioContext.currentTime);
            gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + duration);

            oscillator.start(audioContext.currentTime);
            oscillator.stop(audioContext.currentTime + duration);
        } catch (e) {
            console.log('[ALERT] Cannot play sound:', e.message);
        }
    }
}

// Listen for motion stalls and quality drops
window.addEventListener('state-changed', (event) => {
    const state = event.detail;

    // Check for axis stalls
    if (state.axis) {
        ['x', 'y', 'z'].forEach((axis) => {
            const metrics = state.axis[axis];
            if (metrics && metrics.stalled) {
                AlertManager.add(
                    `Axis ${axis.toUpperCase()} STALLED`,
                    'critical',
                    null
                );
            }
            if (metrics && metrics.quality < 25) {
                AlertManager.add(
                    `Axis ${axis.toUpperCase()} quality critical: ${metrics.quality}%`,
                    'warning',
                    5000
                );
            }
        });
    }

    // Check for VFD faults
    if (state.vfd && state.vfd.fault_code !== 0) {
        AlertManager.add(
            `VFD Fault Code: 0x${state.vfd.fault_code.toString(16).toUpperCase()}`,
            'warning',
            10000
        );
    }

    // Check for thermal warnings
    if (state.vfd && state.vfd.thermal_percent > 85) {
        AlertManager.add(
            `VFD Thermal Warning: ${state.vfd.thermal_percent}%`,
            'warning',
            5000
        );
    }
});
