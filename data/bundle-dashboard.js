'use strict';

// --- shared/toast.js ---
;/**
 * Toast Notification System
 * Displays temporary notifications to the user
 */

const Toast = {
    container: null,
    queue: [],
    maxToasts: 5,

    /**
     * Initialize the toast system
     */
    init() {
        if (this.container) return; // Already initialized

        // Create toast container
        this.container = document.createElement('div');
        this.container.id = 'toast-container';
        this.container.className = 'toast-container';
        document.body.appendChild(this.container);

        console.log('[Toast] Notification system initialized');
    },

    /**
     * Show a toast notification
     * @param {string} message - Message to display
     * @param {string} type - Type: 'success', 'error', 'warning', 'info'
     * @param {number} duration - Duration in ms (0 = persistent)
     * @returns {HTMLElement} - The toast element
     */
    show(message, type = 'info', duration = 3000) {
        if (!this.container) this.init();

        // Create toast element
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;

        // Icon based on type
        const icons = {
            success: '✓',
            error: '✕',
            warning: '⚠',
            info: 'ℹ'
        };
        const icon = icons[type] || icons.info;

        toast.innerHTML = `
            <div class="toast-icon">${icon}</div>
            <div class="toast-message">${Utils.escapeHtml(message)}</div>
            <button class="toast-close" aria-label="Close">×</button>
        `;

        // Close button
        const closeBtn = toast.querySelector('.toast-close');
        closeBtn.addEventListener('click', () => this.remove(toast));

        // Add to container
        this.container.appendChild(toast);

        // Trigger animation
        requestAnimationFrame(() => {
            toast.classList.add('toast-show');
        });

        // Auto-remove after duration
        if (duration > 0) {
            setTimeout(() => this.remove(toast), duration);
        }

        // Limit number of toasts
        this.limitToasts();

        return toast;
    },

    /**
     * Show success toast
     * @param {string} message - Message to display
     * @param {number} duration - Duration in ms
     */
    success(message, duration = 3000) {
        return this.show(message, 'success', duration);
    },

    /**
     * Show error toast
     * @param {string} message - Message to display
     * @param {number} duration - Duration in ms (0 = persistent)
     */
    error(message, duration = 5000) {
        return this.show(message, 'error', duration);
    },

    /**
     * Show warning toast
     * @param {string} message - Message to display
     * @param {number} duration - Duration in ms
     */
    warning(message, duration = 4000) {
        return this.show(message, 'warning', duration);
    },

    /**
     * Show info toast
     * @param {string} message - Message to display
     * @param {number} duration - Duration in ms
     */
    info(message, duration = 3000) {
        return this.show(message, 'info', duration);
    },

    /**
     * Remove a toast
     * @param {HTMLElement} toast - Toast element to remove
     */
    remove(toast) {
        if (!toast || !toast.parentNode) return;

        toast.classList.remove('toast-show');
        toast.classList.add('toast-hide');

        setTimeout(() => {
            if (toast.parentNode) {
                toast.parentNode.removeChild(toast);
            }
        }, 300); // Match CSS transition duration
    },

    /**
     * Remove all toasts
     */
    clear() {
        if (!this.container) return;

        const toasts = this.container.querySelectorAll('.toast');
        toasts.forEach(toast => this.remove(toast));
    },

    /**
     * Limit number of visible toasts
     */
    limitToasts() {
        if (!this.container) return;

        const toasts = this.container.querySelectorAll('.toast');
        if (toasts.length > this.maxToasts) {
            // Remove oldest toasts
            const toRemove = Array.from(toasts).slice(0, toasts.length - this.maxToasts);
            toRemove.forEach(toast => this.remove(toast));
        }
    }
};

// Auto-initialize when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => Toast.init());
} else {
    Toast.init();
}

// Expose globally
window.Toast = Toast;


// --- shared/alerts.js ---
;/**
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


// --- shared/websocket.js ---
;/**
 * @file shared/websocket.js
 * @brief Shared WebSocket connection for all pages
 * @details Single connection reused across all modules
 */

class SharedWebSocket {
    static ws = null;
    static isConnected = false;
    static reconnectAttempts = 0;
    static maxReconnectAttempts = 5;
    static reconnectDelay = 3000;
    static listeners = [];

