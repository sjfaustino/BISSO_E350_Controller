/**
 * @file shared/audio-alerts.js
 * @brief W3: Browser Audio Alerts for job complete, faults, and warnings
 * @details Uses Web Audio API to generate tones without external sound files
 */

class AudioAlerts {
    static audioContext = null;
    static enabled = true;
    static volume = 0.3;
    static lastJobState = null;
    static lastAlarmState = false;
    static lastEstopState = false;
    static initialized = false;

    /**
     * Initialize the audio alert system
     */
    static init() {
        if (this.initialized) return;

        // Load user preferences from localStorage
        this.enabled = localStorage.getItem('audioAlertsEnabled') !== 'false';
        const savedVol = localStorage.getItem('audioAlertsVolume');
        if (savedVol) this.volume = parseFloat(savedVol);

        // Listen for telemetry updates
        window.addEventListener('telemetry', (e) => this.onTelemetry(e.detail));

        // Initialize audio context on first user interaction (browser requirement)
        document.addEventListener('click', () => this.ensureAudioContext(), { once: true });
        document.addEventListener('keydown', () => this.ensureAudioContext(), { once: true });

        console.log('[AudioAlerts] Initialized, enabled:', this.enabled);
        this.initialized = true;
    }

    /**
     * Create audio context (must be after user interaction in most browsers)
     */
    static ensureAudioContext() {
        if (!this.audioContext) {
            try {
                this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
                console.log('[AudioAlerts] AudioContext created');
            } catch (e) {
                console.warn('[AudioAlerts] Web Audio API not supported:', e);
            }
        }
        return this.audioContext;
    }

    /**
     * Handle telemetry updates - detect state changes
     */
    static onTelemetry(data) {
        if (!this.enabled || !data) return;

        // Check for job state changes
        const jobState = data.job?.state;
        if (jobState !== undefined && this.lastJobState !== null) {
            // Job completed (state 3 = COMPLETED)
            if (jobState === 3 && this.lastJobState !== 3) {
                this.playJobComplete();
            }
        }
        this.lastJobState = jobState;

        // Check for alarm/fault
        const alarmActive = data.safety?.alarm || false;
        if (alarmActive && !this.lastAlarmState) {
            this.playAlarm();
        }
        this.lastAlarmState = alarmActive;

        // Check for E-Stop
        const estopActive = data.safety?.estop || false;
        if (estopActive && !this.lastEstopState) {
            this.playEstop();
        }
        this.lastEstopState = estopActive;
    }

    /**
     * Play a tone using Web Audio API
     * @param {number} frequency - Frequency in Hz
     * @param {number} duration - Duration in seconds
     * @param {string} type - Oscillator type: sine, square, sawtooth, triangle
     */
    static playTone(frequency, duration, type = 'sine') {
        const ctx = this.ensureAudioContext();
        if (!ctx) return;

        try {
            const oscillator = ctx.createOscillator();
            const gainNode = ctx.createGain();

            oscillator.connect(gainNode);
            gainNode.connect(ctx.destination);

            oscillator.type = type;
            oscillator.frequency.setValueAtTime(frequency, ctx.currentTime);

            // Envelope: quick attack, sustain, quick release
            gainNode.gain.setValueAtTime(0, ctx.currentTime);
            gainNode.gain.linearRampToValueAtTime(this.volume, ctx.currentTime + 0.01);
            gainNode.gain.setValueAtTime(this.volume, ctx.currentTime + duration - 0.05);
            gainNode.gain.linearRampToValueAtTime(0, ctx.currentTime + duration);

            oscillator.start(ctx.currentTime);
            oscillator.stop(ctx.currentTime + duration);
        } catch (e) {
            console.warn('[AudioAlerts] Failed to play tone:', e);
        }
    }

    /**
     * Job Complete - Pleasant ascending two-tone chime
     */
    static playJobComplete() {
        console.log('[AudioAlerts] Playing job complete sound');
        this.playTone(523.25, 0.15, 'sine');  // C5
        setTimeout(() => this.playTone(659.25, 0.15, 'sine'), 150);  // E5
        setTimeout(() => this.playTone(783.99, 0.25, 'sine'), 300);  // G5

        // Show browser notification if permitted
        this.showNotification('Job Complete', 'Your cutting job has finished successfully.');
    }

    /**
     * Alarm/Fault - Urgent alternating tones
     */
    static playAlarm() {
        console.log('[AudioAlerts] Playing alarm sound');
        for (let i = 0; i < 3; i++) {
            setTimeout(() => {
                this.playTone(880, 0.1, 'square');  // A5
                setTimeout(() => this.playTone(440, 0.1, 'square'), 100);  // A4
            }, i * 300);
        }

        this.showNotification('⚠️ ALARM', 'Machine alarm triggered! Check controller.', true);
    }

    /**
     * E-Stop - Continuous urgent tone
     */
    static playEstop() {
        console.log('[AudioAlerts] Playing E-Stop sound');
        for (let i = 0; i < 5; i++) {
            setTimeout(() => this.playTone(1000, 0.08, 'sawtooth'), i * 100);
        }

        this.showNotification('🛑 E-STOP', 'Emergency stop activated!', true);
    }

    /**
     * Warning beep (for blade wear, etc.)
     */
    static playWarning() {
        this.playTone(600, 0.2, 'triangle');
        setTimeout(() => this.playTone(600, 0.2, 'triangle'), 300);
    }

    /**
     * Show browser notification (requires permission)
     */
    static showNotification(title, body, urgent = false) {
        if (!('Notification' in window)) return;

        if (Notification.permission === 'granted') {
            new Notification(title, {
                body: body,
                icon: '/favicon.png',
                tag: urgent ? 'urgent-alert' : 'info-alert',
                requireInteraction: urgent
            });
        } else if (Notification.permission !== 'denied') {
            Notification.requestPermission().then(permission => {
                if (permission === 'granted') {
                    this.showNotification(title, body, urgent);
                }
            });
        }
    }

    /**
     * Enable/disable audio alerts
     */
    static setEnabled(enabled) {
        this.enabled = enabled;
        localStorage.setItem('audioAlertsEnabled', enabled);
        console.log('[AudioAlerts] Enabled:', enabled);
    }

    /**
     * Set volume (0.0 to 1.0)
     */
    static setVolume(volume) {
        this.volume = Math.max(0, Math.min(1, volume));
        localStorage.setItem('audioAlertsVolume', this.volume);
    }

    /**
     * Test sounds (for settings page)
     */
    static testJobComplete() { this.playJobComplete(); }
    static testAlarm() { this.playAlarm(); }
    static testWarning() { this.playWarning(); }
}

// Auto-initialize on load
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => AudioAlerts.init());
} else {
    AudioAlerts.init();
}

// Export for use in other modules
window.AudioAlerts = AudioAlerts;
