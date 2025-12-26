'use strict';

// --- shared/utils.js ---
;/**
 * Shared Utility Functions
 * Helpers for common tasks across the web UI
 */

const Utils = {
    /**
     * Safely get and update a DOM element's textContent
     * @param {string} id - Element ID
     * @param {string} value - Value to set
     * @returns {boolean} - Whether the element was found and updated
     */
    setText(id, value) {
        const el = document.getElementById(id);
        if (el) {
            el.textContent = value;
            return true;
        }
        return false;
    },

    /**
     * Safely get and update a DOM element's value
     * @param {string} id - Element ID
     * @param {string} value - Value to set
     * @returns {boolean} - Whether the element was found and updated
     */
    setValue(id, value) {
        const el = document.getElementById(id);
        if (el) {
            el.value = value;
            return true;
        }
        return false;
    },

    /**
     * Safely update a DOM element's style property
     * @param {string} id - Element ID
     * @param {string} property - CSS property name
     * @param {string} value - Value to set
     * @returns {boolean} - Whether the element was found and updated
     */
    setStyle(id, property, value) {
        const el = document.getElementById(id);
        if (el) {
            el.style[property] = value;
            return true;
        }
        return false;
    },

    /**
     * Safely get a DOM element
     * @param {string} id - Element ID
     * @returns {HTMLElement|null} - The element or null
     */
    getElement(id) {
        return document.getElementById(id);
    },

    /**
     * Update multiple elements with a mapping object
     * @param {Object} updates - Object with {elementId: value} pairs
     */
    updateElements(updates) {
        for (const [id, value] of Object.entries(updates)) {
            this.setText(id, value);
        }
    },

    /**
     * Debounce function calls
     * @param {Function} func - Function to debounce
     * @param {number} wait - Milliseconds to wait
     * @returns {Function} - Debounced function
     */
    debounce(func, wait) {
        let timeout;
        return function executedFunction(...args) {
            const later = () => {
                clearTimeout(timeout);
                func(...args);
            };
            clearTimeout(timeout);
            timeout = setTimeout(later, wait);
        };
    },

    /**
     * Throttle function calls
     * @param {func} func - Function to throttle
     * @param {number} limit - Milliseconds between calls
     * @returns {Function} - Throttled function
     */
    throttle(func, limit) {
        let inThrottle;
        return function(...args) {
            if (!inThrottle) {
                func.apply(this, args);
                inThrottle = true;
                setTimeout(() => inThrottle = false, limit);
            }
        };
    },

    /**
     * Format bytes to human readable string
     * @param {number} bytes - Number of bytes
     * @param {number} decimals - Decimal places
     * @returns {string} - Formatted string
     */
    formatBytes(bytes, decimals = 2) {
        if (bytes === 0) return '0 Bytes';
        const k = 1024;
        const dm = decimals < 0 ? 0 : decimals;
        const sizes = ['Bytes', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
    },

    /**
     * Format milliseconds to human readable duration
     * @param {number} ms - Milliseconds
     * @returns {string} - Formatted duration
     */
    formatDuration(ms) {
        const seconds = Math.floor(ms / 1000);
        const minutes = Math.floor(seconds / 60);
        const hours = Math.floor(minutes / 60);
        const days = Math.floor(hours / 24);

        if (days > 0) return `${days}d ${hours % 24}h`;
        if (hours > 0) return `${hours}h ${minutes % 60}m`;
        if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
        return `${seconds}s`;
    },

    /**
     * Check if running in file:// protocol or mock mode
     * @returns {boolean} - True if offline mode
     */
    isOfflineMode() {
        return window.location.protocol === 'file:' || window.MockMode?.enabled;
    },

    /**
     * Clamp a number between min and max
     * @param {number} value - Value to clamp
     * @param {number} min - Minimum value
     * @param {number} max - Maximum value
     * @returns {number} - Clamped value
     */
    clamp(value, min, max) {
        return Math.min(Math.max(value, min), max);
    },

    /**
     * Generate a random number between min and max
     * @param {number} min - Minimum value
     * @param {number} max - Maximum value
     * @returns {number} - Random number
     */
    random(min, max) {
        return Math.random() * (max - min) + min;
    },

    /**
     * Linear interpolation between two values
     * @param {number} start - Start value
     * @param {number} end - End value
     * @param {number} t - Interpolation factor (0-1)
     * @returns {number} - Interpolated value
     */
    lerp(start, end, t) {
        return start + (end - start) * t;
    },

    /**
     * Download a file with given content
     * @param {string} filename - Name for the downloaded file
     * @param {string} content - File content
     * @param {string} mimeType - MIME type (default: text/plain)
     */
    downloadFile(filename, content, mimeType = 'text/plain') {
        const blob = new Blob([content], { type: mimeType });
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        a.click();
        window.URL.revokeObjectURL(url);
    },

    /**
     * Copy text to clipboard
     * @param {string} text - Text to copy
     * @returns {Promise<boolean>} - Whether copy succeeded
     */
    async copyToClipboard(text) {
        try {
            await navigator.clipboard.writeText(text);
            return true;
        } catch (err) {
            console.error('Failed to copy to clipboard:', err);
            return false;
        }
    },

    /**
     * Escape HTML special characters
     * @param {string} text - Text to escape
     * @returns {string} - Escaped text
     */
    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    },

    /**
     * Deep clone an object
     * @param {Object} obj - Object to clone
     * @returns {Object} - Cloned object
     */
    deepClone(obj) {
        return JSON.parse(JSON.stringify(obj));
    }
};

// Expose globally
window.Utils = Utils;


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


// --- shared/state.js ---
;/**
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


// --- shared/theme.js ---
;/**
 * @file shared/theme.js
 * @brief Theme and accessibility management
 */

class ThemeManager {
    static themes = ['light', 'dark', 'high-contrast', 'colorblind'];
    static currentTheme = 'light';
    static settings = {
        theme: 'light',
        fontSize: 100,
        gridLines: false,
        dualAxis: true,
        soundAlerts: true,
        autoRefresh: true
    };

    static init() {
        console.log('[THEME] Initializing theme manager');
        this.loadSettings();
        this.applyTheme(this.currentTheme);
        this.setFontSize(this.settings.fontSize); // Apply saved font size
        this.setupListeners();
        console.log('[THEME] Theme manager initialized with theme:', this.currentTheme, 'fontSize:', this.settings.fontSize);
    }

    static loadSettings() {
        const saved = localStorage.getItem('app-settings');
        if (saved) {
            try {
                this.settings = { ...this.settings, ...JSON.parse(saved) };
                this.currentTheme = this.settings.theme;
            } catch (e) {
                console.error('[THEME] Failed to load settings:', e);
            }
        }
    }

    static saveSettings() {
        localStorage.setItem('app-settings', JSON.stringify(this.settings));
        window.dispatchEvent(new CustomEvent('settings-changed', { detail: this.settings }));
    }

    static applyTheme(themeName) {
        if (!this.themes.includes(themeName)) {
            console.warn(`[THEME] Unknown theme: ${themeName}`);
            return;
        }

        // Use data-theme attribute to match enhancements.css
        document.documentElement.setAttribute('data-theme', themeName);

        // Also set body class for backward compatibility
        document.body.className = '';
        if (themeName === 'dark') {
            document.body.classList.add('dark-theme');
        } else if (themeName === 'high-contrast') {
            document.body.classList.add('high-contrast');
        } else if (themeName === 'colorblind') {
            document.body.classList.add('colorblind-mode');
        }

        this.currentTheme = themeName;
        this.settings.theme = themeName;
        this.saveSettings();

        console.log(`[THEME] Applied theme: ${themeName}`);
    }