    static connect() {
        if (this.ws) return this.ws;

        try {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = `${protocol}//${window.location.hostname}:${window.location.port}/ws`;

            this.ws = new WebSocket(wsUrl);

            this.ws.onopen = () => {
                this.isConnected = true;
                this.reconnectAttempts = 0;
                console.log('[WS] Connected');
                this.broadcast('ws-connected');
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.broadcast('telemetry', data);
                } catch (e) {
                    console.error('[WS] Parse error:', e);
                }
            };

            this.ws.onerror = (error) => {
                console.error('[WS] Error:', error);
                this.broadcast('ws-error', error);
            };

            this.ws.onclose = () => {
                this.isConnected = false;
                console.log('[WS] Disconnected');
                this.broadcast('ws-disconnected');
                this.attemptReconnect();
            };

            return this.ws;
        } catch (e) {
            console.error('[WS] Connection failed:', e);
            this.attemptReconnect();
            return null;
        }
    }

    static attemptReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.error('[WS] Max reconnection attempts reached');
            this.broadcast('ws-failed');
            return;
        }

        this.reconnectAttempts++;
        console.log(`[WS] Reconnecting in ${this.reconnectDelay}ms (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`);

        setTimeout(() => this.connect(), this.reconnectDelay);
    }

    static send(message) {
        if (this.ws && this.isConnected) {
            this.ws.send(JSON.stringify(message));
            return true;
        }
        console.warn('[WS] Not connected, message not sent');
        return false;
    }

    static subscribe(callback) {
        this.listeners.push(callback);
        return () => {
            this.listeners = this.listeners.filter(l => l !== callback);
        };
    }

    static broadcast(event, data = null) {
        window.dispatchEvent(new CustomEvent(event, { detail: data }));
    }

    static disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
}

// Auto-connect on load (skip for file:// protocol - mock mode will handle it)
if (window.location.protocol !== 'file:') {
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => SharedWebSocket.connect());
    } else {
        SharedWebSocket.connect();
    }
}


// --- shared/safety.js ---
;/**
 * @file shared/safety.js
 * @brief Global Safety Components - Alarm Banner and E-Stop Button
 */

class SafetyManager {
    static alarmBanner = null;
    static estopButton = null;
    static alarmCheckInterval = null;
    static currentAlarms = [];
    static estopActive = false;

    static init() {
        this.createAlarmBanner();
        this.createEstopButton();
        this.startAlarmMonitoring();
        console.log('[SAFETY] Manager initialized');
    }

    static createAlarmBanner() {
        // Create alarm banner element (hidden by default)
        const banner = document.createElement('div');
        banner.id = 'alarm-banner';
        banner.className = 'alarm-banner';
        banner.style.display = 'none';
        banner.innerHTML = `
            <div class="alarm-content">
                <div class="alarm-icon">⚠️</div>
                <div class="alarm-details">
                    <div class="alarm-title">SYSTEM ALARM</div>
                    <div class="alarm-message" id="alarm-message"></div>
                </div>
                <button class="alarm-dismiss" id="alarm-dismiss" title="Dismiss">✕</button>
            </div>
        `;

        document.body.appendChild(banner);
        this.alarmBanner = banner;

        // Dismiss button
        document.getElementById('alarm-dismiss')?.addEventListener('click', () => {
            this.dismissAlarm();
        });
    }

    static createEstopButton() {
        // Create floating E-stop button
        const button = document.createElement('div');
        button.id = 'estop-button';
        button.className = 'estop-button';
        button.innerHTML = `
            <button class="estop-btn" id="estop-trigger" title="Emergency Stop">
                <div class="estop-icon">⏹</div>
                <div class="estop-label">E-STOP</div>
            </button>
        `;

        document.body.appendChild(button);
        this.estopButton = button;

        // E-stop trigger
        document.getElementById('estop-trigger')?.addEventListener('click', () => {
            this.confirmEstop();
        });
    }

    static startAlarmMonitoring() {
        // Check for alarms every 2 seconds
        this.alarmCheckInterval = setInterval(() => {
            this.checkAlarms();
        }, 2000);

        // Also check immediately
        this.checkAlarms();
    }

    static async checkAlarms() {
        try {
            // Skip API calls in file:// mode or mock mode - no real device to check
            if (window.location.protocol === 'file:' || window.MockMode?.enabled) {
                // In mock mode, assume no alarms for simplicity
                this.estopActive = false;
                this.updateEstopButton();
                this.hideAlarm();
                return;
            }

            // PHASE 5.10: Removed hardcoded credentials - browser handles auth via 401 prompt
            const response = await fetch('/api/alarms', {
                credentials: 'same-origin' // Include auth credentials from browser
            });

            if (!response.ok) return;

            const data = await response.json();

            // Update E-stop state
            this.estopActive = data.estop_active;
            this.updateEstopButton();

            // Update alarm banner
            if (data.estop_active) {
                this.showAlarm('EMERGENCY STOP ACTIVE', 'critical');
            } else if (data.faults && data.faults.length > 0) {
                // Show most recent critical/error fault
                const criticalFault = data.faults.find(f => f.severity === 'CRITICAL' || f.severity === 'ERROR');
                if (criticalFault) {
                    this.showAlarm(
                        `${criticalFault.code}: ${criticalFault.message}`,
                        criticalFault.severity === 'CRITICAL' ? 'critical' : 'warning'
                    );
                }
            } else {
                this.hideAlarm();
            }

            this.currentAlarms = data.faults || [];

        } catch (error) {
            console.error('[SAFETY] Alarm check error:', error);
        }
    }

    static showAlarm(message, severity = 'warning') {
        if (!this.alarmBanner) return;

        const messageEl = document.getElementById('alarm-message');
        if (messageEl) messageEl.textContent = message;

        this.alarmBanner.className = `alarm-banner alarm-${severity}`;
        this.alarmBanner.style.display = 'block';

        // Flash effect for critical alarms
        if (severity === 'critical') {
            this.alarmBanner.classList.add('alarm-flash');
        } else {
            this.alarmBanner.classList.remove('alarm-flash');
        }
    }

