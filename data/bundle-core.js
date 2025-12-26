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