    static setFontSize(percent) {
        this.settings.fontSize = Math.max(80, Math.min(120, percent));
        document.documentElement.style.fontSize = (this.settings.fontSize / 100) * 16 + 'px';
        this.saveSettings();
    }

    static toggleGridLines(enabled) {
        this.settings.gridLines = enabled;
        this.saveSettings();
    }

    static toggleDualAxis(enabled) {
        this.settings.dualAxis = enabled;
        this.saveSettings();
    }

    static toggleSoundAlerts(enabled) {
        this.settings.soundAlerts = enabled;
        this.saveSettings();
    }

    static getThemes() {
        return this.themes;
    }

    static getCurrentTheme() {
        return this.currentTheme;
    }

    static getSettings() {
        return { ...this.settings };
    }

    static setupListeners() {
        window.addEventListener('keydown', (e) => {
            // T = Toggle theme
            if (e.key.toLowerCase() === 't' && !e.ctrlKey && !e.metaKey) {
                const current = this.themes.indexOf(this.currentTheme);
                const next = this.themes[(current + 1) % this.themes.length];
                this.applyTheme(next);
            }
        });
    }
}

// Auto-init on load
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => ThemeManager.init());
} else {
    ThemeManager.init();
}


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


// --- shared/calibration-wizard.js ---
;/**
 * Calibration Wizard System
 * Step-by-step calibration wizards for encoder, motion, and spindle alignment
 */

class CalibrationWizard {
    constructor(wizardType, options = {}) {
        this.wizardType = wizardType;
        this.options = options;
        this.currentStep = 0;
        this.steps = [];
        this.results = {};
        this.overlay = null;
        this.container = null;

        this.initWizard();
    }

    initWizard() {
        switch (this.wizardType) {
            case 'encoder':
                this.steps = this.getEncoderCalibrationSteps();
                break;
            case 'motion':
                this.steps = this.getMotionAccuracySteps();
                break;
            case 'spindle':
                this.steps = this.getSpindleAlignmentSteps();
                break;
            case 'tuning':
                this.steps = this.getAutoTuningSteps();
                break;
            default:
                console.error('[Wizard] Unknown wizard type:', this.wizardType);
                return;
        }

        this.createWizardUI();
        this.showStep(0);
    }

    getEncoderCalibrationSteps() {
        return [
            {
                title: 'Encoder Calibration - Introduction',
                content: `
                    <div style="padding: 20px;">
                        <h3>📏 Encoder Calibration Wizard</h3>
                        <p>This wizard will guide you through calibrating the encoder for precise position feedback.</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Prerequisites:</h4>
                            <ul>
                                <li>✓ Machine must be powered and homed</li>
                                <li>✓ E-stop must be released</li>
                                <li>✓ Clear workspace around machine</li>
                                <li>✓ Dial indicator or measurement tool ready</li>
                            </ul>
                        </div>
                        <p><strong>Estimated time:</strong> 10-15 minutes</p>
                    </div>
                `,
                validation: () => {
                    const state = AppState.data;
                    if (state.safety?.estop) {
                        Toast.error('E-stop is active. Release E-stop to continue.');
                        return false;
                    }
                    return true;
                }
            },
            {
                title: 'Step 1: Initial Position',
                content: `
                    <div style="padding: 20px;">
                        <h3>Initial Position Setup</h3>
                        <p>Move the X-axis to the reference position (home position).</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Current Position:</h4>
                            <div id="wizard-current-pos" style="font-size: 24px; font-weight: bold; margin: 10px 0;">
                                X: <span id="wizard-x-pos">--</span> mm
                            </div>
                        </div>
                        <div style="margin: 20px 0;">
                            <button class="btn btn-primary" onclick="CalibrationWizard.moveToHome()">
                                🏠 Move to Home
                            </button>
                        </div>
                        <p style="color: var(--text-secondary); font-size: 14px;">
                            ⚠️ Ensure the path is clear before moving the machine.
                        </p>
                    </div>
                `,
                onEnter: () => {
                    // Update position display
                    this.updatePositionDisplay();
                    this.positionInterval = setInterval(() => this.updatePositionDisplay(), 500);
                },
                onExit: () => {
                    if (this.positionInterval) {
                        clearInterval(this.positionInterval);
                    }
                }
            },
            {
                title: 'Step 2: Measure Reference',
                content: `
                    <div style="padding: 20px;">
                        <h3>Reference Measurement</h3>
                        <p>Place your measurement tool at the reference position and record the reading.</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin-bottom: 10px;">
                                <strong>Reference Reading (mm):</strong>
                            </label>
                            <input
                                type="number"
                                id="wizard-ref-reading"
                                step="0.001"
                                style="width: 200px; padding: 8px; font-size: 16px;"
                                placeholder="0.000"
                            />
                        </div>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <p><strong>Encoder Reading:</strong> <span id="wizard-encoder-ref">--</span> counts</p>
                        </div>
                    </div>
                `,
                onEnter: () => {
                    const state = AppState.data;
                    const encoderEl = document.getElementById('wizard-encoder-ref');
                    if (encoderEl) {
                        encoderEl.textContent = (state.axis?.x?.encoder_count || 0).toString();
                    }
                }
            },
            {
                title: 'Step 3: Move to Test Position',
                content: `
                    <div style="padding: 20px;">
                        <h3>Test Position Movement</h3>
                        <p>Move the X-axis to a known distance from the reference position.</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin-bottom: 10px;">
                                <strong>Target Distance (mm):</strong>
                            </label>
                            <input
                                type="number"
                                id="wizard-target-dist"
                                value="100"
                                step="10"
                                style="width: 200px; padding: 8px; font-size: 16px;"
                            />
                            <button class="btn btn-primary" style="margin-left: 10px;" onclick="CalibrationWizard.moveRelative()">
                                ➡️ Move
                            </button>
                        </div>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <p><strong>Current Position:</strong> <span id="wizard-current-x">--</span> mm</p>
                            <p><strong>Encoder Count:</strong> <span id="wizard-encoder-test">--</span> counts</p>
                        </div>
                    </div>
                `,
                onEnter: () => {
                    this.updateTestPositionDisplay();
                    this.testPosInterval = setInterval(() => this.updateTestPositionDisplay(), 500);
                },
                onExit: () => {
                    if (this.testPosInterval) {
                        clearInterval(this.testPosInterval);
                    }
                }
            },
            {
                title: 'Step 4: Verify and Calculate',
                content: `
                    <div style="padding: 20px;">
                        <h3>Verification and Calculation</h3>
                        <p>Measure the actual position with your measurement tool and enter the reading.</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin-bottom: 10px;">
                                <strong>Actual Position Reading (mm):</strong>
                            </label>
                            <input
                                type="number"
                                id="wizard-actual-reading"
                                step="0.001"
                                style="width: 200px; padding: 8px; font-size: 16px;"
                                placeholder="100.000"
                            />
                        </div>
                        <div id="wizard-calibration-results" style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <p><em>Enter measurement to see calibration results...</em></p>
                        </div>
                        <button class="btn btn-primary" onclick="CalibrationWizard.calculateCalibration()">
                            🧮 Calculate Calibration Factor
                        </button>
                    </div>
                `
            },
            {
                title: 'Calibration Complete',
                content: `
                    <div style="padding: 20px;">
                        <h3>✅ Calibration Complete</h3>
                        <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <h4>Calibration Results:</h4>
                            <div id="wizard-final-results" style="margin: 15px 0; font-size: 16px;">
                                <p><strong>Calibration Factor:</strong> <span id="wizard-cal-factor">--</span> counts/mm</p>
                                <p><strong>Position Error:</strong> <span id="wizard-pos-error">--</span> mm</p>
                                <p><strong>Accuracy:</strong> <span id="wizard-accuracy">--</span>%</p>
                            </div>
                        </div>
                        <div style="margin: 20px 0;">
                            <button class="btn btn-success" onclick="CalibrationWizard.applyCalibration()">
                                ✓ Apply Calibration
                            </button>
                            <button class="btn btn-secondary" style="margin-left: 10px;" onclick="CalibrationWizard.exportResults()">
                                💾 Export Results
                            </button>
                        </div>
                        <p style="color: var(--text-secondary); font-size: 14px; margin-top: 20px;">
                            ℹ️ The calibration factor has been calculated. Click "Apply Calibration" to save it to the system.
                        </p>
                    </div>
                `,
                onEnter: () => {
                    this.displayFinalResults();
                }
            }
        ];
    }

