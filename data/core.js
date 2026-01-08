'use strict';
/**
 * core.js - Core JavaScript modules for BISSO E350 Web UI
 * Contains Utils, AppState, ThemeManager, and Router
 */

// --- Utils ---
const Utils = {
    setText(id, value) {
        const el = document.getElementById(id);
        if (el) { el.textContent = value; return true; }
        return false;
    },
    setValue(id, value) {
        const el = document.getElementById(id);
        if (el) { el.value = value; return true; }
        return false;
    },
    setStyle(id, property, value) {
        const el = document.getElementById(id);
        if (el) { el.style[property] = value; return true; }
        return false;
    },
    getElement(id) { return document.getElementById(id); },
    updateElements(updates) {
        for (const [id, value] of Object.entries(updates)) this.setText(id, value);
    },
    debounce(func, wait) {
        let timeout;
        return function (...args) {
            clearTimeout(timeout);
            timeout = setTimeout(() => func(...args), wait);
        };
    },
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
    formatBytes(bytes, decimals = 2) {
        if (bytes === 0) return '0 Bytes';
        const k = 1024, dm = decimals < 0 ? 0 : decimals;
        const sizes = ['Bytes', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
    },
    formatDuration(ms) {
        const s = Math.floor(ms / 1000), m = Math.floor(s / 60), h = Math.floor(m / 60), d = Math.floor(h / 24);
        if (d > 0) return `${d}d ${h % 24}h`;
        if (h > 0) return `${h}h ${m % 60}m`;
        if (m > 0) return `${m}m ${s % 60}s`;
        return `${s}s`;
    },
    clamp(value, min, max) { return Math.min(Math.max(value, min), max); },
    escapeHtml(text) { const div = document.createElement('div'); div.textContent = text; return div.innerHTML; }
};
window.Utils = Utils;

// --- AppState ---
class AppState {
    static data = {
        system: { status: 'INITIALIZING', health: 'unknown', cpu_percent: 0, free_heap_bytes: 0, firmware_version: '--', uptime_seconds: 0, plc_hardware_present: false },
        motion: { position: { x: 0, y: 0, z: 0, a: 0 }, moving: false, status: 'STOPPED' },
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
    static update(newData) {
        this.data = this.deepMerge(this.data, newData);
        if (Math.random() < 0.1) this.recordHistory();
        this.notifyListeners('state-changed');
    }
    static get(path) { return this.getNestedValue(this.data, path); }
    static set(path, value) { this.setNestedValue(this.data, path, value); this.notifyListeners('state-changed'); }
    static subscribe(callback) { this.listeners.push(callback); return () => { this.listeners = this.listeners.filter(l => l !== callback); }; }
    static deepMerge(target, source) {
        const result = { ...target };
        for (const key in source) {
            if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
                result[key] = this.deepMerge(target[key] || {}, source[key]);
            } else { result[key] = source[key]; }
        }
        return result;
    }
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
                    <span class="ota-icon">🚀</span>
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
window.addEventListener('telemetry', (event) => AppState.update(event.detail));

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
        'motion': { file: 'pages/motion/motion.html', js: 'pages/motion/motion.js' },
        'homing': { file: 'pages/homing/homing.html', js: 'pages/homing/homing.js' },
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
        window.addEventListener('hashchange', () => this.navigate());
        this.navigate();
    }
    static async navigate(page = null) {
        page = page || window.location.hash.slice(1) || 'dashboard';
        if (!this.routes[page]) { window.location.hash = '#dashboard'; return; }
        if (this.isLoading) return;
        this.isLoading = true;

        // CRITICAL FIX: Clear old page state before navigation starts
        // This prevents the new HTML from having the "old" module's init() called on it
        if (this.currentModule && this.currentModule.cleanup) this.currentModule.cleanup();
        this.currentModule = null;
        window.currentPageModule = null;

        try {
            const route = this.routes[page];
            const container = document.getElementById('page-container');

            const htmlResponse = await fetch(route.file);
            if (!htmlResponse.ok) throw new Error(`HTTP ${htmlResponse.status}`);
            container.innerHTML = await htmlResponse.text();

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