    static hideAlarm() {
        if (this.alarmBanner) {
            this.alarmBanner.style.display = 'none';
        }
    }

    static dismissAlarm() {
        // Only allow dismissing non-E-stop alarms
        if (!this.estopActive) {
            this.hideAlarm();
        } else {
            AlertManager.add('Cannot dismiss - E-stop active! Reset E-stop first.', 'critical', 3000);
        }
    }

    static updateEstopButton() {
        const btn = document.getElementById('estop-trigger');
        if (!btn) return;

        if (this.estopActive) {
            btn.classList.add('estop-active');
            btn.querySelector('.estop-label').textContent = 'RESET';
            btn.title = 'Reset Emergency Stop';
        } else {
            btn.classList.remove('estop-active');
            btn.querySelector('.estop-label').textContent = 'E-STOP';
            btn.title = 'Emergency Stop';
        }
    }

    static confirmEstop() {
        if (this.estopActive) {
            // Reset E-stop
            if (confirm('Reset Emergency Stop?\n\nEnsure it is safe to resume operation.')) {
                this.resetEstop();
            }
        } else {
            // Trigger E-stop
            if (confirm('Trigger Emergency Stop?\n\nThis will halt all motion immediately.')) {
                this.triggerEstop();
            }
        }
    }

    static async triggerEstop() {
        // In file:// or mock mode, these are non-functional
        if (window.location.protocol === 'file:' || window.MockMode?.enabled) {
            AlertManager.add('E-stop not available in offline mode', 'warning', 3000);
            return;
        }

        try {
            // PHASE 5.10: Removed hardcoded credentials - browser handles auth via 401 prompt
            const response = await fetch('/api/estop/trigger', {
                method: 'POST',
                credentials: 'same-origin' // Include auth credentials from browser
            });

            if (response.ok) {
                AlertManager.add('Emergency Stop Activated', 'critical', 5000);
                this.checkAlarms(); // Immediate update
            } else {
                AlertManager.add('Failed to trigger E-stop', 'critical', 3000);
            }
        } catch (error) {
            console.error('[SAFETY] E-stop trigger error:', error);
            AlertManager.add('Communication error', 'critical', 3000);
        }
    }

    static async resetEstop() {
        // In file:// or mock mode, these are non-functional
        if (window.location.protocol === 'file:' || window.MockMode?.enabled) {
            AlertManager.add('E-stop reset not available in offline mode', 'warning', 3000);
            return;
        }

        try {
            // PHASE 5.10: Removed hardcoded credentials - browser handles auth via 401 prompt
            const response = await fetch('/api/estop/reset', {
                method: 'POST',
                credentials: 'same-origin' // Include auth credentials from browser
            });

            const data = await response.json();

            if (data.success) {
                AlertManager.add('Emergency Stop Reset - Operation can resume', 'success', 3000);
                this.checkAlarms(); // Immediate update
            } else {
                AlertManager.add('E-stop reset failed - Check system state', 'critical', 3000);
            }
        } catch (error) {
            console.error('[SAFETY] E-stop reset error:', error);
            AlertManager.add('Communication error', 'critical', 3000);
        }
    }

    static cleanup() {
        if (this.alarmCheckInterval) {
            clearInterval(this.alarmCheckInterval);
        }
    }
}

// CSS styles for safety components (inject into page)
const safetyStyles = `
<style>
.alarm-banner {
    position: fixed;
    top: 60px;
    left: 0;
    right: 0;
    z-index: 9999;
    padding: 15px 20px;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    animation: slideDown 0.3s ease;
}

.alarm-banner.alarm-critical {
    background: var(--color-critical);
    color: white;
}

.alarm-banner.alarm-warning {
    background: var(--color-warning);
    color: white;
}

.alarm-content {
    display: flex;
    align-items: center;
    gap: 15px;
    max-width: 1200px;
    margin: 0 auto;
}

.alarm-icon {
    font-size: 32px;
    flex-shrink: 0;
}

.alarm-details {
    flex: 1;
}

.alarm-title {
    font-weight: bold;
    font-size: 16px;
    margin-bottom: 4px;
    letter-spacing: 1px;
}

.alarm-message {
    font-size: 14px;
    opacity: 0.95;
}

.alarm-dismiss {
    background: rgba(255, 255, 255, 0.2);
    border: none;
    color: white;
    width: 30px;
    height: 30px;
    border-radius: 50%;
    cursor: pointer;
    font-size: 20px;
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
}

.alarm-dismiss:hover {
    background: rgba(255, 255, 255, 0.3);
}

.alarm-flash {
    animation: flash 1s infinite;
}

@keyframes flash {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.7; }
}

@keyframes slideDown {
    from {
        transform: translateY(-100%);
        opacity: 0;
    }
    to {
        transform: translateY(0);
        opacity: 1;
    }
}

.estop-button {
    position: fixed;
    bottom: 30px;
    right: 30px;
    z-index: 9998;
}

.estop-btn {
    background: var(--color-critical);
    color: white;
    border: 3px solid #fff;
    border-radius: 50%;
    width: 90px;
    height: 90px;
    cursor: pointer;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    box-shadow: 0 4px 16px rgba(220, 38, 38, 0.5);
    transition: all 0.2s ease;
}

.estop-btn:hover {
    transform: scale(1.05);
    box-shadow: 0 6px 20px rgba(220, 38, 38, 0.7);
}

.estop-btn:active {
    transform: scale(0.95);
}

.estop-btn.estop-active {
    background: var(--color-warning);
    animation: pulse 1.5s infinite;
}

.estop-icon {
    font-size: 32px;
    line-height: 1;
}

.estop-label {
    font-size: 11px;
    font-weight: bold;
    margin-top: 4px;
    letter-spacing: 0.5px;
}

@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.6; }
}

@media (max-width: 768px) {
    .alarm-banner {
        top: 50px;
        padding: 12px 15px;
    }

    .alarm-icon {
        font-size: 24px;
    }

    .alarm-title {
        font-size: 14px;
    }

    .alarm-message {
        font-size: 12px;
    }

    .estop-button {
        bottom: 20px;
        right: 20px;
    }

    .estop-btn {
        width: 70px;
        height: 70px;
    }

    .estop-icon {
        font-size: 24px;
    }

    .estop-label {
        font-size: 9px;
    }
}
</style>
`;

