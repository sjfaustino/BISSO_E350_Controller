'use strict';
/**
 * core.js - Core JavaScript modules for BISSO E350 Web UI
 * Contains Utils, AppState, ThemeManager, and Router
 */

// --- Utils ---
const Utils = {
    /**
     * Sets the text content of an element.
     * @param {string} id - Element ID
     * @param {string} value - Text to set
     * @returns {boolean} True if element found
     */
    setText(id, value) {
        const el = document.getElementById(id);
        if (el) { el.textContent = value; return true; }
        return false;
    },

    /**
     * Sets the value of an input element.
     * @param {string} id - Element ID
     * @param {any} value - Value to set
     * @returns {boolean} True if element found
     */
    setValue(id, value) {
        const el = document.getElementById(id);
        if (el) { el.value = value; return true; }
        return false;
    },

    /**
     * Sets a style property on an element.
     * @param {string} id - Element ID
     * @param {string} property - Style property name
     * @param {string} value - Style value
     * @returns {boolean} True if element found
     */
    setStyle(id, property, value) {
        const el = document.getElementById(id);
        if (el) { el.style[property] = value; return true; }
        return false;
    },

    /**
     * Gets an element by ID.
     * @param {string} id - Element ID
     * @returns {HTMLElement|null} The element
     */
    getElement(id) { return document.getElementById(id); },

    /**
     * Updates multiple elements' text content.
     * @param {object} updates - Map of ID to text value
     */
    updateElements(updates) {
        for (const [id, value] of Object.entries(updates)) this.setText(id, value);
    },

    /**
     * Creates a debounced function.
     * @param {function} func - Function to debounce
     * @param {number} wait - Wait time in ms
     * @returns {function} Debounced function
     */
    debounce(func, wait) {
        let timeout;
        return function (...args) {
            clearTimeout(timeout);
            timeout = setTimeout(() => func(...args), wait);
        };
    },

    /**
     * Creates a throttled function.
     * @param {function} func - Function to throttle
     * @param {number} limit - Limit time in ms
     * @returns {function} Throttled function
     */
    throttle(func, limit) {
        let inThrottle;
        return function (...args) {
            if (!inThrottle) {
                func.apply(this, args);
                inThrottle = true;
                setTimeout(() => inThrottle = false, limit);
            }
        };
    },

    /**
     * Formats bytes into human readable string.
     * @param {number} bytes - Size in bytes
     * @param {number} [decimals=2] - Number of decimals
     * @returns {string} Formatted string (e.g., "1.5 MB")
     */
    formatBytes(bytes, decimals = 2) {
        if (bytes === 0) return '0 Bytes';
        const k = 1024, dm = decimals < 0 ? 0 : decimals;
        const sizes = ['Bytes', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
    },

    /**
     * Formats duration in milliseconds to generic string.
     * @param {number} ms - Duration in ms
     * @returns {string} Formatted duration (e.g., "1h 30m")
     */
    formatDuration(ms) {
        const s = Math.floor(ms / 1000), m = Math.floor(s / 60), h = Math.floor(m / 60), d = Math.floor(h / 24);
        if (d > 0) return `${d}d ${h % 24}h`;
        if (h > 0) return `${h}h ${m % 60}m`;
        if (m > 0) return `${m}m ${s % 60}s`;
        return `${s}s`;
    },

    /**
     * Clamps a number between min and max.
     * @param {number} value - Input value
     * @param {number} min - Minimum
     * @param {number} max - Maximum
     * @returns {number} Clamped value
     */
    clamp(value, min, max) { return Math.min(Math.max(value, min), max); },

    /**
     * Escapes HTML characters in a string.
     * @param {string} text - Text to escape
     * @returns {string} Escaped HTML
     */
    escapeHtml(text) { const div = document.createElement('div'); div.textContent = text; return div.innerHTML; }
};
window.Utils = Utils;

// --- AppState ---
// --- AppState ---
/**
 * Global application state management.
 * Handles state updates, history recording, and subscription notifications.
 */
class AppState {
    /**
     * @property {object} data - The complete application state tree.
     */
    static data = {
        system: {
            status: 'INITIALIZING', health: 'unknown', cpu_percent: 0, free_heap_bytes: 0,
            firmware_version: '--', uptime_seconds: 0, plc_hardware_present: false,
            lcd_msg: '', lcd_msg_id: 0,
            hw_model: '', hw_mcu: '', hw_revision: '', hw_has_psram: false,
            hw_has_rtc: false, hw_has_oled: false, hw_has_sd: false, hw_eth_chip: 'unknown'
        },
        motion: { position: { x: 0, y: 0, z: 0, a: 0 }, moving: false, buffer_count: 0, buffer_capacity: 32, status: 'STOPPED' },
        safety: { estop: false, alarm: false },
        vfd: { current_amps: 0, frequency_hz: 0, thermal_percent: 0, fault_code: 0, stall_threshold: 0, calibration_valid: false, connected: false },
        axis: {
            x: { quality: 0, jitter_mms: 0, stalled: false, vfd_error_percent: 0 },
            y: { quality: 0, jitter_mms: 0, stalled: false, vfd_error_percent: 0 },
            z: { quality: 0, jitter_mms: 0, stalled: false, vfd_error_percent: 0 }
        },
        encoders: [], network: { wifi_connected: false, signal_percent: 0 }, load_state: 0, performance: { tasks: [] },
        ota: { available: false, latest_version: '', download_url: '', release_notes: '' },
        config: { http_auth: true, https: false, websocket: true, modbus: false }
    };
    static listeners = [];
    static history = [];
    static maxHistory = 1440;

    /**
     * Updates the application state with new data.
     * Performs deep merge and notifies subscribers.
     * @param {object} newData - Partial state object to merge
     */
    static update(newData) {
        this.data = this.deepMerge(this.data, newData);
        if (Math.random() < 0.1) this.recordHistory();
        this.notifyListeners('state-changed');
    }

    /**
     * Gets a value from the state by path.
     * @param {string} path - Dot-notation path (e.g. 'system.status')
     * @returns {any} The value at the path
     */
    static get(path) { return this.getNestedValue(this.data, path); }

    /**
     * Sets a value in the state by path and notifies listeners.
     * @param {string} path - Dot-notation path
     * @param {any} value - Value to set
     */
    static set(path, value) { this.setNestedValue(this.data, path, value); this.notifyListeners('state-changed'); }

    /**
     * Subscribes to state changes.
     * @param {function} callback - Function called on state change
     * @returns {function} Unsubscribe function
     */
    static subscribe(callback) { this.listeners.push(callback); return () => { this.listeners = this.listeners.filter(l => l !== callback); }; }

    /**
     * Deep merges source object into target object.
     * @param {object} target - Target object
     * @param {object} source - Source object
     * @returns {object} Merged object
     */
    static deepMerge(target, source) {
        const result = { ...target };
        for (const key in source) {
            if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
                result[key] = this.deepMerge(target[key] || {}, source[key]);
            } else { result[key] = source[key]; }
        }
        return result;
    }

    /**
     * Helper to retrieve nested value.
     * @param {object} obj - Object to traverse
     * @param {string} path - Dot-notation path
     * @returns {any} Value
     */
    static getNestedValue(obj, path) { return path.split('.').reduce((curr, prop) => curr?.[prop], obj); }
    static setNestedValue(obj, path, value) {
        const keys = path.split('.'), lastKey = keys.pop();
        const target = keys.reduce((curr, prop) => curr[prop] = curr[prop] || {}, obj);
        target[lastKey] = value;
    }
    static async checkForUpdates() {
        try {
            const response = await fetch('/api/ota/latest');
            if (response.ok) {
                const data = await response.json();
                this.update({ ota: data });

                if (data.available) {
                    this.showUpdateBanner(data);
                } else {
                    const banner = document.getElementById('ota-update-banner');
                    if (banner) banner.classList.add('hidden');
                }
            }
        } catch (error) {
            console.error('[OTA] Failed to check for updates:', error);
        }
    }

    static showUpdateBanner(data) {
        const banner = document.getElementById('ota-update-banner');
        if (banner) {
            banner.innerHTML = `
                <div class="ota-content">
                    <span class="ota-icon">ðŸš€</span>
                    <span class="ota-text">
                        <strong>Update Available:</strong> PosiPro ${data.latest_version || 'v?.?.?'} is ready to install.
                    </span>
                    <div class="ota-actions">
                        <button class="btn btn-sm btn-light" onclick="Router.go('system')">View Details</button>
                    </div>
                </div>
            `;
            banner.classList.remove('hidden');
        }
    }

    static recordHistory() {
        this.history.push({ timestamp: Date.now(), data: JSON.parse(JSON.stringify(this.data)) });
        if (this.history.length > this.maxHistory) this.history.shift();
    }
    static getHistory(minutes = 60) { const cutoff = Date.now() - minutes * 60 * 1000; return this.history.filter(h => h.timestamp > cutoff); }
    static notifyListeners(event) { window.dispatchEvent(new CustomEvent(event, { detail: this.data })); }
    static reset() { this.data = { ...this.constructor.data }; this.history = []; this.notifyListeners('state-reset'); }
}
window.addEventListener('telemetry', (event) => {
    const data = event.detail;
    // PHASE 3.2: Trigger toast notification for new LCD messages (M117)
    if (data.system && data.system.lcd_msg_id && data.system.lcd_msg) {
        const lastId = AppState.data.system?.lcd_msg_id || 0;
        if (data.system.lcd_msg_id !== lastId) {
            if (typeof AlertManager !== 'undefined') {
                AlertManager.add(data.system.lcd_msg, 'info', 10000);
            }
        }
    }
    AppState.update(data);
});

