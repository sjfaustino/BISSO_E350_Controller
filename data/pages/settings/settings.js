/**
 * settings.js - Settings Page Core Module
 * Manages user preferences, alerts, and system configuration
 * Motion/VFD/Encoder config loaded from settings-motion.js
 * Spindle alarm config loaded from settings-spindle.js
 */
window.SettingsModule = window.SettingsModule || {
    defaults: {
        theme: 'light', fontSize: 100, autoRefresh: true,
        qualityThreshold: 50, jitterThreshold: 1.0, tempThreshold: 80,
        soundAlerts: true, desktopAlerts: true,
        historyRetention: 24, chartResolution: 1, autoRefreshInterval: 1000
    },
    settings: {},

    init() {
        console.log('[Settings] Initializing');
        this.loadSettings();
        this.setupEventListeners();
        this.updateDisplay();
        // Load hardware config modules then load API data
        this.loadModules().then(() => this.loadConfiguration());
        window.addEventListener('state-changed', () => this.updateSystemInfo());
    },

    async loadModules() {
        const base = '/pages/settings/';
        try {
            await this.loadScript(base + 'settings-motion.js');
            await this.loadScript(base + 'settings-spindle.js');
        } catch (e) { console.warn('[Settings] Module load failed:', e); }
    },

    loadScript(src) {
        return new Promise((resolve, reject) => {
            const s = document.createElement('script');
            s.src = src;
            s.onload = resolve;
            s.onerror = reject;
            document.head.appendChild(s);
        });
    },

    setupEventListeners() {
        // Theme buttons
        document.querySelectorAll('.theme-option').forEach(btn => {
            btn.addEventListener('click', e => this.setTheme(e.currentTarget.dataset.theme));
        });

        // Font size slider
        const fontSlider = document.getElementById('font-size-slider');
        if (fontSlider) {
            fontSlider.addEventListener('input', e => {
                const size = parseInt(e.target.value);
                this.setFontSize(size);
                document.getElementById('font-size-display').textContent = size;
            });
        }

        // Alert threshold sliders
        this.bindSlider('quality-threshold', 'qualityThreshold', 'quality-value', parseInt);
        this.bindSlider('jitter-threshold', 'jitterThreshold', 'jitter-value', parseFloat, v => v.toFixed(1));
        this.bindSlider('temp-threshold', 'tempThreshold', 'temp-value', parseInt);

        // Checkboxes
        this.bindCheckbox('auto-refresh-toggle', 'autoRefresh');
        this.bindCheckbox('alert-sound-toggle', 'soundAlerts');
        this.bindCheckbox('alert-desktop-toggle', 'desktopAlerts');

        // Select menus
        this.bindSelect('history-retention', 'historyRetention');
        this.bindSelect('chart-resolution', 'chartResolution');

        // Buttons
        document.getElementById('test-alert-btn')?.addEventListener('click', () => {
            AlertManager.add('This is a test alert!', 'warning', 3000);
            if (this.settings.soundAlerts) this.playAlertSound();
        });
        document.getElementById('clear-cache-btn')?.addEventListener('click', () => this.clearCache());
        document.getElementById('factory-reset-btn')?.addEventListener('click', () => this.factoryReset());

        // Motion config buttons (delegate to module)
        document.getElementById('save-motion-btn')?.addEventListener('click', () => this.saveMotionSettings?.());
        document.getElementById('reset-motion-btn')?.addEventListener('click', () => this.resetMotionSettings?.());
        ['x-limit-low', 'x-limit-high', 'y-limit-low', 'y-limit-high', 'z-limit-low', 'z-limit-high'].forEach(id => {
            document.getElementById(id)?.addEventListener('change', () => this.hideError('motion'));
        });

        // VFD config buttons
        document.getElementById('save-vfd-btn')?.addEventListener('click', () => this.saveVfdSettings?.());
        document.getElementById('reset-vfd-btn')?.addEventListener('click', () => this.resetVfdSettings?.());
        this.bindDisplay('vfd-min-speed', 'vfd-min-display', 'vfd');
        this.bindDisplay('vfd-max-speed', 'vfd-max-display', 'vfd');
        this.bindDisplay('vfd-acc-time', 'vfd-acc-display', 'vfd');
        this.bindDisplay('vfd-dec-time', 'vfd-dec-display', 'vfd');

        // Encoder config buttons
        document.getElementById('calibrate-x-btn')?.addEventListener('click', () => this.calibrateEncoder?.(0));
        document.getElementById('calibrate-y-btn')?.addEventListener('click', () => this.calibrateEncoder?.(1));
        document.getElementById('calibrate-z-btn')?.addEventListener('click', () => this.calibrateEncoder?.(2));
        document.getElementById('reset-encoder-btn')?.addEventListener('click', () => this.resetEncoderSettings?.());
        this.bindDisplay('x-encoder-ppm', 'x-ppm-display', 'encoder');
        this.bindDisplay('y-encoder-ppm', 'y-ppm-display', 'encoder');
        this.bindDisplay('z-encoder-ppm', 'z-ppm-display', 'encoder');

        // Spindle alarm buttons
        document.getElementById('save-spindle-alarm-btn')?.addEventListener('click', () => this.saveSpindleAlarmSettings?.());
        document.getElementById('reset-spindle-alarm-btn')?.addEventListener('click', () => this.resetSpindleAlarmSettings?.());
        document.getElementById('clear-spindle-alarms-btn')?.addEventListener('click', () => this.clearSpindleAlarms?.());
        this.bindSpindleDisplay('spindle-toolbreak-threshold', 'toolbreak-value', v => parseFloat(v).toFixed(1));
        this.bindSpindleDisplay('spindle-stall-threshold', 'stall-threshold-value');
        this.bindSpindleDisplay('spindle-stall-timeout', 'stall-timeout-value');
    },

    // Helper: bind slider to settings property
    bindSlider(id, prop, displayId, parser, formatter = v => v) {
        const el = document.getElementById(id);
        if (el) el.addEventListener('input', e => {
            const v = parser(e.target.value);
            this.settings[prop] = v;
            document.getElementById(displayId).textContent = formatter(v);
            this.saveSettings();
        });
    },

    bindCheckbox(id, prop) {
        document.getElementById(id)?.addEventListener('change', e => {
            this.settings[prop] = e.target.checked;
            this.saveSettings();
        });
    },

    bindSelect(id, prop) {
        document.getElementById(id)?.addEventListener('change', e => {
            this.settings[prop] = parseInt(e.target.value);
            this.saveSettings();
        });
    },

    bindDisplay(inputId, displayId, section) {
        document.getElementById(inputId)?.addEventListener('input', e => {
            document.getElementById(displayId).textContent = e.target.value;
            this.hideError(section);
        });
    },

    bindSpindleDisplay(inputId, displayId, formatter = v => v) {
        document.getElementById(inputId)?.addEventListener('input', e => {
            document.getElementById(displayId).textContent = formatter(e.target.value);
            this.hideError('spindle-alarm');
        });
    },

    updateDisplay() {
        // Theme
        document.querySelectorAll('.theme-option').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.theme === this.settings.theme);
        });

        // Sliders
        this.setEl('font-size-slider', this.settings.fontSize, 'value');
        this.setEl('font-size-display', this.settings.fontSize);
        this.setEl('quality-threshold', this.settings.qualityThreshold, 'value');
        this.setEl('quality-value', this.settings.qualityThreshold);
        this.setEl('jitter-threshold', this.settings.jitterThreshold, 'value');
        this.setEl('jitter-value', this.settings.jitterThreshold.toFixed(1));
        this.setEl('temp-threshold', this.settings.tempThreshold, 'value');
        this.setEl('temp-value', this.settings.tempThreshold);

        // Checkboxes
        this.setEl('auto-refresh-toggle', this.settings.autoRefresh, 'checked');
        this.setEl('alert-sound-toggle', this.settings.soundAlerts, 'checked');
        this.setEl('alert-desktop-toggle', this.settings.desktopAlerts, 'checked');

        // Selects
        this.setEl('history-retention', this.settings.historyRetention, 'value');
        this.setEl('chart-resolution', this.settings.chartResolution, 'value');

        this.updateStorageInfo();
        this.updateSystemInfo();
    },

    setEl(id, val, prop = 'textContent') {
        const el = document.getElementById(id);
        if (el) el[prop] = val;
    },

    updateStorageInfo() {
        try {
            let used = 0;
            for (const k in localStorage) if (localStorage.hasOwnProperty(k)) used += localStorage[k].length + k.length;
            const usedKB = Math.round(used / 1024), limitKB = 5120;
            this.setEl('storage-used', usedKB);
            this.setEl('storage-limit', limitKB);
            const pct = (usedKB / limitKB) * 100;
            const fill = document.getElementById('storage-fill');
            if (fill) {
                fill.style.width = Math.min(100, pct) + '%';
                fill.style.background = pct > 80 ? 'var(--color-critical)' : pct > 60 ? 'var(--color-warning)' : 'var(--color-optimal)';
            }
        } catch (e) { }
    },

    updateSystemInfo() {
        const s = AppState.data;
        this.setEl('fw-version', s.system?.fw_version || 'Unknown');
        this.setEl('hw-version', s.system?.hw_version || 'E350 Rev A');
        const ms = s.system?.uptime_ms || 0;
        const d = Math.floor(ms / 86400000), h = Math.floor((ms % 86400000) / 3600000), m = Math.floor((ms % 3600000) / 60000);
        this.setEl('sys-uptime', `${d}d ${h}h ${m}m`);
        this.setEl('free-memory', ((s.system?.free_heap_bytes || 0) / 1024).toFixed(1) + ' KB');
        const rssi = s.network?.rssi || 0;
        this.setEl('signal-strength', `${this.getRssiDescription(rssi)} (${rssi} dBm)`);
        this.setEl('last-connection', new Date().toLocaleTimeString());
    },

    getRssiDescription(r) {
        if (r >= -30) return 'Excellent';
        if (r >= -67) return 'Good';
        if (r >= -70) return 'Fair';
        if (r >= -80) return 'Weak';
        return 'Very Weak';
    },

    setTheme(theme) {
        this.settings.theme = theme;
        ThemeManager.applyTheme(theme);
        this.saveSettings();
        document.querySelectorAll('.theme-option').forEach(btn => btn.classList.toggle('active', btn.dataset.theme === theme));
    },

    setFontSize(size) {
        this.settings.fontSize = size;
        ThemeManager.setFontSize(size);
        this.saveSettings();
    },

    playAlertSound() {
        try {
            const ctx = new (window.AudioContext || window.webkitAudioContext)();
            const osc = ctx.createOscillator(), gain = ctx.createGain();
            osc.connect(gain); gain.connect(ctx.destination);
            osc.frequency.value = 800; osc.type = 'sine';
            gain.gain.setValueAtTime(0.3, ctx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.01, ctx.currentTime + 0.5);
            osc.start(); osc.stop(ctx.currentTime + 0.5);
        } catch (e) { }
    },

    clearCache() {
        if (!confirm('Clear cache and history?')) return;
        const keep = ['themeSettings', 'userSettings'];
        for (const k in localStorage) if (localStorage.hasOwnProperty(k) && !keep.includes(k)) localStorage.removeItem(k);
        this.updateStorageInfo();
        AlertManager.add('Cache cleared', 'success', 2000);
    },

    factoryReset() {
        if (!confirm('Factory reset will clear ALL settings. Continue?') || !confirm('Are you sure?')) return;
        localStorage.clear();
        this.settings = { ...this.defaults };
        this.saveSettings();
        location.reload();
    },

    loadSettings() {
        try {
            this.settings = { ...this.defaults, ...JSON.parse(localStorage.getItem('userSettings') || '{}') };
        } catch (e) { this.settings = { ...this.defaults }; }
        ThemeManager.applyTheme(this.settings.theme);
        ThemeManager.setFontSize(this.settings.fontSize);
    },

    saveSettings() {
        try { localStorage.setItem('userSettings', JSON.stringify(this.settings)); }
        catch (e) { AlertManager.add('Failed to save settings', 'warning'); }
    },

    loadConfiguration() {
        console.log('[Settings] Loading configuration');
        Promise.all([
            this.loadMotionConfig?.() || Promise.resolve(),
            this.loadVfdConfig?.() || Promise.resolve(),
            this.loadEncoderConfig?.() || Promise.resolve(),
            this.loadSpindleAlarmConfig?.() || Promise.resolve()
        ]).catch(e => console.error('[Settings] Config load failed:', e));
    },

    showError(section, msg) {
        const el = document.getElementById(`${section}-error`);
        if (el) { el.textContent = msg; el.style.display = 'block'; }
    },

    hideError(section) {
        const el = document.getElementById(`${section}-error`);
        if (el) el.style.display = 'none';
    },

    setStatusLoaded(section) {
        const el = document.getElementById(`${section}-status`);
        if (el) { el.textContent = '✓ Loaded'; el.className = 'card-status loaded'; }
    },

    setStatusError(section, msg) {
        const el = document.getElementById(`${section}-status`);
        if (el) { el.textContent = '✗ ' + msg; el.className = 'card-status error'; }
    },

    cleanup() { console.log('[Settings] Cleaning up'); }
};

window.currentPageModule = SettingsModule;