// Inject styles
document.head.insertAdjacentHTML('beforeend', safetyStyles);

// Auto-initialize
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => SafetyManager.init());
} else {
    SafetyManager.init();
}

window.SafetyManager = SafetyManager;


// --- shared/mini-charts.js ---
;/**
 * Mini Charts - Lightweight Real-time Charting for Mock Mode
 * Works with minimal DOM and provides real-time visualization
 */

class MiniChart {
    constructor(containerId, options = {}) {
        this.container = document.getElementById(containerId);
        if (!this.container) {
            console.warn(`[MiniChart] Container ${containerId} not found`);
            return;
        }

        this.options = {
            width: options.width || 300,
            height: options.height || 150,
            maxDataPoints: options.maxDataPoints || 60,
            lineColor: options.lineColor || '#3b82f6',
            fillColor: options.fillColor || 'rgba(59, 130, 246, 0.1)',
            gridColor: options.gridColor || 'rgba(128, 128, 128, 0.2)',
            textColor: options.textColor || '#666',
            min: options.min !== undefined ? options.min : 'auto',
            max: options.max !== undefined ? options.max : 'auto',
            unit: options.unit || '',
            showGrid: options.showGrid !== false,
            showValues: options.showValues !== false,
            smooth: options.smooth !== false
        };

        this.data = [];
        this.canvas = null;
        this.ctx = null;
        this.animationFrame = null;

        this.init();
    }

    init() {
        // Create canvas
        this.canvas = document.createElement('canvas');
        this.canvas.width = this.options.width;
        this.canvas.height = this.options.height;
        this.canvas.style.width = '100%';
        this.canvas.style.height = 'auto';
        this.canvas.style.display = 'block';

        this.ctx = this.canvas.getContext('2d');
        this.container.appendChild(this.canvas);

        // Create value display if enabled
        if (this.options.showValues) {
            this.valueDisplay = document.createElement('div');
            this.valueDisplay.style.cssText = `
                margin-top: 8px;
                display: flex;
                justify-content: space-between;
                font-size: 12px;
                color: var(--text-secondary);
            `;
            this.container.appendChild(this.valueDisplay);
        }

        this.render();
    }

    addDataPoint(value) {
        this.data.push(value);

        // Keep only recent data
        if (this.data.length > this.options.maxDataPoints) {
            this.data.shift();
        }

        this.render();
    }

    render() {
        if (!this.ctx) return;

        const { width, height } = this.canvas;
        const ctx = this.ctx;

        // Clear canvas
        ctx.clearRect(0, 0, width, height);

        if (this.data.length < 2) return;

        // Calculate bounds
        let min = this.options.min === 'auto' ? Math.min(...this.data) : this.options.min;
        let max = this.options.max === 'auto' ? Math.max(...this.data) : this.options.max;

        // Add padding to range
        const range = max - min;
        if (this.options.min === 'auto') min -= range * 0.1;
        if (this.options.max === 'auto') max += range * 0.1;

        // Prevent division by zero
        if (max === min) {
            max = min + 1;
        }

        // Draw grid
        if (this.options.showGrid) {
            this.drawGrid(ctx, width, height, min, max);
        }

        // Draw line
        this.drawLine(ctx, width, height, min, max);

        // Update value display
        if (this.options.showValues && this.valueDisplay) {
            const current = this.data[this.data.length - 1];
            const avg = this.data.reduce((a, b) => a + b, 0) / this.data.length;

            this.valueDisplay.innerHTML = `
                <span>Current: <strong>${current.toFixed(1)}${this.options.unit}</strong></span>
                <span>Avg: <strong>${avg.toFixed(1)}${this.options.unit}</strong></span>
                <span>Max: <strong>${Math.max(...this.data).toFixed(1)}${this.options.unit}</strong></span>
            `;
        }
    }