// --- ThemeManager ---
class ThemeManager {
    static themes = ['light', 'dark', 'high-contrast', 'colorblind'];
    static currentTheme = 'light';
    static settings = { theme: 'light', fontSize: 100, gridLines: false, dualAxis: true, soundAlerts: true, autoRefresh: true };
    static init() {
        this.loadSettings(); this.applyTheme(this.currentTheme); this.setFontSize(this.settings.fontSize);
    }
    static loadSettings() {
        const saved = localStorage.getItem('app-settings');
        if (saved) { try { this.settings = { ...this.settings, ...JSON.parse(saved) }; this.currentTheme = this.settings.theme; } catch (e) { } }
    }
    static saveSettings() { localStorage.setItem('app-settings', JSON.stringify(this.settings)); }
    static applyTheme(themeName) {
        if (!this.themes.includes(themeName)) return;
        document.documentElement.setAttribute('data-theme', themeName);
        document.body.className = '';
        if (themeName === 'dark') document.body.classList.add('dark-theme');
        else if (themeName === 'high-contrast') document.body.classList.add('high-contrast');
        else if (themeName === 'colorblind') document.body.classList.add('colorblind-mode');
        this.currentTheme = themeName; this.settings.theme = themeName; this.saveSettings();
    }
    static setFontSize(percent) {
        this.settings.fontSize = Math.max(80, Math.min(120, percent));
        document.documentElement.style.fontSize = (this.settings.fontSize / 100) * 16 + 'px';
        this.saveSettings();
    }
    static getSettings() { return { ...this.settings }; }
    static getCurrentTheme() { return this.currentTheme; }
}