    getMotionAccuracySteps() {
        return [
            {
                title: 'Motion Accuracy Test - Introduction',
                content: `
                    <div style="padding: 20px;">
                        <h3>🎯 Motion Accuracy Test</h3>
                        <p>This wizard tests the accuracy of motion across multiple positions and calculates repeatability.</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Test Parameters:</h4>
                            <ul>
                                <li>Test Points: 5 positions</li>
                                <li>Repetitions: 3 per position</li>
                                <li>Axes Tested: X, Y, Z</li>
                            </ul>
                        </div>
                        <p><strong>Estimated time:</strong> 15-20 minutes</p>
                    </div>
                `
            },
            {
                title: 'Running Tests',
                content: `
                    <div style="padding: 20px;">
                        <h3>Test in Progress</h3>
                        <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <div id="wizard-test-progress" style="margin-bottom: 15px;">
                                <div style="display: flex; justify-content: space-between; margin-bottom: 5px;">
                                    <span>Progress:</span>
                                    <span id="wizard-test-percent">0%</span>
                                </div>
                                <div style="height: 20px; background: var(--bg-tertiary); border-radius: 10px; overflow: hidden;">
                                    <div id="wizard-test-bar" style="height: 100%; width: 0%; background: var(--color-optimal); transition: width 0.3s;"></div>
                                </div>
                            </div>
                            <div id="wizard-test-status" style="margin-top: 15px;">
                                <p>Status: <span id="wizard-status-text">Ready to start</span></p>
                                <p>Current Position: <span id="wizard-test-pos">--</span></p>
                            </div>
                        </div>
                        <button class="btn btn-primary" id="wizard-start-test" onclick="CalibrationWizard.startMotionTest()">
                            ▶️ Start Test
                        </button>
                    </div>
                `
            },
            {
                title: 'Test Results',
                content: `
                    <div style="padding: 20px;">
                        <h3>📊 Test Results</h3>
                        <div id="wizard-motion-results" style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <h4>Accuracy Metrics:</h4>
                            <div style="margin: 15px 0;">
                                <p><strong>X-Axis Repeatability:</strong> <span id="result-x-repeat">--</span> mm</p>
                                <p><strong>Y-Axis Repeatability:</strong> <span id="result-y-repeat">--</span> mm</p>
                                <p><strong>Z-Axis Repeatability:</strong> <span id="result-z-repeat">--</span> mm</p>
                                <p style="margin-top: 15px;"><strong>Overall Accuracy:</strong> <span id="result-overall">--</span>%</p>
                            </div>
                        </div>
                        <button class="btn btn-primary" onclick="CalibrationWizard.exportMotionResults()">
                            💾 Export Results
                        </button>
                    </div>
                `
            }
        ];
    }

    getSpindleAlignmentSteps() {
        return [
            {
                title: 'Spindle Alignment - Introduction',
                content: `
                    <div style="padding: 20px;">
                        <h3>🔄 Spindle Alignment Wizard</h3>
                        <p>This wizard helps verify and adjust spindle alignment for optimal cutting performance.</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Safety Requirements:</h4>
                            <ul>
                                <li>⚠️ Remove blade before testing</li>
                                <li>✓ Clear workspace</li>
                                <li>✓ Dial indicator ready</li>
                            </ul>
                        </div>
                        <p><strong>Estimated time:</strong> 20-30 minutes</p>
                    </div>
                `
            },
            {
                title: 'Vertical Alignment Check',
                content: `
                    <div style="padding: 20px;">
                        <h3>Vertical Alignment</h3>
                        <p>Check spindle perpendicularity to the work surface.</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin-bottom: 10px;">
                                <strong>Front Reading (mm):</strong>
                            </label>
                            <input type="number" id="wizard-align-front" step="0.001" style="width: 200px; padding: 8px;"/>
                            <label style="display: block; margin: 15px 0 10px 0;">
                                <strong>Back Reading (mm):</strong>
                            </label>
                            <input type="number" id="wizard-align-back" step="0.001" style="width: 200px; padding: 8px;"/>
                        </div>
                        <div id="wizard-align-result" style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <p><em>Enter readings to see alignment results...</em></p>
                        </div>
                        <button class="btn btn-primary" onclick="CalibrationWizard.calculateAlignment()">
                            🧮 Calculate Alignment
                        </button>
                    </div>
                `
            },
            {
                title: 'Alignment Complete',
                content: `
                    <div style="padding: 20px;">
                        <h3>✅ Alignment Check Complete</h3>
                        <div id="wizard-alignment-summary" style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <h4>Alignment Results:</h4>
                            <p><strong>Vertical Deviation:</strong> <span id="align-deviation">--</span> mm</p>
                            <p><strong>Status:</strong> <span id="align-status">--</span></p>
                        </div>
                        <button class="btn btn-primary" onclick="CalibrationWizard.exportAlignmentResults()">
                            💾 Export Results
                        </button>
                    </div>
                `
            }
        ];
    }

    getAutoTuningSteps() {
        return [
            {
                title: 'Auto-Tuning - Introduction',
                content: `
                    <div style="padding: 20px;">
                        <h3>⚙️ Automated Parameter Tuning</h3>
                        <p>This wizard automatically tunes motion parameters for optimal performance.</p>
                        <div style="background: var(--bg-secondary); padding: 15px; border-radius: 8px; margin: 20px 0;">
                            <h4>Parameters to Tune:</h4>
                            <ul>
                                <li>Acceleration profiles</li>
                                <li>Jerk limits</li>
                                <li>Velocity profiles</li>
                                <li>PID gains (if applicable)</li>
                            </ul>
                        </div>
                        <p><strong>Estimated time:</strong> 10-15 minutes</p>
                        <p style="color: var(--color-warning); margin-top: 15px;">
                            ⚠️ Machine will make rapid test movements. Ensure workspace is clear.
                        </p>
                    </div>
                `
            },
            {
                title: 'Select Parameters',
                content: `
                    <div style="padding: 20px;">
                        <h3>Parameter Selection</h3>
                        <p>Select which parameters to tune:</p>
                        <div style="margin: 20px 0;">
                            <label style="display: block; margin: 10px 0;">
                                <input type="checkbox" id="tune-accel" checked /> Acceleration
                            </label>
                            <label style="display: block; margin: 10px 0;">
                                <input type="checkbox" id="tune-jerk" checked /> Jerk Limits
                            </label>
                            <label style="display: block; margin: 10px 0;">
                                <input type="checkbox" id="tune-velocity" checked /> Velocity
                            </label>
                        </div>
                    </div>
                `
            },
            {
                title: 'Tuning in Progress',
                content: `
                    <div style="padding: 20px;">
                        <h3>Auto-Tuning...</h3>
                        <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <div id="wizard-tuning-progress">
                                <div style="display: flex; justify-content: space-between; margin-bottom: 5px;">
                                    <span>Progress:</span>
                                    <span id="wizard-tuning-percent">0%</span>
                                </div>
                                <div style="height: 20px; background: var(--bg-tertiary); border-radius: 10px; overflow: hidden;">
                                    <div id="wizard-tuning-bar" style="height: 100%; width: 0%; background: var(--color-optimal); transition: width 0.3s;"></div>
                                </div>
                            </div>
                            <p id="wizard-tuning-status" style="margin-top: 15px;">Status: Initializing...</p>
                        </div>
                    </div>
                `
            },
            {
                title: 'Tuning Complete',
                content: `
                    <div style="padding: 20px;">
                        <h3>✅ Auto-Tuning Complete</h3>
                        <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                            <h4>Optimized Parameters:</h4>
                            <div id="wizard-tuned-params" style="margin: 15px 0;">
                                <p><strong>Acceleration:</strong> <span id="tuned-accel">--</span> mm/s²</p>
                                <p><strong>Jerk:</strong> <span id="tuned-jerk">--</span> mm/s³</p>
                                <p><strong>Max Velocity:</strong> <span id="tuned-vel">--</span> mm/s</p>
                            </div>
                            <p style="margin-top: 15px;"><strong>Improvement:</strong> <span id="tuned-improvement">--</span>% faster</p>
                        </div>
                        <button class="btn btn-success" onclick="CalibrationWizard.applyTunedParams()">
                            ✓ Apply Parameters
                        </button>
                    </div>
                `
            }
        ];
    }

