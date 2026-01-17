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
            await this.loadScript(base + 'material-presets.js');
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
            AlertManager.add(window.i18n.t('settings.test_alert_msg'), 'warning', 3000);
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

        // Configuration Management
        document.getElementById('load-preset-btn')?.addEventListener('click', () => this.loadPreset());

        // CLI Options (includes OTA toggle now)
        document.getElementById('save-cli-options-btn')?.addEventListener('click', () => this.saveCliOptions());
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
        if (r >= -30) return window.i18n.t('settings.rssi_excellent');
        if (r >= -67) return window.i18n.t('settings.rssi_good');
        if (r >= -70) return window.i18n.t('settings.rssi_fair');
        if (r >= -80) return window.i18n.t('settings.rssi_weak');
        return window.i18n.t('settings.rssi_very_weak');
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
        if (!confirm(window.i18n.t('settings.confirm_clear_cache'))) return;
        const keep = ['themeSettings', 'userSettings'];
        for (const k in localStorage) if (localStorage.hasOwnProperty(k) && !keep.includes(k)) localStorage.removeItem(k);
        this.updateStorageInfo();
        AlertManager.add(window.i18n.t('settings.cache_cleared'), 'success', 2000);
    },

    factoryReset() {
        if (!confirm(window.i18n.t('settings.confirm_reset')) || !confirm(window.i18n.t('settings.confirm_sure'))) return;
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
        catch (e) { AlertManager.add(window.i18n.t('settings.save_failed'), 'warning'); }
    },

    loadConfiguration() {
        console.log('[Settings] Loading configuration');
        Promise.all([
            this.loadMotionConfig?.() || Promise.resolve(),
            this.loadEncoderConfig?.() || Promise.resolve(),
            this.loadSpindleAlarmConfig?.() || Promise.resolve(),
            this.loadCliOptions(),
            this.loadOtaSettings()
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
        if (el) { el.textContent = ''; el.className = 'card-status'; }
    },

    setStatusError(section, msg) {
        const el = document.getElementById(`${section}-status`);
        if (el) { el.textContent = '✗ ' + msg; el.className = 'card-status error'; }
    },

    cleanup() { console.log('[Settings] Cleaning up'); },

    loadPreset() {
        const presetId = document.getElementById('preset-select').value;
        if (!presetId || !window.MaterialPresets[presetId]) {
            AlertManager.add(window.i18n.t('settings.select_valid_preset'), 'warning');
            return;
        }

        const preset = window.MaterialPresets[presetId];
        console.log('[Settings] Applying preset:', preset.name);

        // Apply Spindle Settings
        if (preset.spindle) {
            const tb = document.getElementById('spindle-toolbreak-threshold');
            const st = document.getElementById('spindle-stall-threshold');
            const to = document.getElementById('spindle-stall-timeout');

            if (tb) { tb.value = preset.spindle.toolbreak_threshold; document.getElementById('toolbreak-value').textContent = preset.spindle.toolbreak_threshold.toFixed(1); }
            if (st) { st.value = preset.spindle.stall_threshold; document.getElementById('stall-threshold-value').textContent = preset.spindle.stall_threshold; }
            if (to) { to.value = preset.spindle.stall_timeout_ms; document.getElementById('stall-timeout-value').textContent = preset.spindle.stall_timeout_ms; }
        }

        AlertManager.add(`"${preset.name}": ` + window.i18n.t('settings.preset_loaded'), 'info', 3000);
    },

    async loadCliOptions() {
        try {
            const res = await fetch('/api/config/get?category=6');
            if (res.ok) {
                const data = await res.json();
                const toggle = document.getElementById('cli-echo-toggle');
                if (toggle && data.config) toggle.checked = (data.config.cli_echo === 1);
            }
        } catch (e) {
            console.warn('[Settings] CLI options load failed:', e);
        }
    },

    async saveCliOptions() {
        const echoToggle = document.getElementById('cli-echo-toggle');
        const otaToggle = document.getElementById('ota-check-toggle');

        try {
            // Save CLI echo
            if (echoToggle) {
                await fetch('/api/config/set', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ category: 6, key: 'cli_echo', value: echoToggle.checked ? 1 : 0 })
                });
            }

            // Save OTA check
            if (otaToggle) {
                await fetch('/api/config/set', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ category: 6, key: 'ota_chk_en', value: otaToggle.checked ? 1 : 0 })
                });
            }

            AlertManager.add(window.i18n.t('settings.cli_saved'), 'success', 2000);
        } catch (e) {
            this.showError('cli-options', window.i18n.t('settings.network_error') + ' ' + e.message);
        }
    },

    async loadOtaSettings() {
        try {
            const res = await fetch('/api/config/get?category=6');
            if (res.ok) {
                const data = await res.json();
                const toggle = document.getElementById('ota-check-toggle');
                if (toggle && data.config) toggle.checked = (data.config.ota_chk_en === 1);
            }
        } catch (e) {
            console.warn('[Settings] OTA settings load failed:', e);
        }
    },

    async saveOtaSettings() {
        const toggle = document.getElementById('ota-check-toggle');
        if (!toggle) return;

        try {
            const res = await fetch('/api/config/set', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ category: 6, key: 'ota_chk_en', value: toggle.checked ? 1 : 0 })
            });

            const data = await res.json();
            if (res.ok && !data.error) {
                AlertManager.add(window.i18n.t('settings.ota_saved'), 'success', 2000);
            } else {
                this.showError('ota-settings', window.i18n.t('settings.save_error') + ' ' + (data.error || 'Unknown error'));
            }
        } catch (e) {
            this.showError('ota-settings', window.i18n.t('settings.network_error') + ' ' + e.message);
        }
    }
};

window.currentPageModule = SettingsModule;