// --- Router ---
class Router {
    static routes = {
        'dashboard': { file: 'pages/dashboard/dashboard.html', js: 'pages/dashboard/dashboard.js' },
        'gcode': { file: 'pages/gcode/gcode.html', js: 'pages/gcode/gcode.js' },
        'cut-planner': { file: 'pages/cut-planner/cut-planner.html', js: 'pages/cut-planner/cut-planner.js' },
        'motion': { file: 'pages/motion/motion.html', js: 'pages/motion/motion.js' },
        'homing': { file: 'pages/homing/homing.html', js: 'pages/homing/homing.js' },
        'diagnostics': { file: 'pages/diagnostics/diagnostics.html', js: 'pages/diagnostics/diagnostics.js' },
        'network': { file: 'pages/network/network.html', js: 'pages/network/network.js' },
        'system': { file: 'pages/system/system.html', js: 'pages/system/system.js' },
        'maintenance': { file: 'pages/maintenance/maintenance.html', js: 'pages/maintenance/maintenance.js' },
        'logs': { file: 'pages/logs/logs.html', js: 'pages/logs/logs.js' },
        'explorer': { file: 'pages/explorer/explorer.html', js: 'pages/explorer/explorer.js' },
        'hardware': { file: 'pages/hardware/hardware.html', js: 'pages/hardware/hardware.js' },
        'settings': { file: 'pages/settings/settings.html', js: 'pages/settings/settings.js' }
    };
    static currentPage = null;
    static currentModule = null;
    static isLoading = false;
    static init() {
        window.addEventListener('hashchange', () => this.navigate());
        this.navigate();
    }
    static async navigate(page = null) {
        page = page || window.location.hash.slice(1) || 'dashboard';
        if (!this.routes[page]) { window.location.hash = '#dashboard'; return; }
        if (this.isLoading) return;

        // Ensure i18n is ready before injecting content
        if (window.i18n && window.i18n.ready) {
            await window.i18n.ready;
        }

        const route = this.routes[page];
        const container = document.getElementById('page-container');
        if (!container) return;

        this.isLoading = true;

        // CRITICAL FIX: Clear old page state before navigation starts
        if (this.currentModule && this.currentModule.cleanup) this.currentModule.cleanup();
        this.currentModule = null;
        window.currentPageModule = null;

        try {
            const htmlResponse = await fetch(route.file);
            if (!htmlResponse.ok) throw new Error(`HTTP ${htmlResponse.status}`);

            container.innerHTML = await htmlResponse.text();

            // Apply translations to the newly injected HTML
            if (window.i18n && window.i18n.updatePage) {
                window.i18n.updatePage();
            }

            const cssFile = route.file.replace('.html', '.css');
            this.loadCSS(cssFile).catch(() => { });

            const script = document.createElement('script');
            script.src = route.js;
            script.onload = () => {
                this.currentPage = page;
                this.currentModule = window.currentPageModule || {};
                if (this.currentModule.init) this.currentModule.init();
                this.updateNav(page);
                this.isLoading = false;
            };
            script.onerror = () => {
                container.innerHTML = `<div style="color: red; padding: 20px;">Error loading ${page}</div>`;
                this.isLoading = false;
            };
            document.body.appendChild(script);
        } catch (error) {
            document.getElementById('page-container').innerHTML = `<div style="color: red; padding: 20px;">Error: ${error.message}</div>`;
            this.isLoading = false;
        }
    }
    static loadCSS(href) {
        return new Promise((resolve, reject) => {
            if (document.querySelector(`link[href="${href}"]`)) { resolve(); return; }
            const link = document.createElement('link');
            link.rel = 'stylesheet'; link.href = href; link.onload = resolve; link.onerror = reject;
            document.head.appendChild(link);
        });
    }
    static updateNav(page) {
        document.querySelectorAll('.nav-item').forEach(item => {
            item.classList.toggle('active', item.getAttribute('href').slice(1) === page);
        });
    }
    static go(page) { window.location.hash = '#' + page; }
}
window.Router = Router;