    createWizardUI() {
        // Create overlay
        this.overlay = document.createElement('div');
        this.overlay.className = 'wizard-overlay';
        this.overlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: rgba(0, 0, 0, 0.7);
            z-index: 9999;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        `;

        // Create wizard container
        this.container = document.createElement('div');
        this.container.className = 'wizard-container';
        this.container.style.cssText = `
            background: var(--bg-primary);
            border-radius: 12px;
            box-shadow: var(--shadow-lg);
            max-width: 700px;
            width: 100%;
            max-height: 90vh;
            overflow-y: auto;
            position: relative;
        `;

        this.container.innerHTML = `
            <div class="wizard-header" style="padding: 20px; border-bottom: 1px solid var(--border-color); position: sticky; top: 0; background: var(--bg-primary); z-index: 10;">
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <h2 id="wizard-title" style="margin: 0;">Calibration Wizard</h2>
                    <button class="wizard-close" onclick="CalibrationWizard.activeWizard?.close()" style="background: none; border: none; font-size: 24px; cursor: pointer; color: var(--text-secondary);">×</button>
                </div>
                <div id="wizard-progress" style="margin-top: 15px;">
                    <div style="display: flex; justify-content: space-between; margin-bottom: 5px; font-size: 12px; color: var(--text-secondary);">
                        <span>Step <span id="wizard-step-num">1</span> of <span id="wizard-step-total">1</span></span>
                        <span id="wizard-step-percent">0%</span>
                    </div>
                    <div style="height: 4px; background: var(--bg-tertiary); border-radius: 2px; overflow: hidden;">
                        <div id="wizard-progress-bar" style="height: 100%; width: 0%; background: var(--color-optimal); transition: width 0.3s;"></div>
                    </div>
                </div>
            </div>
            <div class="wizard-content" id="wizard-content" style="padding: 0; min-height: 300px;">
                <!-- Step content goes here -->
            </div>
            <div class="wizard-footer" style="padding: 20px; border-top: 1px solid var(--border-color); display: flex; justify-content: space-between; position: sticky; bottom: 0; background: var(--bg-primary);">
                <button class="btn btn-secondary" id="wizard-prev" onclick="CalibrationWizard.activeWizard?.previousStep()">← Previous</button>
                <button class="btn btn-primary" id="wizard-next" onclick="CalibrationWizard.activeWizard?.nextStep()">Next →</button>
            </div>
        `;

        this.overlay.appendChild(this.container);
        document.body.appendChild(this.overlay);

        // Update step total
        document.getElementById('wizard-step-total').textContent = this.steps.length;
    }

    showStep(stepIndex) {
        if (stepIndex < 0 || stepIndex >= this.steps.length) return;

        const step = this.steps[stepIndex];
        this.currentStep = stepIndex;

        // Update header
        document.getElementById('wizard-title').textContent = step.title;
        document.getElementById('wizard-step-num').textContent = stepIndex + 1;

        // Update progress bar
        const progress = ((stepIndex + 1) / this.steps.length) * 100;
        document.getElementById('wizard-progress-bar').style.width = progress + '%';
        document.getElementById('wizard-step-percent').textContent = Math.round(progress) + '%';

        // Update content
        document.getElementById('wizard-content').innerHTML = step.content;

        // Update buttons
        const prevBtn = document.getElementById('wizard-prev');
        const nextBtn = document.getElementById('wizard-next');

        prevBtn.disabled = stepIndex === 0;
        prevBtn.style.opacity = stepIndex === 0 ? '0.5' : '1';

        if (stepIndex === this.steps.length - 1) {
            nextBtn.textContent = 'Finish';
            nextBtn.className = 'btn btn-success';
        } else {
            nextBtn.textContent = 'Next →';
            nextBtn.className = 'btn btn-primary';
        }

        // Call step callbacks
        if (step.onEnter) {
            step.onEnter.call(this);
        }
    }

    nextStep() {
        const step = this.steps[this.currentStep];

        // Validate if validator exists
        if (step.validation && !step.validation.call(this)) {
            return;
        }

        // Call onExit if exists
        if (step.onExit) {
            step.onExit.call(this);
        }

        if (this.currentStep < this.steps.length - 1) {
            this.showStep(this.currentStep + 1);
        } else {
            this.finish();
        }
    }

    previousStep() {
        const step = this.steps[this.currentStep];

        // Call onExit if exists
        if (step.onExit) {
            step.onExit.call(this);
        }

        if (this.currentStep > 0) {
            this.showStep(this.currentStep - 1);
        }
    }

    finish() {
        Toast.success('Wizard completed successfully!');
        this.close();
    }

    close() {
        if (this.overlay && this.overlay.parentNode) {
            this.overlay.parentNode.removeChild(this.overlay);
        }
        CalibrationWizard.activeWizard = null;
    }

    // Helper methods for updating displays
    updatePositionDisplay() {
        const state = AppState.data;
        const xPosEl = document.getElementById('wizard-x-pos');
        if (xPosEl) {
            xPosEl.textContent = (state.axis?.x?.position_mm || 0).toFixed(3);
        }
    }

    updateTestPositionDisplay() {
        const state = AppState.data;
        const currentXEl = document.getElementById('wizard-current-x');
        const encoderTestEl = document.getElementById('wizard-encoder-test');

        if (currentXEl) {
            currentXEl.textContent = (state.axis?.x?.position_mm || 0).toFixed(3);
        }
        if (encoderTestEl) {
            encoderTestEl.textContent = (state.axis?.x?.encoder_count || 0).toString();
        }
    }

    displayFinalResults() {
        // Display calibration results from stored data
        const calFactorEl = document.getElementById('wizard-cal-factor');
        const posErrorEl = document.getElementById('wizard-pos-error');
        const accuracyEl = document.getElementById('wizard-accuracy');

        if (this.results.calibrationFactor && calFactorEl) {
            calFactorEl.textContent = this.results.calibrationFactor.toFixed(3);
        }
        if (this.results.positionError && posErrorEl) {
            posErrorEl.textContent = this.results.positionError.toFixed(3);
        }
        if (this.results.accuracy && accuracyEl) {
            accuracyEl.textContent = this.results.accuracy.toFixed(2);
        }
    }

    // Static methods for wizard actions
    static moveToHome() {
        Toast.info('Moving to home position...');
        // In real implementation, send G-code command
        console.log('[Wizard] Move to home');
    }

    static moveRelative() {
        const distance = document.getElementById('wizard-target-dist').value;
        Toast.info(`Moving ${distance}mm...`);
        console.log('[Wizard] Move relative:', distance);
    }

    static calculateCalibration() {
        const refReading = parseFloat(document.getElementById('wizard-ref-reading').value);
        const actualReading = parseFloat(document.getElementById('wizard-actual-reading').value);

        if (isNaN(refReading) || isNaN(actualReading)) {
            Toast.error('Please enter valid measurements');
            return;
        }

        const distance = actualReading - refReading;
        const state = AppState.data;
        const encoderCounts = state.axis?.x?.encoder_count || 1000; // Mock value

        const calibrationFactor = encoderCounts / distance;
        const positionError = Math.abs(distance - 100); // Expected 100mm
        const accuracy = Math.max(0, (1 - positionError / 100) * 100);

        // Store results
        if (CalibrationWizard.activeWizard) {
            CalibrationWizard.activeWizard.results = {
                calibrationFactor,
                positionError,
                accuracy
            };
        }

        const resultsEl = document.getElementById('wizard-calibration-results');
        if (resultsEl) {
            resultsEl.innerHTML = `
                <h4>Calculated Values:</h4>
                <p><strong>Calibration Factor:</strong> ${calibrationFactor.toFixed(3)} counts/mm</p>
                <p><strong>Position Error:</strong> ${positionError.toFixed(3)} mm</p>
                <p><strong>Accuracy:</strong> ${accuracy.toFixed(2)}%</p>
            `;
        }

        Toast.success('Calibration calculated!');
    }

    static applyCalibration() {
        Toast.success('Calibration applied successfully!');
        console.log('[Wizard] Apply calibration:', CalibrationWizard.activeWizard?.results);
    }

    static exportResults() {
        const results = CalibrationWizard.activeWizard?.results || {};
        const csv = `Encoder Calibration Results
Generated: ${new Date().toLocaleString()}

Calibration Factor: ${results.calibrationFactor || '--'} counts/mm
Position Error: ${results.positionError || '--'} mm
Accuracy: ${results.accuracy || '--'}%
`;
        Utils.downloadFile(`calibration-${Date.now()}.txt`, csv, 'text/plain');
        Toast.success('Results exported!');
    }

    static startMotionTest() {
        Toast.info('Starting motion accuracy test...');
        // Simulate test progress
        let progress = 0;
        const interval = setInterval(() => {
            progress += 10;
            const barEl = document.getElementById('wizard-test-bar');
            const percentEl = document.getElementById('wizard-test-percent');
            const statusEl = document.getElementById('wizard-status-text');

            if (barEl) barEl.style.width = progress + '%';
            if (percentEl) percentEl.textContent = progress + '%';
            if (statusEl) statusEl.textContent = `Testing position ${Math.floor(progress / 20) + 1}/5`;

            if (progress >= 100) {
                clearInterval(interval);
                if (statusEl) statusEl.textContent = 'Test complete!';
                Toast.success('Motion test completed!');
            }
        }, 1000);
    }

    static exportMotionResults() {
        Toast.success('Motion test results exported!');
    }

    static calculateAlignment() {
        const front = parseFloat(document.getElementById('wizard-align-front').value);
        const back = parseFloat(document.getElementById('wizard-align-back').value);

        if (isNaN(front) || isNaN(back)) {
            Toast.error('Please enter valid readings');
            return;
        }

        const deviation = Math.abs(front - back);
        const resultEl = document.getElementById('wizard-align-result');

        if (resultEl) {
            let status = 'OK';
            let color = 'var(--color-optimal)';

            if (deviation > 0.05) {
                status = 'Needs Adjustment';
                color = 'var(--color-warning)';
            }
            if (deviation > 0.1) {
                status = 'Critical - Adjust Immediately';
                color = 'var(--color-critical)';
            }

            resultEl.innerHTML = `
                <h4>Alignment Analysis:</h4>
                <p><strong>Deviation:</strong> ${deviation.toFixed(3)} mm</p>
                <p><strong>Status:</strong> <span style="color: ${color};">${status}</span></p>
            `;
        }

        Toast.success('Alignment calculated!');
    }

    static exportAlignmentResults() {
        Toast.success('Alignment results exported!');
    }

    static applyTunedParams() {
        Toast.success('Tuned parameters applied!');
    }

    // Launch wizard
    static launch(wizardType) {
        if (CalibrationWizard.activeWizard) {
            CalibrationWizard.activeWizard.close();
        }
        CalibrationWizard.activeWizard = new CalibrationWizard(wizardType);
    }
}

// Global reference to active wizard
CalibrationWizard.activeWizard = null;

// Expose globally
window.CalibrationWizard = CalibrationWizard;


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


// --- shared/fallback-pages.js ---
;/**
 * @file shared/fallback-pages.js
 * @brief Fallback HTML templates for file:// and mock mode
 * @description Extracted from router.js to reduce file size and improve maintainability
 */

const FallbackPages = {
    /**
     * Get fallback HTML for a page when running in file:// or mock mode
     * @param {string} page - Page name
     * @param {boolean} isFileProtocol - True if running from file://
     * @returns {string} HTML content
     */
    getPageHTML(page, isFileProtocol = false) {
        const mode = isFileProtocol ? 'File Mode' : 'Mock Mode';
        const tip = isFileProtocol ? '' : ' - press M to disable';

        switch (page) {
            case 'dashboard':
                return `
                    <div class="dashboard-page">
                        <div style="padding: 20px 0;">
                            <h1>📊 Dashboard (${mode})</h1>
                            <p style="color: var(--text-secondary); font-size: 14px;">Simulated data${tip}</p>
                        </div>
                        <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 20px 0;">
                            <div class="card">
                                <div class="card-header"><h3>Status</h3></div>
                                <div class="card-content"><div id="motion-status">--</div></div>
                            </div>
                            <div class="card">
                                <div class="card-header"><h3>CPU</h3></div>
                                <div class="card-content"><div id="cpu-value" style="font-size: 24px;">--</div></div>
                            </div>
                            <div class="card">
                                <div class="card-header"><h3>Memory</h3></div>
                                <div class="card-content"><div id="memory-value" style="font-size: 24px;">--</div></div>
                            </div>
                            <div class="card">
                                <div class="card-header"><h3>VFD</h3></div>
                                <div class="card-content"><div id="vfd-status">--</div></div>
                            </div>
                        </div>
                        <div id="charts-section" style="margin-top: 30px;"></div>
                    </div>
                `;

            case 'gcode':
                return `
                    <div class="gcode-page">
                        <div class="card">
                            <div class="card-header"><h2>G-code Command Input (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">G-code execution requires connection to the ESP32 device.</p>
                                <textarea id="gcode-input" placeholder="Enter G-code commands..." style="width: 100%; min-height: 150px; padding: 10px; font-family: monospace; background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 4px;" readonly></textarea>
                                <div style="margin-top: 10px;">
                                    <button class="btn btn-primary" disabled>▶️ Execute Command (Requires Device)</button>
                                </div>
                            </div>
                        </div>
                        <div class="card">
                            <div class="card-header"><h2>Parser State</h2></div>
                            <div class="card-content">
                                <div id="distance-mode">Distance Mode: G90 (Absolute)</div>
                                <div id="work-coordinate">Work Coordinate: G54</div>
                                <div id="feed-rate">Feed Rate: -- mm/min</div>
                                <div id="motion-state">Motion State: Idle</div>
                            </div>
                        </div>
                    </div>
                `;

            case 'motion':
                return `
                    <div class="motion-page">
                        <div class="card">
                            <div class="card-header"><h2>Motion Control (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary);">Motion control requires connection to the ESP32 device.</p>
                                <div style="margin-top: 20px;">
                                    <h3>Current Position</h3>
                                    <div style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin: 15px 0;">
                                        ${['X', 'Y', 'Z', 'A'].map(axis => `
                                            <div class="position-display">
                                                <div style="color: var(--text-secondary); font-size: 12px;">${axis} Axis</div>
                                                <div id="pos-${axis.toLowerCase()}" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                            </div>
                                        `).join('')}
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'diagnostics':
                return `
                    <div class="diagnostics-page">
                        <div class="card">
                            <div class="card-header"><h2>System Diagnostics (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">Live diagnostics require connection to the ESP32 device.</p>
                                <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                    <h3>Status</h3>
                                    <div id="system-status">System: ${mode}</div>
                                    <div id="motion-system">Motion: Not Connected</div>
                                    <div id="vfd-system">VFD: Not Connected</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'network':
                return `
                    <div class="network-page">
                        <div class="card">
                            <div class="card-header"><h2>Network Status (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">Network information requires connection to the ESP32 device.</p>
                                <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                    <div id="wifi-status">WiFi: Not Connected</div>
                                    <div id="wifi-ssid">SSID: --</div>
                                    <div id="signal-dbm">Signal: -- dBm</div>
                                    <div id="signal-quality">Quality: --</div>
                                    <div id="latency-ms" style="margin-top: 10px;">Latency: -- ms</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'system':
                return `
                    <div class="system-page">
                        <div class="card">
                            <div class="card-header"><h2>System Information (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">System information requires connection to the ESP32 device.</p>
                                <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                    <h3>Device Info</h3>
                                    <div>Firmware: Not Connected</div>
                                    <div>Hardware: ESP32-WROOM-32E</div>
                                    <div>Uptime: --</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'maintenance':
                return `
                    <div class="maintenance-page">
                        <div class="card">
                            <div class="card-header"><h2>Maintenance (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">Maintenance tracking requires connection to the ESP32 device.</p>
                                <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                    <h3>Component Status</h3>
                                    <div>Motor Hours: --</div>
                                    <div>VFD Runtime: --</div>
                                    <div>Last Service: --</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'logs':
                return `
                    <div class="logs-page">
                        <div class="card">
                            <div class="card-header"><h2>System Logs (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">Log viewing requires connection to the ESP32 device.</p>
                                <div id="log-container" style="padding: 20px; background: var(--bg-secondary); border-radius: 4px; font-family: monospace; max-height: 400px; overflow-y: auto;">
                                    <div>No logs available in ${mode.toLowerCase()}</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'settings':
                return `
                    <div class="settings-page">
                        <div class="card">
                            <div class="card-header"><h2>⚙️ Display & Theme Settings</h2></div>
                            <div class="card-content">
                                <div class="setting-group">
                                    <label>Theme:</label>
                                    <div class="theme-selector" style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin: 15px 0;">
                                        ${['light', 'dark', 'high-contrast', 'colorblind'].map(theme => `
                                            <button class="theme-option" data-theme="${theme}" title="${theme} Theme" style="display: flex; flex-direction: column; align-items: center; gap: 8px; padding: 12px; border: 2px solid var(--border-color); border-radius: 8px; background: var(--bg-primary); cursor: pointer;">
                                                <div class="theme-preview ${theme}" style="width: 60px; height: 60px; border-radius: 8px; background: ${this.getThemeGradient(theme)}; border: 1px solid var(--border-color);"></div>
                                                <span style="font-size: 12px;">${theme.charAt(0).toUpperCase() + theme.slice(1).replace('-', ' ')}</span>
                                            </button>
                                        `).join('')}
                                    </div>
                                </div>
                                <div class="setting-group" style="margin-top: 20px;">
                                    <label>Font Size: <span id="font-size-display">100</span>%</label>
                                    <div class="slider-container" style="margin: 10px 0;">
                                        <input type="range" id="font-size-slider" min="80" max="120" value="100" class="slider" style="width: 100%;">
                                        <div class="slider-labels" style="display: flex; justify-content: space-between; font-size: 12px; color: var(--text-secondary); margin-top: 5px;">
                                            <span>Smaller</span>
                                            <span>Normal</span>
                                            <span>Larger</span>
                                        </div>
                                    </div>
                                </div>
                                <p style="color: var(--text-secondary); margin-top: 20px; font-size: 14px;">
                                    💡 Tip: Press <strong>T</strong> key to quickly cycle through themes
                                </p>
                            </div>
                        </div>
                    </div>
                `;

            default:
                return `
                    <div style="padding: 40px 20px; text-align: center;">
                        <h2>📄 ${page.charAt(0).toUpperCase() + page.slice(1)} Page</h2>
                        <p style="color: var(--text-secondary); margin: 20px 0;">
                            ${mode} - This page requires connection to the ESP32 device.
                        </p>
                    </div>
                `;
        }
    },

    /**
     * Get theme gradient for preview
     */
    getThemeGradient(theme) {
        const gradients = {
            'light': 'linear-gradient(135deg, #ffffff 0%, #f8fafc 100%)',
            'dark': 'linear-gradient(135deg, #1e293b 0%, #0f172a 100%)',
            'high-contrast': 'linear-gradient(135deg, #000000 0%, #ffffff 100%)',
            'colorblind': 'linear-gradient(135deg, #0173b2 0%, #de8f05 100%)'
        };
        return gradients[theme] || gradients.light;
    },

    /**
     * Get offline mode HTML
     */
    getOfflineHTML() {
        return `
            <div style="padding: 40px 20px; text-align: center;">
                <h2>📡 Offline Mode</h2>
                <p style="color: var(--text-secondary); margin: 20px 0;">
                    Cannot load page content while offline.
                </p>
                <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                    <p style="margin: 10px 0; font-size: 14px;">
                        <strong>Mock Mode:</strong> ✗ Disabled
                    </p>
                    <p style="margin: 10px 0; font-size: 14px;">
                        <strong>Network:</strong> ✗ Offline
                    </p>
                </div>
                <p style="color: var(--text-secondary); margin-top: 20px; font-size: 14px;">
                    Enable mock mode with <strong>M</strong> key to preview dashboard with simulated data.
                </p>
            </div>
        `;
    },

    /**
     * Get error page HTML
     */
    getErrorHTML(message) {
        return `
            <div style="padding: 40px 20px; text-align: center; color: var(--color-critical);">
                <h2>❌ Error Loading Page</h2>
                <p>${message || 'An error occurred'}</p>
                <p style="font-size: 14px; margin-top: 20px;">
                    Make sure the device is online or enable <strong>Mock Mode</strong> (press <strong>M</strong>)
                </p>
            </div>
        `;
    }
};

// Expose globally
window.FallbackPages = FallbackPages;


// --- shared/router.js ---
;/**
 * @file shared/router.js
 * @brief Client-side router for page navigation
 * @description Refactored to use FallbackPages for fallback HTML templates
 */

console.log('[ROUTER] router.js loading...');

class Router {
    static routes = {
        'dashboard': { file: 'pages/dashboard/dashboard.html', js: 'pages/dashboard/dashboard.js' },
        'gcode': { file: 'pages/gcode/gcode.html', js: 'pages/gcode/gcode.js' },
        'motion': { file: 'pages/motion/motion.html', js: 'pages/motion/motion.js' },
        'diagnostics': { file: 'pages/diagnostics/diagnostics.html', js: 'pages/diagnostics/diagnostics.js' },
        'network': { file: 'pages/network/network.html', js: 'pages/network/network.js' },
        'system': { file: 'pages/system/system.html', js: 'pages/system/system.js' },
        'maintenance': { file: 'pages/maintenance/maintenance.html', js: 'pages/maintenance/maintenance.js' },
        'logs': { file: 'pages/logs/logs.html', js: 'pages/logs/logs.js' },
        'hardware': { file: 'pages/hardware/hardware.html', js: 'pages/hardware/hardware.js' },
        'settings': { file: 'pages/settings/settings.html', js: 'pages/settings/settings.js' }
    };

    static currentPage = null;
    static currentModule = null;
    static isLoading = false;

    static init() {
        console.log('[ROUTER] Initializing router');
        window.addEventListener('hashchange', () => this.navigate());
        this.navigate();
    }

    static async navigate(page = null) {
        console.log('[ROUTER] Navigate called, page:', page, 'isLoading:', this.isLoading);
        page = page || window.location.hash.slice(1) || 'dashboard';

        if (!this.routes[page]) {
            console.warn(`[ROUTER] Unknown page: ${page}`);
            window.location.hash = '#dashboard';
            return;
        }

        if (this.isLoading) {
            console.log('[ROUTER] Already loading, skipping duplicate navigate request');
            return;
        }
        this.isLoading = true;

        try {
            // Cleanup previous page
            if (this.currentModule && this.currentModule.cleanup) {
                this.currentModule.cleanup();
            }

            const route = this.routes[page];
            const container = document.getElementById('page-container');

            // Detect file:// protocol - fetch will fail, so skip it and use direct script loading
            const isFileProtocol = window.location.protocol === 'file:';

            // Load HTML
            let html;
            let fetchFailed = false;

            // Skip fetch if file:// protocol or mock mode is enabled
            if (isFileProtocol || window.MockMode?.enabled) {
                fetchFailed = true;
                console.log(`[ROUTER] ${isFileProtocol ? 'File protocol detected' : 'Mock mode enabled'} - using fallback HTML for:`, page);

                // Use FallbackPages module for fallback HTML
                html = window.FallbackPages?.getPageHTML(page, isFileProtocol) ||
                    `<div style="padding: 40px 20px; text-align: center;"><h2>${page} (Fallback)</h2></div>`;
            } else {
                try {
                    const htmlResponse = await fetch(route.file);
                    if (!htmlResponse.ok) throw new Error(`HTTP ${htmlResponse.status}`);
                    html = await htmlResponse.text();
                } catch (fetchError) {
                    fetchFailed = true;
                    console.warn(`[ROUTER] Failed to fetch ${route.file}:`, fetchError.message);

                    // If mock mode is enabled after fetch failed, use fallback HTML
                    if (window.MockMode?.enabled) {
                        console.log('[ROUTER] Mock mode enabled after fetch failure, generating fallback structure for:', page);
                        html = window.FallbackPages?.getPageHTML(page, false) ||
                            `<div style="padding: 40px 20px; text-align: center;"><h2>${page} (Mock)</h2></div>`;
                    } else if (!navigator.onLine) {
                        // Offline but mock mode not enabled - show helpful message
                        html = window.FallbackPages?.getOfflineHTML() ||
                            `<div style="padding: 40px 20px; text-align: center;"><h2>Offline</h2></div>`;
                        container.innerHTML = html;
                        this.isLoading = false;
                        return;
                    } else {
                        throw fetchError;
                    }
                }
            }

            console.log('[ROUTER] Setting page content, HTML length:', html?.length || 0);
            container.innerHTML = html;
            console.log('[ROUTER] Page content set successfully');

            // Load CSS if exists (but don't fail if it doesn't)
            if (!fetchFailed) {
                const cssFile = route.file.replace('.html', '.css');
                this.loadCSS(cssFile).catch(() => {
                    // CSS not found, that's okay
                });
            }

            // Load JS module (this is what populates the page content)
            const script = document.createElement('script');
            script.src = route.js;
            script.onload = () => {
                this.currentPage = page;
                this.currentModule = window.currentPageModule || {};

                // Call init if exists
                if (this.currentModule.init) {
                    this.currentModule.init();
                }

                // Update nav
                this.updateNav(page);

                this.isLoading = false;
            };
            script.onerror = () => {
                console.error(`[ROUTER] Failed to load ${route.js}`);

                // If JS also fails and we're offline, show error
                if (fetchFailed && !navigator.onLine) {
                    container.innerHTML = window.FallbackPages?.getErrorHTML('Could not load page module') ||
                        '<div style="color: red;">Error loading page</div>';
                }
                this.isLoading = false;
            };
            document.body.appendChild(script);

        } catch (error) {
            console.error('[ROUTER] Navigation error:', error);
            const container = document.getElementById('page-container');
            container.innerHTML = window.FallbackPages?.getErrorHTML(error.message) ||
                `<div style="color: red;">Error: ${error.message}</div>`;
            this.isLoading = false;
        }
    }

    static loadCSS(href) {
        return new Promise((resolve, reject) => {
            // Check if already loaded
            if (document.querySelector(`link[href="${href}"]`)) {
                resolve();
                return;
            }

            const link = document.createElement('link');
            link.rel = 'stylesheet';
            link.href = href;
            link.onload = resolve;
            link.onerror = reject;
            document.head.appendChild(link);
        });
    }

    static updateNav(page) {
        document.querySelectorAll('.nav-item').forEach(item => {
            const href = item.getAttribute('href').slice(1);
            item.classList.toggle('active', href === page);
        });
    }

    static go(page) {
        window.location.hash = '#' + page;
    }
}

// Expose Router globally
window.Router = Router;

console.log('[ROUTER] Router class defined, typeof Router:', typeof Router);
console.log('[ROUTER] Router attached to window, typeof window.Router:', typeof window.Router);


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


// --- shared/mock-data.js ---
;/**
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


// --- shared/position-viz.js ---
;/**
 * Real-Time Position Visualization
 * Displays 2D/3D workspace view with current position indicator
 */

class PositionVisualizer {
    constructor(canvasId, options = {}) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) {
            console.error(`[PosViz] Canvas element '${canvasId}' not found`);
            return;
        }

        this.ctx = this.canvas.getContext('2d');
        this.width = this.canvas.width;
        this.height = this.canvas.height;

        // Configuration
        this.config = {
            x_min: options.x_min || -100,
            x_max: options.x_max || 500,
            y_min: options.y_min || -100,
            y_max: options.y_max || 500,
            z_min: options.z_min || 0,
            z_max: options.z_max || 100,
            showGrid: options.showGrid !== false,
            showLimits: options.showLimits !== false,
            showTrail: options.showTrail !== false,
            ...options
        };

        // Current position
        this.position = {
            x: this.config.x_min,
            y: this.config.y_min,
            z: this.config.z_min,
            a: 0
        };

        // Trail tracking (last 100 points)
        this.trail = [];
        this.maxTrailLength = 100;

        // Colors
        this.colors = {
            bg: getComputedStyle(document.documentElement).getPropertyValue('--bg-primary') || '#ffffff',
            grid: getComputedStyle(document.documentElement).getPropertyValue('--border-color') || '#e5e7eb',
            limit: getComputedStyle(document.documentElement).getPropertyValue('--color-warning') || '#f59e0b',
            position: getComputedStyle(document.documentElement).getPropertyValue('--color-optimal') || '#10b981',
            trail: getComputedStyle(document.documentElement).getPropertyValue('--color-normal') || '#3b82f6',
            text: getComputedStyle(document.documentElement).getPropertyValue('--text-primary') || '#000000'
        };

        // Handle resize
        this.resizeObserver = new ResizeObserver(() => this.handleResize());
        this.resizeObserver.observe(this.canvas);

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

    updatePosition(x, y, z, a) {
        this.position.x = x;
        this.position.y = y;
        this.position.z = z;
        this.position.a = a || 0;

        // Add to trail
        if (this.config.showTrail) {
            this.trail.push({ x, y, z });
            if (this.trail.length > this.maxTrailLength) {
                this.trail.shift();
            }
        }

        this.draw();
    }

    // Convert workspace coordinates to canvas coordinates
    toCanvasX(x) {
        const range = this.config.x_max - this.config.x_min;
        const normalized = (x - this.config.x_min) / range;
        const padding = 40;
        return padding + normalized * (this.width - 2 * padding);
    }

    toCanvasY(y) {
        const range = this.config.y_max - this.config.y_min;
        const normalized = (y - this.config.y_min) / range;
        const padding = 40;
        // Invert Y axis (canvas Y increases downward)
        return this.height - padding - normalized * (this.height - 2 * padding);
    }

    draw() {
        // Clear canvas
        this.ctx.fillStyle = this.colors.bg;
        this.ctx.fillRect(0, 0, this.width, this.height);

        // Draw grid
        if (this.config.showGrid) {
            this.drawGrid();
        }

        // Draw soft limits
        if (this.config.showLimits) {
            this.drawLimits();
        }

        // Draw trail
        if (this.config.showTrail && this.trail.length > 1) {
            this.drawTrail();
        }

        // Draw axes labels
        this.drawLabels();

        // Draw current position
        this.drawPosition();

        // Draw info box
        this.drawInfoBox();
    }

    drawGrid() {
        this.ctx.strokeStyle = this.colors.grid;
        this.ctx.lineWidth = 1;
        this.ctx.globalAlpha = 0.3;

        const gridStep = 50; // Draw grid every 50mm
        const range_x = this.config.x_max - this.config.x_min;
        const range_y = this.config.y_max - this.config.y_min;

        // Vertical lines (X axis)
        let x = Math.ceil(this.config.x_min / gridStep) * gridStep;
        while (x <= this.config.x_max) {
            const canvasX = this.toCanvasX(x);
            this.ctx.beginPath();
            this.ctx.moveTo(canvasX, 40);
            this.ctx.lineTo(canvasX, this.height - 40);
            this.ctx.stroke();
            x += gridStep;
        }

        // Horizontal lines (Y axis)
        let y = Math.ceil(this.config.y_min / gridStep) * gridStep;
        while (y <= this.config.y_max) {
            const canvasY = this.toCanvasY(y);
            this.ctx.beginPath();
            this.ctx.moveTo(40, canvasY);
            this.ctx.lineTo(this.width - 40, canvasY);
            this.ctx.stroke();
            y += gridStep;
        }

        this.ctx.globalAlpha = 1;
    }

    drawLimits() {
        // Draw soft limit boundaries as dashed rectangles
        this.ctx.strokeStyle = this.colors.limit;
        this.ctx.lineWidth = 2;
        this.ctx.setLineDash([5, 5]);
        this.ctx.globalAlpha = 0.5;

        const x1 = this.toCanvasX(this.config.x_min);
        const y1 = this.toCanvasY(this.config.y_max);
        const x2 = this.toCanvasX(this.config.x_max);
        const y2 = this.toCanvasY(this.config.y_min);

        this.ctx.strokeRect(x1, y1, x2 - x1, y2 - y1);

        this.ctx.setLineDash([]);
        this.ctx.globalAlpha = 1;
    }

    drawTrail() {
        this.ctx.strokeStyle = this.colors.trail;
        this.ctx.lineWidth = 1.5;
        this.ctx.globalAlpha = 0.6;

        this.ctx.beginPath();
        for (let i = 0; i < this.trail.length; i++) {
            const point = this.trail[i];
            const canvasX = this.toCanvasX(point.x);
            const canvasY = this.toCanvasY(point.y);

            if (i === 0) {
                this.ctx.moveTo(canvasX, canvasY);
            } else {
                this.ctx.lineTo(canvasX, canvasY);
            }
        }
        this.ctx.stroke();

        this.ctx.globalAlpha = 1;
    }

    drawLabels() {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = '12px Arial';
        this.ctx.textAlign = 'center';
        this.ctx.globalAlpha = 0.5;

        // X axis labels
        const step_x = 100;
        let x = Math.ceil(this.config.x_min / step_x) * step_x;
        while (x <= this.config.x_max) {
            const canvasX = this.toCanvasX(x);
            this.ctx.fillText(x + 'mm', canvasX, this.height - 20);
            x += step_x;
        }

        // Y axis labels
        this.ctx.textAlign = 'right';
        const step_y = 100;
        let y = Math.ceil(this.config.y_min / step_y) * step_y;
        while (y <= this.config.y_max) {
            const canvasY = this.toCanvasY(y);
            this.ctx.fillText(y + 'mm', 25, canvasY + 4);
            y += step_y;
        }

        this.ctx.globalAlpha = 1;
    }

    drawPosition() {
        const x = this.toCanvasX(this.position.x);
        const y = this.toCanvasY(this.position.y);

        // Draw position circle
        this.ctx.fillStyle = this.colors.position;
        this.ctx.beginPath();
        this.ctx.arc(x, y, 8, 0, 2 * Math.PI);
        this.ctx.fill();

        // Draw crosshair
        this.ctx.strokeStyle = this.colors.position;
        this.ctx.lineWidth = 2;
        this.ctx.globalAlpha = 0.7;

        this.ctx.beginPath();
        this.ctx.moveTo(x - 15, y);
        this.ctx.lineTo(x + 15, y);
        this.ctx.stroke();

        this.ctx.beginPath();
        this.ctx.moveTo(x, y - 15);
        this.ctx.lineTo(x, y + 15);
        this.ctx.stroke();

        this.ctx.globalAlpha = 1;
    }

    drawInfoBox() {
        const padding = 10;
        const lineHeight = 18;
        const box_width = 150;
        const box_height = 90;

        // Semi-transparent background
        this.ctx.fillStyle = 'rgba(0, 0, 0, 0.3)';
        this.ctx.fillRect(padding, padding, box_width, box_height);

        // Border
        this.ctx.strokeStyle = this.colors.text;
        this.ctx.lineWidth = 1;
        this.ctx.globalAlpha = 0.5;
        this.ctx.strokeRect(padding, padding, box_width, box_height);
        this.ctx.globalAlpha = 1;

        // Text
        this.ctx.fillStyle = this.colors.position;
        this.ctx.font = 'bold 12px Arial';
        this.ctx.textAlign = 'left';

        let y_pos = padding + 12;
        this.ctx.fillText(`X: ${this.position.x.toFixed(1)} mm`, padding + 5, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`Y: ${this.position.y.toFixed(1)} mm`, padding + 5, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`Z: ${this.position.z.toFixed(1)} mm`, padding + 5, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`A: ${this.position.a.toFixed(1)}°`, padding + 5, y_pos);
    }

    // Reset trail
    clearTrail() {
        this.trail = [];
        this.draw();
    }

    // Cleanup
    destroy() {
        if (this.resizeObserver) {
            this.resizeObserver.disconnect();
        }
    }
}

