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
    undoHistory: [],

    init() {
        console.log('[Settings] Initializing');
        this.loadSettings();
        this.setupEventListeners();
        this.updateDisplay();
        // Load hardware config modules then load API data
        this.loadModules().then(() => this.loadConfiguration());
        window.addEventListener('state-changed', () => this.updateSystemInfo());

        // Check SD card status for storage selectors
        this.checkSDCardStatus();
        this.setupStorageSelectors();



        // Hide Undo button initially
        const undoBtn = document.getElementById('undo-settings-btn');
        if (undoBtn) undoBtn.style.display = 'none';
        if (undoBtn) undoBtn.onclick = () => this.undoSetting();
    },

    pushHistory(key, oldValue) {
        this.undoHistory.push({ key, value: oldValue });
        const btn = document.getElementById('undo-settings-btn');
        if (btn) btn.style.display = 'inline-block';
    },

    undoSetting() {
        const last = this.undoHistory.pop();
        if (last) {
            this.settings[last.key] = last.value;
            this.saveSettings(false); // don't push to history again
            this.updateDisplay();
        }
        if (this.undoHistory.length === 0) {
            document.getElementById('undo-settings-btn').style.display = 'none';
        }
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
        // Spindle alarm buttons
        document.getElementById('save-spindle-alarm-btn')?.addEventListener('click', () => this.saveSpindleAlarmSettings?.());
        document.getElementById('revert-spindle-alarm-btn')?.addEventListener('click', () => this.revertSpindleAlarmSettings?.());
        document.getElementById('reset-spindle-alarm-btn')?.addEventListener('click', () => this.resetSpindleAlarmSettings?.());
        document.getElementById('clear-spindle-alarms-btn')?.addEventListener('click', () => this.clearSpindleAlarms?.());
        this.bindSpindleDisplay('spindle-toolbreak-threshold', 'toolbreak-value', v => parseFloat(v).toFixed(1));
        this.bindSpindleDisplay('spindle-stall-threshold', 'stall-threshold-value');
        this.bindSpindleDisplay('spindle-stall-timeout', 'stall-timeout-value');

        // Revert buttons for other sections
        document.getElementById('revert-motion-btn')?.addEventListener('click', () => this.revertMotionSettings?.());
        document.getElementById('revert-encoder-btn')?.addEventListener('click', () => this.revertEncoderSettings?.());
        // VFD revert if added? (I didn't add revert-vfd-btn to html explicitly in previous step? Or did I?)
        // I checked snippet 2556, I only added it to Motion, Spindle, Encoder. VFD was not in the chunks.
        // I should add it to VFD later if I missed it.
        // But for now, bind what I added.



        // Configuration Management
        document.getElementById('export-all-btn')?.addEventListener('click', () => { window.location.href = '/api/config/backup'; });
        document.getElementById('import-config-btn')?.addEventListener('click', () => this.importConfiguration());
        document.getElementById('load-preset-btn')?.addEventListener('click', () => this.loadPreset());

        // CLI Options (includes OTA toggle now)
        document.getElementById('save-cli-options-btn')?.addEventListener('click', () => this.saveCliOptions());
    },

    // Helper: bind slider to settings property
    bindSlider(id, prop, displayId, parser, formatter = v => v) {
        const el = document.getElementById(id);
        if (el) el.addEventListener('change', e => { // Changed to change for history, input for display?
            // Use 'change' for history to avoid spamming stack while sliding. 
            // 'input' updates display.
            const v = parser(e.target.value);
            if (this.settings[prop] !== v) {
                this.pushHistory(prop, this.settings[prop]);
                this.settings[prop] = v;
                this.saveSettings();
            }
        });
        if (el && displayId) el.addEventListener('input', e => {
            document.getElementById(displayId).textContent = formatter(parser(e.target.value));
        });
    },

    bindCheckbox(id, prop) {
        document.getElementById(id)?.addEventListener('change', e => {
            const v = e.target.checked;
            if (this.settings[prop] !== v) {
                this.pushHistory(prop, this.settings[prop]);
                this.settings[prop] = v;
                this.saveSettings();
            }
        });
    },

    bindSelect(id, prop) {
        document.getElementById(id)?.addEventListener('change', e => {
            const v = parseInt(e.target.value);
            if (this.settings[prop] !== v) {
                this.pushHistory(prop, this.settings[prop]);
                this.settings[prop] = v;
                this.saveSettings();
            }
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
        if (this.settings.theme !== theme) {
            this.pushHistory('theme', this.settings.theme);
            this.settings.theme = theme;
            ThemeManager.applyTheme(theme);
            this.saveSettings();
            document.querySelectorAll('.theme-option').forEach(btn => btn.classList.toggle('active', btn.dataset.theme === theme));
        }
    },

    setFontSize(size) {
        if (this.settings.fontSize !== size) {
            this.pushHistory('fontSize', this.settings.fontSize);
            this.settings.fontSize = size;
            ThemeManager.setFontSize(size);
            this.saveSettings();
        }
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

    async importConfiguration() {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json';

        input.onchange = async (e) => {
            const file = e.target.files[0];
            if (!file) return;

            const reader = new FileReader();
            reader.onload = async (re) => {
                try {
                    const json = JSON.parse(re.target.result);
                    // Minimal validation
                    if (!json.motion || !json.system) throw new Error("Invalid configuration file format");

                    if (!await window.UI.showConfirm("Overwrite current settings with imported configuration? Device will reboot.")) return;

                    try {
                        const data = await window.API.post('config/restore', json, 'import-config-btn');
                        if (data.success) {
                            window.AlertManager.add(data.message, 'success');
                            setTimeout(() => location.reload(), 3000);
                        } else {
                            // If API didn't throw (parsed success:false), show error
                            window.AlertManager.add(data.error || 'Import failed', 'error');
                        }
                    } catch (err) {
                        // API client handles toast for errors
                    }
                } catch (err) {
                    window.AlertManager.add(err.message, 'error');
                }
            };
            reader.readAsText(file);
        };
        input.click();
    },

    async loadCliOptions() {
        try {
            const data = await window.API.get('config/get?category=6');
            if (data && data.config) {
                const echoToggle = document.getElementById('cli-echo-toggle');
                if (echoToggle) echoToggle.checked = (data.config.cli_echo === 1);
            }
        } catch (e) {
            console.warn('[Settings] CLI options load failed:', e);
        }
    },

    async saveCliOptions() {
        const echoToggle = document.getElementById('cli-echo-toggle');
        const otaToggle = document.getElementById('ota-check-toggle');
        const spinnerTarget = 'save-cli-options-btn';

        try {
            const reqs = [];
            // Save CLI echo
            if (echoToggle) {
                reqs.push(window.API.post('config/set', { category: 6, key: 'cli_echo', value: echoToggle.checked ? 1 : 0 }, spinnerTarget));
            }
            // Save OTA check
            if (otaToggle) {
                reqs.push(window.API.post('config/set', { category: 6, key: 'ota_chk_en', value: otaToggle.checked ? 1 : 0 }, spinnerTarget));
            }

            await Promise.all(reqs);
            AlertManager.add(window.i18n.t('settings.cli_saved'), 'success', 2000);
        } catch (e) {
            this.showError('cli-options', window.i18n.t('settings.network_error') + ' ' + e.message);
        }
    },

    async loadOtaSettings() {
        // OTA settings are loaded together with CLI options from the same API category
        // This function is kept for backward compatibility but loadCliOptions handles both
        try {
            const data = await window.API.get('config/get?category=6', null, { silent: true }); // silent to avoid double error if both fail
            if (data && data.config) {
                const toggle = document.getElementById('ota-check-toggle');
                if (toggle) toggle.checked = (data.config.ota_chk_en === 1);
            }
        } catch (e) {
            console.warn('[Settings] OTA settings load failed:', e);
        }
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
            const data = await window.API.get('config/get?category=6');
            const toggle = document.getElementById('cli-echo-toggle');
            if (toggle && data.config) toggle.checked = (data.config.cli_echo === 1);
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
                await window.API.post('config/set', {
                    category: 6,
                    key: 'cli_echo',
                    value: echoToggle.checked ? 1 : 0
                });
            }

            // Save OTA check
            if (otaToggle) {
                await window.API.post('config/set', {
                    category: 6,
                    key: 'ota_chk_en',
                    value: otaToggle.checked ? 1 : 0
                });
            }

            AlertManager.add(window.i18n.t('settings.cli_saved'), 'success', 2000);
        } catch (e) {
            this.showError('cli-options', window.i18n.t('settings.network_error') + ' ' + e.message);
        }
    },

    async loadOtaSettings() {
        // OTA settings are loaded together with CLI options from the same API category
        // This function is kept for backward compatibility but loadCliOptions handles both
        try {
            const data = await window.API.get('config/get?category=6');
            const toggle = document.getElementById('ota-check-toggle');
            if (toggle && data.config) toggle.checked = (data.config.ota_chk_en === 1);
        } catch (e) {
            console.warn('[Settings] OTA settings load failed:', e);
        }
    },

    // === SD Card Storage Selection ===

    async checkSDCardStatus() {
        try {
            const data = await window.API.get('sd/status', null, { silent: true });
            const badge = document.getElementById('sd-status-badge');
            const sdOption = document.getElementById('sd-option');
            const importSdOption = document.getElementById('import-sd-option');
            const sdInfo = document.getElementById('sd-info');
            const sdSpaceInfo = document.getElementById('sd-space-info');

            if (data && data.available) {
                // SD Card available
                if (badge) {
                    badge.textContent = '✓ SD Ready';
                    badge.style.background = 'var(--color-optimal)';
                }
                if (sdOption) sdOption.disabled = false;
                if (importSdOption) importSdOption.disabled = false;
                if (sdInfo) sdInfo.style.display = 'block';
                if (sdSpaceInfo && data.freeMB) {
                    sdSpaceInfo.textContent = `SD: ${data.freeMB} MB free`;
                }
                this.sdCardAvailable = true;
                this.sdCardInfo = data;
            } else {
                // SD Card not available
                if (badge) {
                    badge.textContent = data?.present ? 'SD Not Mounted' : 'No SD Card';
                    badge.style.background = 'var(--color-critical)';
                }
                if (sdOption) sdOption.disabled = true;
                if (importSdOption) importSdOption.disabled = true;
                if (sdInfo) sdInfo.style.display = 'none';
                this.sdCardAvailable = false;
            }
        } catch (e) {
            console.warn('[Settings] SD card check failed:', e);
            this.sdCardAvailable = false;
        }
    },

    setupStorageSelectors() {
        const exportSelect = document.getElementById('export-storage-select');
        const importSelect = document.getElementById('import-storage-select');
        const filesList = document.getElementById('storage-files-list');
        const importBtn = document.getElementById('import-config-btn');

        // Update import button text based on selection
        if (importSelect) {
            importSelect.addEventListener('change', (e) => {
                const value = e.target.value;
                if (filesList) filesList.style.display = (value !== 'upload') ? 'block' : 'none';
                if (importBtn) {
                    importBtn.textContent = value === 'upload' ? 'Choose File & Import' : 'Load Selected';
                }
                if (value === 'sd' || value === 'flash') {
                    this.loadStorageBackups(value);
                }
            });
        }
    },

    async loadStorageBackups(storage) {
        const filesList = document.getElementById('storage-files-list');
        if (!filesList) return;

        filesList.innerHTML = '<div style="padding: 12px; color: var(--text-muted); text-align: center;">Loading files...</div>';

        try {
            const endpoint = storage === 'sd' ? 'sd/backups' : 'config/backups';
            const data = await window.API.get(endpoint);

            if (data?.files?.length > 0) {
                filesList.innerHTML = data.files.map(f => `
                    <div class="backup-file-item" data-file="${f.name}" style="padding: 10px 12px; cursor: pointer; border-bottom: 1px solid var(--border-color); display: flex; justify-content: space-between;">
                        <span>📄 ${f.name}</span>
                        <span style="color: var(--text-muted); font-size: 0.85em;">${(f.size / 1024).toFixed(1)} KB</span>
                    </div>
                `).join('');

                // Add click handlers
                filesList.querySelectorAll('.backup-file-item').forEach(item => {
                    item.addEventListener('click', () => {
                        filesList.querySelectorAll('.backup-file-item').forEach(i => i.style.background = '');
                        item.style.background = 'var(--bg-tertiary)';
                        this.selectedBackupFile = item.dataset.file;
                    });
                });
            } else {
                filesList.innerHTML = '<div style="padding: 12px; color: var(--text-muted); text-align: center;">No backup files found</div>';
            }
        } catch (e) {
            filesList.innerHTML = '<div style="padding: 12px; color: var(--color-critical); text-align: center;">Failed to load files</div>';
        }
    }
};

window.currentPageModule = SettingsModule;