// Initialize
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => { ThemeManager.init(); Router.init(); });
} else {
    ThemeManager.init(); Router.init();
    setTimeout(() => AppState.checkForUpdates(), 2000);

}

// PWA Service Worker
if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js').catch(() => { });
}
/**
/**
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
        const graphHeight = this.height - padding - 60; // 60px for bottom area (labels + legend)
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

        // Vertical grid lines (time) - FIXED RELATIVE GRID
        let timeStep = 60000; // Default 1 minute
        if (this.config.timeWindow > 300000) timeStep = 180000;  // > 5m -> 3m
        if (this.config.timeWindow > 900000) timeStep = 300000;  // > 15m -> 5m
        if (this.config.timeWindow > 1800000) timeStep = 600000; // > 30m -> 10m

        // Iterate from 0 (Now) back to timeWindow
        for (let offset = 0; offset < this.config.timeWindow; offset += timeStep) {
            const t = maxTime - offset;
            const x = this.timeToCanvasX(t, minTime, maxTime);
            this.ctx.beginPath();
            this.ctx.moveTo(x, 50);
            this.ctx.lineTo(x, this.height - 60);
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
        if (range === 0) return 1;

        // Target roughly 5-8 grid lines
        const targetStep = range / 6;
        const magnitude = Math.pow(10, Math.floor(Math.log10(targetStep)));
        const normalized = targetStep / magnitude;

        let step;
        if (normalized < 1.5) step = 1 * magnitude;
        else if (normalized < 3.5) step = 2 * magnitude;
        else if (normalized < 7.5) step = 5 * magnitude;
        else step = 10 * magnitude;

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

        let timeStep = 60000; // Default 1 minute
        if (this.config.timeWindow > 300000) timeStep = 180000;  // > 5m -> 3m
        if (this.config.timeWindow > 900000) timeStep = 300000;  // > 15m -> 5m
        if (this.config.timeWindow > 1800000) timeStep = 600000; // > 30m -> 10m

        // Iterate from 0 (Now) back to timeWindow - STATIC LABELS
        for (let offset = 0; offset <= this.config.timeWindow; offset += timeStep) {
            const t = maxTime - offset;
            const x = this.timeToCanvasX(t, minTime, maxTime);

            // Convert offset to label
            const secondsAgo = Math.round(offset / 1000);
            let label = '';

            if (secondsAgo === 0) {
                label = 'Now';
            } else if (secondsAgo < 60) {
                label = secondsAgo + 's ago';
            } else if (secondsAgo < 3600) {
                label = Math.round(secondsAgo / 60) + 'm ago';
            } else {
                label = Math.round(secondsAgo / 3600) + 'h ago';
            }

            // Draw label above legend
            this.ctx.fillText(label, x, this.height - 42);
        }

        this.ctx.globalAlpha = 1;
    }

    drawAxes() {
        this.ctx.strokeStyle = this.colors.text;
        this.ctx.lineWidth = 2;

        // Y-axis
        this.ctx.beginPath();
        this.ctx.moveTo(50, 50);
        this.ctx.lineTo(50, this.height - 60);
        this.ctx.stroke();

        // X-axis
        this.ctx.beginPath();
        this.ctx.moveTo(50, this.height - 60);
        this.ctx.lineTo(this.width - 20, this.height - 60);
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

    drawLegend() {
        const startX = 20;
        const rowHeight = 16;
        let currentX = startX;
        let currentY = this.height - 28;
        const canvasWidth = this.width - 20;
        const boxSize = 10;
        const itemPadding = 15;

        this.ctx.font = '11px Arial';
        this.ctx.textAlign = 'left';

        this.series.forEach((series, seriesName) => {
            if (!series.visible) return;

            const labelWidth = this.ctx.measureText(seriesName).width + boxSize + 10;

            // Wrap to next row if it doesn't fit
            if (currentX + labelWidth > canvasWidth && currentX > startX) {
                currentX = startX;
                currentY += rowHeight;
            }

            // Draw color box
            this.ctx.fillStyle = series.color;
            this.ctx.globalAlpha = 0.8;
            this.ctx.fillRect(currentX, currentY, boxSize, boxSize);

            // Draw label
            this.ctx.fillStyle = this.colors.text;
            this.ctx.globalAlpha = 1;
            this.ctx.fillText(seriesName, currentX + boxSize + 5, currentY + boxSize - 1);

            currentX += labelWidth + itemPadding;
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
/**
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

        this.padding = 60; // Increased padding for axis labels

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
        return this.padding + normalized * (this.width - 2 * this.padding);
    }

    toCanvasY(y) {
        const range = this.config.y_max - this.config.y_min;
        const normalized = (y - this.config.y_min) / range;
        // Invert Y axis (canvas Y increases downward)
        return this.height - this.padding - normalized * (this.height - 2 * this.padding);
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
            this.ctx.moveTo(canvasX, this.padding);
            this.ctx.lineTo(canvasX, this.height - this.padding);
            this.ctx.stroke();
            x += gridStep;
        }

        // Horizontal lines (Y axis)
        let y = Math.ceil(this.config.y_min / gridStep) * gridStep;
        while (y <= this.config.y_max) {
            const canvasY = this.toCanvasY(y);
            this.ctx.beginPath();
            this.ctx.moveTo(this.padding, canvasY);
            this.ctx.lineTo(this.width - this.padding, canvasY);
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
            this.ctx.fillText(y + 'mm', this.padding - 8, canvasY + 4);
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

        // Move to top-right corner to avoid Y-axis overlap
        const x_pos = this.width - box_width - padding;
        const y_pos_start = padding;

        // Semi-transparent background
        this.ctx.fillStyle = 'rgba(0, 0, 0, 0.5)'; // Detached from theme for better contrast
        this.ctx.fillRect(x_pos, y_pos_start, box_width, box_height);

        // Border
        this.ctx.strokeStyle = this.colors.text;
        this.ctx.lineWidth = 1;
        this.ctx.globalAlpha = 0.5;
        this.ctx.strokeRect(x_pos, y_pos_start, box_width, box_height);
        this.ctx.globalAlpha = 1;

        // Text
        this.ctx.fillStyle = this.colors.position;
        this.ctx.font = 'bold 12px Arial';
        this.ctx.textAlign = 'left';

        let y_pos = y_pos_start + 20;

        // Use white text for better contrast on dark overlay
        this.ctx.fillStyle = '#ffffff';

        this.ctx.fillText(`X: ${this.position.x.toFixed(1)} mm`, x_pos + 10, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`Y: ${this.position.y.toFixed(1)} mm`, x_pos + 10, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`Z: ${this.position.z.toFixed(1)} mm`, x_pos + 10, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`A: ${this.position.a.toFixed(1)}Â°`, x_pos + 10, y_pos);
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
/**
 * @file shared/websocket.js
 * @brief Shared WebSocket connection for all pages
 * @details Single connection reused across all modules with exponential backoff reconnection
 */