    drawGrid(ctx, width, height, min, max) {
        ctx.strokeStyle = this.options.gridColor;
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 2]);

        // Horizontal grid lines (4 lines)
        for (let i = 0; i <= 4; i++) {
            const y = (height / 4) * i;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }

        ctx.setLineDash([]);
    }

    drawLine(ctx, width, height, min, max) {
        const range = max - min;
        const stepX = width / (this.options.maxDataPoints - 1);

        // Build path
        ctx.beginPath();

        this.data.forEach((value, index) => {
            const x = index * stepX;
            const y = height - ((value - min) / range) * height;

            if (index === 0) {
                ctx.moveTo(x, y);
            } else {
                if (this.options.smooth) {
                    // Smooth curve using quadratic bezier
                    const prevX = (index - 1) * stepX;
                    const prevY = height - ((this.data[index - 1] - min) / range) * height;
                    const cpX = (prevX + x) / 2;
                    const cpY = (prevY + y) / 2;
                    ctx.quadraticCurveTo(prevX, prevY, cpX, cpY);
                } else {
                    ctx.lineTo(x, y);
                }
            }
        });

        // Draw filled area
        if (this.options.fillColor) {
            ctx.lineTo(width, height);
            ctx.lineTo(0, height);
            ctx.closePath();
            ctx.fillStyle = this.options.fillColor;
            ctx.fill();
        }

        // Draw line
        ctx.strokeStyle = this.options.lineColor;
        ctx.lineWidth = 2;
        ctx.stroke();
    }

    destroy() {
        if (this.animationFrame) {
            cancelAnimationFrame(this.animationFrame);
        }
        if (this.canvas && this.canvas.parentNode) {
            this.canvas.parentNode.removeChild(this.canvas);
        }
        if (this.valueDisplay && this.valueDisplay.parentNode) {
            this.valueDisplay.parentNode.removeChild(this.valueDisplay);
        }
    }
}

/**
 * Multi-Series Chart for comparing multiple metrics
 */
class MiniMultiChart {
    constructor(containerId, options = {}) {
        this.container = document.getElementById(containerId);
        if (!this.container) {
            console.warn(`[MiniMultiChart] Container ${containerId} not found`);
            return;
        }

        this.options = {
            width: options.width || 400,
            height: options.height || 200,
            maxDataPoints: options.maxDataPoints || 60,
            series: options.series || [], // Array of {name, color, data}
            gridColor: options.gridColor || 'rgba(128, 128, 128, 0.2)',
            textColor: options.textColor || '#666',
            min: options.min !== undefined ? options.min : 'auto',
            max: options.max !== undefined ? options.max : 'auto',
            showGrid: options.showGrid !== false,
            showLegend: options.showLegend !== false
        };

        this.seriesData = new Map(); // Map of series name to data array
        this.canvas = null;
        this.ctx = null;

        this.init();
    }

    init() {
        // Create canvas
        this.canvas = document.createElement('canvas');
        this.canvas.width = this.options.width;
        this.canvas.height = this.options.height;
        this.canvas.style.width = '100%';
        this.canvas.style.height = 'auto';
        this.canvas.style.display = 'block';

        this.ctx = this.canvas.getContext('2d');
        this.container.appendChild(this.canvas);

        // Create legend if enabled
        if (this.options.showLegend) {
            this.legend = document.createElement('div');
            this.legend.style.cssText = `
                margin-top: 8px;
                display: flex;
                gap: 16px;
                flex-wrap: wrap;
                font-size: 12px;
            `;
            this.container.appendChild(this.legend);
        }

        // Initialize series
        this.options.series.forEach(s => {
            this.seriesData.set(s.name, []);
        });

        this.render();
    }

    addDataPoint(seriesName, value) {
        if (!this.seriesData.has(seriesName)) {
            console.warn(`[MiniMultiChart] Series ${seriesName} not found`);
            return;
        }

        const data = this.seriesData.get(seriesName);
        data.push(value);

        // Keep only recent data
        if (data.length > this.options.maxDataPoints) {
            data.shift();
        }

        this.render();
    }

    render() {
        if (!this.ctx) return;

        const { width, height } = this.canvas;
        const ctx = this.ctx;

        // Clear canvas
        ctx.clearRect(0, 0, width, height);

        // Calculate global bounds across all series
        let allValues = [];
        this.seriesData.forEach(data => allValues.push(...data));

        if (allValues.length < 2) return;

        let min = this.options.min === 'auto' ? Math.min(...allValues) : this.options.min;
        let max = this.options.max === 'auto' ? Math.max(...allValues) : this.options.max;

        const range = max - min;
        if (this.options.min === 'auto') min -= range * 0.1;
        if (this.options.max === 'auto') max += range * 0.1;

        if (max === min) max = min + 1;

        // Draw grid
        if (this.options.showGrid) {
            this.drawGrid(ctx, width, height);
        }

        // Draw each series
        this.options.series.forEach(series => {
            const data = this.seriesData.get(series.name);
            if (data && data.length > 0) {
                this.drawSeries(ctx, width, height, min, max, data, series.color);
            }
        });

        // Update legend
        if (this.options.showLegend && this.legend) {
            this.updateLegend();
        }
    }