class SharedWebSocket {
    static ws = null;
    static isConnected = false;
    static reconnectAttempts = 0;
    static maxReconnectAttempts = 10;
    static baseReconnectDelay = 1000;  // Start at 1 second
    static maxReconnectDelay = 30000;  // Cap at 30 seconds
    static currentReconnectDelay = 1000;
    static reconnectTimer = null;
    static listeners = [];
    static packetsSent = 0;
    static packetsReceived = 0;
    static dataReceivedBytes = 0;
    static latency = 0;
    static lastPingTime = 0;
    static lastMessageTime = Date.now();
    static pingInterval = null;
    static watchdogInterval = null;

    static connect() {
        // Clear any pending reconnect timer
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }

        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            return this.ws;
        }

        // Close existing connection if in bad state
        if (this.ws) {
            try { this.ws.close(); } catch (e) { }
            this.ws = null;
        }

        try {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = `${protocol}//${window.location.hostname}:${window.location.port}/ws`;

            console.log('[WS] Connecting to', wsUrl);
            this.ws = new WebSocket(wsUrl);

            this.ws.onopen = () => {
                this.isConnected = true;
                this.reconnectAttempts = 0;
                this.currentReconnectDelay = this.baseReconnectDelay; // Reset delay on success
                console.log('[WS] Connected');
                this.broadcast('ws-connected');

                // Show reconnection success if we had been reconnecting
                if (typeof AlertManager !== 'undefined' && this.reconnectAttempts > 0) {
                    AlertManager.add('WebSocket reconnected', 'success', 2000);
                }

                // Start pinging for latency and watchdog
                this.lastMessageTime = Date.now();
                this.startLatencyTracking();
                this.startWatchdog();
            };

            this.ws.onmessage = (event) => {
                this.packetsReceived++;
                this.lastMessageTime = Date.now();
                if (event.data) this.dataReceivedBytes += event.data.length;
                try {
                    const data = JSON.parse(event.data);

                    // Handle pong response
                    if (data.type === 'pong') {
                        if (this.lastPingTime > 0) {
                            this.latency = Date.now() - this.lastPingTime;
                        }
                        return;
                    }

                    this.broadcast('telemetry', data);
                } catch (e) {
                    console.error('[WS] Parse error:', e);
                }
            };

            this.ws.onerror = (error) => {
                console.error('[WS] Error:', error);
                this.broadcast('ws-error', error);
            };

            this.ws.onclose = (event) => {
                this.isConnected = false;
                console.log('[WS] Disconnected (code:', event.code, ')');
                this.stopWatchdog();
                this.broadcast('ws-disconnected');
                this.scheduleReconnect();
            };

            return this.ws;
        } catch (e) {
            console.error('[WS] Connection failed:', e);
            this.scheduleReconnect();
            return null;
        }
    }

    /**
     * Schedule reconnection with exponential backoff
     * Delay doubles each attempt: 1s, 2s, 4s, 8s, 16s, 30s (capped)
     */
    static scheduleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.error('[WS] Max reconnection attempts reached');
            this.broadcast('ws-failed');

            // Show persistent error to user
            if (typeof AlertManager !== 'undefined') {
                AlertManager.add('WebSocket connection failed. Refresh page to retry.', 'error', 10000);
            }
            return;
        }

        this.reconnectAttempts++;

        // Calculate exponential backoff delay
        this.currentReconnectDelay = Math.min(
            this.baseReconnectDelay * Math.pow(2, this.reconnectAttempts - 1),
            this.maxReconnectDelay
        );

        console.log(`[WS] Reconnecting in ${this.currentReconnectDelay}ms (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`);

        // Update UI if available
        this.broadcast('ws-reconnecting', {
            attempt: this.reconnectAttempts,
            maxAttempts: this.maxReconnectAttempts,
            delay: this.currentReconnectDelay
        });

        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this.connect();
        }, this.currentReconnectDelay);
    }

    /**
     * Force immediate reconnection (resets backoff)
     */
    static forceReconnect() {
        console.log('[WS] Force reconnect requested');
        this.reconnectAttempts = 0;
        this.currentReconnectDelay = this.baseReconnectDelay;

        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }

        if (this.ws) {
            try { this.ws.close(); } catch (e) { }
            this.ws = null;
        }

        this.connect();
    }

    static send(message) {
        if (this.ws && this.isConnected && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(message));
            this.packetsSent++;
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
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.isConnected = false;
        this.stopLatencyTracking();
        this.stopWatchdog();
    }

    static startLatencyTracking() {
        this.stopLatencyTracking();
        this.pingInterval = setInterval(() => {
            if (this.isConnected && this.ws.readyState === WebSocket.OPEN) {
                this.ping();
            }
        }, 5000); // Check latency every 5 seconds
    }

    static ping() {
        if (this.isConnected && this.ws.readyState === WebSocket.OPEN) {
            this.lastPingTime = Date.now();
            this.ws.send(JSON.stringify({ type: 'ping' }));
        }
    }

    static stopLatencyTracking() {
        if (this.pingInterval) {
            clearInterval(this.pingInterval);
            this.pingInterval = null;
        }
    }

    static startWatchdog() {
        this.stopWatchdog();
        this.watchdogInterval = setInterval(() => {
            if (this.isConnected) {
                const timeSinceLastMessage = Date.now() - this.lastMessageTime;
                if (timeSinceLastMessage > 15000) { // 15 seconds timeout
                    console.warn('[WS] Heartbeat timeout. Forcing reconnect...');
                    this.forceReconnect();
                }
            }
        }, 5000);
    }

    static stopWatchdog() {
        if (this.watchdogInterval) {
            clearInterval(this.watchdogInterval);
            this.watchdogInterval = null;
        }
    }

    /**
     * Get connection status for UI display
     */
    static getStatus() {
        return {
            connected: this.isConnected,
            reconnecting: this.reconnectTimer !== null,
            attempts: this.reconnectAttempts,
            maxAttempts: this.maxReconnectAttempts,
            nextRetryMs: this.currentReconnectDelay
        };
    }
}

// Auto-connect on load (skip for file:// protocol)
if (window.location.protocol !== 'file:') {
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => SharedWebSocket.connect());
    } else {
        SharedWebSocket.connect();
    }
}
/**
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

        // Listen for AlertManager events
        window.addEventListener('alert-added', (e) => {
            const alert = e.detail;
            // Map 'critical' to 'error' for Toast styling
            const type = alert.type === 'critical' ? 'error' : alert.type;
            const duration = alert.duration !== null ? alert.duration : 3000;
            this.show(alert.message, type, duration);
        });
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

        // Clear existing toasts if it's a transient message (not error)
        // or clear if we already have too many to prevent blocking the screen
        if (type !== 'error') {
            const existing = this.container.querySelectorAll('.toast');
            existing.forEach(t => {
                if (!t.classList.contains('toast-error')) this.remove(t);
            });
        }

        // Create toast element
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;

        // Icon based on type
        const icons = {
            success: 'âœ“',
            error: 'âœ•',
            warning: 'âš ',
            info: 'â„¹'
        };
        const icon = icons[type] || icons.info;

        toast.innerHTML = `
            <div class="toast-icon">${icon}</div>
            <div class="toast-message">${Utils.escapeHtml(message)}</div>
            <button class="toast-close" aria-label="Close">OK</button>
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