    drawGrid(ctx, width, height) {
        ctx.strokeStyle = this.options.gridColor;
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 2]);

        for (let i = 0; i <= 4; i++) {
            const y = (height / 4) * i;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }

        ctx.setLineDash([]);
    }

    drawSeries(ctx, width, height, min, max, data, color) {
        const range = max - min;
        const stepX = width / (this.options.maxDataPoints - 1);

        ctx.beginPath();

        data.forEach((value, index) => {
            const x = index * stepX;
            const y = height - ((value - min) / range) * height;

            if (index === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        });

        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.stroke();
    }

    updateLegend() {
        this.legend.innerHTML = this.options.series.map(series => {
            const data = this.seriesData.get(series.name);
            const current = data.length > 0 ? data[data.length - 1] : 0;

            return `
                <div style="display: flex; align-items: center; gap: 6px;">
                    <div style="width: 12px; height: 12px; background: ${series.color}; border-radius: 2px;"></div>
                    <span style="color: var(--text-secondary);">${series.name}: <strong>${current.toFixed(1)}</strong></span>
                </div>
            `;
        }).join('');
    }

    destroy() {
        if (this.canvas && this.canvas.parentNode) {
            this.canvas.parentNode.removeChild(this.canvas);
        }
        if (this.legend && this.legend.parentNode) {
            this.legend.parentNode.removeChild(this.legend);
        }
    }
}

// Expose globally
window.MiniChart = MiniChart;
window.MiniMultiChart = MiniMultiChart;


// --- shared/graphs.js ---
;/**
 * Advanced Graph Visualizer
 * Real-time line charts with multiple data series, auto-scaling, and responsive design
 */

class GraphVisualizer {
    constructor(canvasId, options = {}) {
        this.canvas = document.getElementById(canvasId);

        // Initialize essential properties BEFORE canvas check to prevent broken objects
        this.series = new Map();
        this.seriesColors = ['#10b981', '#3b82f6', '#f59e0b', '#ef4444', '#8b5cf6', '#ec4899'];

        // Configuration - initialize early so methods don't crash
        this.config = {
            title: options.title || 'Graph',
            timeWindow: options.timeWindow || 300000, // 5 minutes in ms
            maxPoints: options.maxPoints || 300,
            updateInterval: options.updateInterval || 1000,
            yMin: options.yMin || 0,
            yMax: options.yMax || 100,
            autoScale: options.autoScale !== false,
            unit: options.unit || '%',
            showGrid: options.showGrid !== false,
            showLegend: options.showLegend !== false,
            ...options
        };

        if (!this.canvas) {
            console.error(`[Graph] Canvas element '${canvasId}' not found`);
            // Don't return - object is initialized enough that methods won't crash
            this.isDisabled = true;
            return;
        }

        this.ctx = this.canvas.getContext('2d');
        this.width = this.canvas.width;
        this.height = this.canvas.height;

        // Data storage - already initialized above
        // this.series = new Map(); // Moved to top
        this.colors = {
            bg: getComputedStyle(document.documentElement).getPropertyValue('--bg-primary') || '#ffffff',
            grid: getComputedStyle(document.documentElement).getPropertyValue('--border-color') || '#e5e7eb',
            text: getComputedStyle(document.documentElement).getPropertyValue('--text-primary') || '#000000',
            optimal: getComputedStyle(document.documentElement).getPropertyValue('--color-optimal') || '#10b981',
            normal: getComputedStyle(document.documentElement).getPropertyValue('--color-normal') || '#3b82f6',
            warning: getComputedStyle(document.documentElement).getPropertyValue('--color-warning') || '#f59e0b',
            critical: getComputedStyle(document.documentElement).getPropertyValue('--color-critical') || '#ef4444'
        };

        // Default series colors
        this.seriesColors = [
            this.colors.optimal,
            this.colors.normal,
            this.colors.warning,
            this.colors.critical,
            '#8b5cf6',
            '#ec4899'
        ];

        // Handle resize
        this.resizeObserver = new ResizeObserver(() => this.handleResize());
        this.resizeObserver.observe(this.canvas);

        // Animation
        this.animationFrame = null;
        this.lastDrawTime = 0;

        // Initial draw
        this.draw();
    }

    handleResize() {
        const rect = this.canvas.parentElement.getBoundingClientRect();
        this.canvas.width = rect.width;
        this.canvas.height = rect.height;
        this.width = this.canvas.width;
        this.height = this.canvas.height;
        this.draw();
    }

    addSeries(seriesName, color = null) {
        if (!this.series.has(seriesName)) {
            const colorIndex = this.series.size % this.seriesColors.length;
            this.series.set(seriesName, {
                data: [],
                color: color || this.seriesColors[colorIndex],
                visible: true
            });
        }
    }

    addDataPoint(seriesName, value, timestamp = null) {
        if (!this.series.has(seriesName)) {
            this.addSeries(seriesName);
        }

        const series = this.series.get(seriesName);
        const time = timestamp || Date.now();

        series.data.push({ time, value });

        // Keep only recent data points
        const cutoffTime = time - this.config.timeWindow;
        series.data = series.data.filter(p => p.time >= cutoffTime);

        // Limit total points
        if (series.data.length > this.config.maxPoints) {
            series.data = series.data.slice(-this.config.maxPoints);
        }

        // Trigger redraw
        this.draw();
    }

    toggleSeries(seriesName, visible = null) {
        if (this.series.has(seriesName)) {
            const series = this.series.get(seriesName);
            series.visible = visible !== null ? visible : !series.visible;
            this.draw();
        }
    }

    clear() {
        this.series.forEach(series => {
            series.data = [];
        });
        this.draw();
    }

    clearSeries(seriesName) {
        if (this.series.has(seriesName)) {
            this.series.get(seriesName).data = [];
            this.draw();
        }
    }

    getMinMax() {
        let min = Infinity;
        let max = -Infinity;

        this.series.forEach((series) => {
            if (!series.visible) return;
            series.data.forEach(p => {
                min = Math.min(min, p.value);
                max = Math.max(max, p.value);
            });
        });

        // Apply configured min/max
        min = Math.min(min, this.config.yMin);
        max = Math.max(max, this.config.yMax);

        if (min === Infinity) min = 0;
        if (max === -Infinity) max = 100;

        // Add 10% padding
        const range = max - min;
        const padding = range * 0.1;

        return {
            min: Math.max(0, min - padding),
            max: max + padding
        };
    }

    valueToCanvasY(value, yMin, yMax) {
        const padding = 60; // pixels
        const graphHeight = this.height - padding - 40; // 40px for legend
        const normalized = (value - yMin) / (yMax - yMin);
        return padding + graphHeight - (normalized * graphHeight);
    }

    timeToCanvasX(time, minTime, maxTime) {
        const leftPadding = 50;
        const rightPadding = 20;
        const graphWidth = this.width - leftPadding - rightPadding;
        const normalized = (time - minTime) / (maxTime - minTime);
        return leftPadding + (normalized * graphWidth);
    }

    draw() {
        // Skip if canvas not available
        if (this.isDisabled || !this.canvas || !this.ctx) return;

        // Clear canvas
        this.ctx.fillStyle = this.colors.bg;
        this.ctx.fillRect(0, 0, this.width, this.height);

        // Get time range from visible data
        let minTime = Date.now() - this.config.timeWindow;
        let maxTime = Date.now();

        // Draw title
        this.drawTitle();

        // Draw grid
        if (this.config.showGrid) {
            this.drawGrid(minTime, maxTime);
        }

        // Get min/max values
        const { min: yMin, max: yMax } = this.getMinMax();

        // Draw Y-axis labels
        this.drawYAxisLabels(yMin, yMax);

        // Draw X-axis labels (time)
        this.drawXAxisLabels(minTime, maxTime);

        // Draw axes
        this.drawAxes();

        // Draw data series
        this.drawDataSeries(minTime, maxTime, yMin, yMax);

        // Draw legend
        if (this.config.showLegend) {
            this.drawLegend(yMin, yMax);
        }
    }

    drawTitle() {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = 'bold 14px Arial';
        this.ctx.textAlign = 'left';
        this.ctx.fillText(this.config.title, 10, 20);
    }

    drawGrid(minTime, maxTime) {
        this.ctx.strokeStyle = this.colors.grid;
        this.ctx.lineWidth = 1;
        this.ctx.globalAlpha = 0.3;

        // Vertical grid lines (time)
        const timeStep = 60000; // 1 minute
        for (let t = Math.ceil(minTime / timeStep) * timeStep; t <= maxTime; t += timeStep) {
            const x = this.timeToCanvasX(t, minTime, maxTime);
            this.ctx.beginPath();
            this.ctx.moveTo(x, 50);
            this.ctx.lineTo(x, this.height - 40);
            this.ctx.stroke();
        }

        // Horizontal grid lines (values)
        const { min: yMin, max: yMax } = this.getMinMax();
        const valueStep = this.getGridStep(yMin, yMax);

        for (let v = Math.ceil(yMin / valueStep) * valueStep; v <= yMax; v += valueStep) {
            const y = this.valueToCanvasY(v, yMin, yMax);
            this.ctx.beginPath();
            this.ctx.moveTo(50, y);
            this.ctx.lineTo(this.width - 20, y);
            this.ctx.stroke();
        }

        this.ctx.globalAlpha = 1;
    }

    getGridStep(min, max) {
        const range = max - min;
        let step = 10;
        if (range > 100) step = 20;
        if (range > 200) step = 50;
        if (range > 500) step = 100;
        return step;
    }

    drawYAxisLabels(yMin, yMax) {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = '11px Arial';
        this.ctx.textAlign = 'right';
        this.ctx.globalAlpha = 0.7;

        const step = this.getGridStep(yMin, yMax);

        for (let v = Math.ceil(yMin / step) * step; v <= yMax; v += step) {
            const y = this.valueToCanvasY(v, yMin, yMax);
            this.ctx.fillText(v.toFixed(0) + this.config.unit, 45, y + 4);
        }

        this.ctx.globalAlpha = 1;
    }

    drawXAxisLabels(minTime, maxTime) {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = '11px Arial';
        this.ctx.textAlign = 'center';
        this.ctx.globalAlpha = 0.7;

        const timeStep = 60000; // 1 minute
        const now = Date.now();

        for (let t = Math.ceil(minTime / timeStep) * timeStep; t <= maxTime; t += timeStep) {
            const x = this.timeToCanvasX(t, minTime, maxTime);
            const secondsAgo = Math.round((now - t) / 1000);
            let label = '';

            if (secondsAgo < 60) {
                label = secondsAgo + 's ago';
            } else if (secondsAgo < 3600) {
                label = Math.round(secondsAgo / 60) + 'm ago';
            } else {
                label = Math.round(secondsAgo / 3600) + 'h ago';
            }

            this.ctx.fillText(label, x, this.height - 15);
        }

        this.ctx.globalAlpha = 1;
    }

    drawAxes() {
        this.ctx.strokeStyle = this.colors.text;
        this.ctx.lineWidth = 2;

        // Y-axis
        this.ctx.beginPath();
        this.ctx.moveTo(50, 50);
        this.ctx.lineTo(50, this.height - 40);
        this.ctx.stroke();

        // X-axis
        this.ctx.beginPath();
        this.ctx.moveTo(50, this.height - 40);
        this.ctx.lineTo(this.width - 20, this.height - 40);
        this.ctx.stroke();
    }

    drawDataSeries(minTime, maxTime, yMin, yMax) {
        const { min: actualMin, max: actualMax } = this.getMinMax();

        this.series.forEach((series, seriesName) => {
            if (!series.visible || series.data.length === 0) return;

            this.ctx.strokeStyle = series.color;
            this.ctx.lineWidth = 2;
            this.ctx.globalAlpha = 0.8;

            this.ctx.beginPath();

            for (let i = 0; i < series.data.length; i++) {
                const point = series.data[i];
                const x = this.timeToCanvasX(point.time, minTime, maxTime);
                const y = this.valueToCanvasY(point.value, actualMin, actualMax);

                if (i === 0) {
                    this.ctx.moveTo(x, y);
                } else {
                    this.ctx.lineTo(x, y);
                }
            }

            this.ctx.stroke();

            // Draw data points (dots)
            this.ctx.fillStyle = series.color;
            series.data.forEach(point => {
                const x = this.timeToCanvasX(point.time, minTime, maxTime);
                const y = this.valueToCanvasY(point.value, actualMin, actualMax);
                this.ctx.fillRect(x - 2, y - 2, 4, 4);
            });

            this.ctx.globalAlpha = 1;
        });
    }

    drawLegend(yMin, yMax) {
        const legendX = 60;
        const legendY = this.height - 25;
        const spacing = 120;

        let index = 0;
        this.series.forEach((series, seriesName) => {
            // Only show visible series in legend
            if (!series.visible) return;

            const x = legendX + index * spacing;
            const y = legendY;

            // Draw color box
            this.ctx.fillStyle = series.color;
            this.ctx.globalAlpha = 0.8;
            this.ctx.fillRect(x, y, 10, 10);

            // Draw label
            this.ctx.fillStyle = this.colors.text;
            this.ctx.globalAlpha = 1;
            this.ctx.font = '11px Arial';
            this.ctx.textAlign = 'left';
            this.ctx.fillText(seriesName, x + 15, y + 9);

            index++;
        });
    }

    // Cleanup
    destroy() {
        if (this.resizeObserver) {
            this.resizeObserver.disconnect();
        }
        if (this.animationFrame) {
            cancelAnimationFrame(this.animationFrame);
        }
    }

    // Get statistics for current data
    getStats(seriesName) {
        if (!this.series.has(seriesName)) return null;

        const data = this.series.get(seriesName).data;
        if (data.length === 0) return null;

        const values = data.map(p => p.value);
        const avg = values.reduce((a, b) => a + b, 0) / values.length;
        const min = Math.min(...values);
        const max = Math.max(...values);
        const current = data[data.length - 1].value;

        return { avg, min, max, current, count: values.length };
    }

    // Export data as CSV
    exportData() {
        let csv = 'Time,' + Array.from(this.series.keys()).join(',') + '\n';

        if (this.series.size === 0) return csv;

        const allPoints = new Map();
        this.series.forEach((series, seriesName) => {
            series.data.forEach(p => {
                if (!allPoints.has(p.time)) {
                    allPoints.set(p.time, {});
                }
                allPoints.get(p.time)[seriesName] = p.value;
            });
        });

        const sortedTimes = Array.from(allPoints.keys()).sort((a, b) => a - b);
        const seriesNames = Array.from(this.series.keys());

        sortedTimes.forEach(time => {
            const row = [new Date(time).toISOString()];
            const values = allPoints.get(time);
            seriesNames.forEach(name => {
                row.push(values[name] || '');
            });
            csv += row.join(',') + '\n';
        });

        return csv;
    }
}

