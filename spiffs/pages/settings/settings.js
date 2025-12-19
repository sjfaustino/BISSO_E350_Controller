/**
 * Settings Page Module
 * Manages user preferences, alerts, and system configuration
 * Note: Use window.SettingsModule to avoid "already declared" errors when navigating
 */
window.SettingsModule = window.SettingsModule || {
    // Default settings
    defaults: {
        theme: 'light',
        fontSize: 100,
        autoRefresh: true,
        qualityThreshold: 50,
        jitterThreshold: 1.0,
        tempThreshold: 80,
        soundAlerts: true,
        desktopAlerts: true,
        historyRetention: 24,
        chartResolution: 1,
        autoRefreshInterval: 1000
    },

    settings: {},

    init() {
        console.log('[Settings] Initializing');
        this.loadSettings();
        this.setupEventListeners();
        this.setupConfigManagement();
        this.updateDisplay();
        this.loadConfiguration();
        window.addEventListener('state-changed', () => this.updateSystemInfo());
    },

    setupEventListeners() {
        // Theme buttons
        document.querySelectorAll('.theme-option').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const theme = e.currentTarget.dataset.theme;
                this.setTheme(theme);
            });
        });

        // Font size slider
        const fontSlider = document.getElementById('font-size-slider');
        if (fontSlider) {
            fontSlider.addEventListener('input', (e) => {
                const size = parseInt(e.target.value);
                this.setFontSize(size);
                document.getElementById('font-size-display').textContent = size;
            });
        }

        // Alert threshold sliders
        const qualitySlider = document.getElementById('quality-threshold');
        if (qualitySlider) {
            qualitySlider.addEventListener('input', (e) => {
                const value = parseInt(e.target.value);
                this.settings.qualityThreshold = value;
                document.getElementById('quality-value').textContent = value;
                this.saveSettings();
            });
        }

        const jitterSlider = document.getElementById('jitter-threshold');
        if (jitterSlider) {
            jitterSlider.addEventListener('input', (e) => {
                const value = parseFloat(e.target.value);
                this.settings.jitterThreshold = value;
                document.getElementById('jitter-value').textContent = value.toFixed(1);
                this.saveSettings();
            });
        }

        const tempSlider = document.getElementById('temp-threshold');
        if (tempSlider) {
            tempSlider.addEventListener('input', (e) => {
                const value = parseInt(e.target.value);
                this.settings.tempThreshold = value;
                document.getElementById('temp-value').textContent = value;
                this.saveSettings();
            });
        }

        // Checkboxes
        document.getElementById('auto-refresh-toggle')?.addEventListener('change', (e) => {
            this.settings.autoRefresh = e.target.checked;
            this.saveSettings();
        });

        document.getElementById('alert-sound-toggle')?.addEventListener('change', (e) => {
            this.settings.soundAlerts = e.target.checked;
            this.saveSettings();
        });

        document.getElementById('alert-desktop-toggle')?.addEventListener('change', (e) => {
            this.settings.desktopAlerts = e.target.checked;
            this.saveSettings();
        });

        // Mock mode toggle button
        const mockModeBtn = document.getElementById('toggle-mock-mode-btn');
        if (mockModeBtn) {
            mockModeBtn.addEventListener('click', () => {
                if (window.MockMode) {
                    MockMode.toggle();
                    this.updateMockModeUI();
                }
            });
            // Initial UI update
            this.updateMockModeUI();
        }

        // Select menus
        document.getElementById('history-retention')?.addEventListener('change', (e) => {
            this.settings.historyRetention = parseInt(e.target.value);
            this.saveSettings();
        });

        document.getElementById('chart-resolution')?.addEventListener('change', (e) => {
            this.settings.chartResolution = parseInt(e.target.value);
            this.saveSettings();
        });

        // Buttons
        document.getElementById('test-alert-btn')?.addEventListener('click', () => {
            AlertManager.add('This is a test alert!', 'warning', 3000);
            if (this.settings.soundAlerts) {
                this.playAlertSound();
            }
        });

        document.getElementById('clear-cache-btn')?.addEventListener('click', () => {
            this.clearCache();
        });

        document.getElementById('factory-reset-btn')?.addEventListener('click', () => {
            this.factoryReset();
        });

        // Motion control listeners
        document.getElementById('save-motion-btn')?.addEventListener('click', () => {
            this.saveMotionSettings();
        });

        document.getElementById('reset-motion-btn')?.addEventListener('click', () => {
            this.resetMotionSettings();
        });

        // Real-time input updates for motion (display feedback)
        ['x-limit-low', 'x-limit-high', 'y-limit-low', 'y-limit-high', 'z-limit-low', 'z-limit-high'].forEach(id => {
            document.getElementById(id)?.addEventListener('change', () => {
                this.hideError('motion');
            });
        });

        // VFD parameters listeners
        document.getElementById('save-vfd-btn')?.addEventListener('click', () => {
            this.saveVfdSettings();
        });

        document.getElementById('reset-vfd-btn')?.addEventListener('click', () => {
            this.resetVfdSettings();
        });

        // Real-time display updates for VFD
        document.getElementById('vfd-min-speed')?.addEventListener('input', (e) => {
            document.getElementById('vfd-min-display').textContent = e.target.value;
            this.hideError('vfd');
        });

        document.getElementById('vfd-max-speed')?.addEventListener('input', (e) => {
            document.getElementById('vfd-max-display').textContent = e.target.value;
            this.hideError('vfd');
        });

        document.getElementById('vfd-acc-time')?.addEventListener('input', (e) => {
            document.getElementById('vfd-acc-display').textContent = e.target.value;
            this.hideError('vfd');
        });

        document.getElementById('vfd-dec-time')?.addEventListener('input', (e) => {
            document.getElementById('vfd-dec-display').textContent = e.target.value;
            this.hideError('vfd');
        });

        // Encoder calibration listeners
        document.getElementById('calibrate-x-btn')?.addEventListener('click', () => {
            this.calibrateEncoder(0);
        });

        document.getElementById('calibrate-y-btn')?.addEventListener('click', () => {
            this.calibrateEncoder(1);
        });

        document.getElementById('calibrate-z-btn')?.addEventListener('click', () => {
            this.calibrateEncoder(2);
        });

        document.getElementById('reset-encoder-btn')?.addEventListener('click', () => {
            this.resetEncoderSettings();
        });

        // Real-time display updates for encoder
        document.getElementById('x-encoder-ppm')?.addEventListener('input', (e) => {
            document.getElementById('x-ppm-display').textContent = e.target.value;
            this.hideError('encoder');
        });

        document.getElementById('y-encoder-ppm')?.addEventListener('input', (e) => {
            document.getElementById('y-ppm-display').textContent = e.target.value;
            this.hideError('encoder');
        });

        document.getElementById('z-encoder-ppm')?.addEventListener('input', (e) => {
            document.getElementById('z-ppm-display').textContent = e.target.value;
            this.hideError('encoder');
        });
    },

    updateDisplay() {
        // Update theme selector
        document.querySelectorAll('.theme-option').forEach(btn => {
            btn.classList.remove('active');
            if (btn.dataset.theme === this.settings.theme) {
                btn.classList.add('active');
            }
        });

        // Update sliders and inputs
        document.getElementById('font-size-slider').value = this.settings.fontSize;
        document.getElementById('font-size-display').textContent = this.settings.fontSize;

        document.getElementById('quality-threshold').value = this.settings.qualityThreshold;
        document.getElementById('quality-value').textContent = this.settings.qualityThreshold;

        document.getElementById('jitter-threshold').value = this.settings.jitterThreshold;
        document.getElementById('jitter-value').textContent = this.settings.jitterThreshold.toFixed(1);

        document.getElementById('temp-threshold').value = this.settings.tempThreshold;
        document.getElementById('temp-value').textContent = this.settings.tempThreshold;

        // Update checkboxes
        document.getElementById('auto-refresh-toggle').checked = this.settings.autoRefresh;
        document.getElementById('alert-sound-toggle').checked = this.settings.soundAlerts;
        document.getElementById('alert-desktop-toggle').checked = this.settings.desktopAlerts;

        // Update selects
        document.getElementById('history-retention').value = this.settings.historyRetention;
        document.getElementById('chart-resolution').value = this.settings.chartResolution;

        // Update storage info
        this.updateStorageInfo();
        this.updateSystemInfo();
    },

    updateStorageInfo() {
        try {
            let used = 0;
            for (const key in localStorage) {
                if (localStorage.hasOwnProperty(key)) {
                    used += localStorage[key].length + key.length;
                }
            }
            const usedKB = Math.round(used / 1024);
            const limitKB = 5120; // 5MB typical browser quota per domain

            document.getElementById('storage-used').textContent = usedKB;
            document.getElementById('storage-limit').textContent = limitKB;

            const percent = (usedKB / limitKB) * 100;
            const fillElem = document.getElementById('storage-fill');
            if (fillElem) {
                fillElem.style.width = Math.min(100, percent) + '%';
                fillElem.style.background = percent > 80
                    ? 'var(--color-critical)'
                    : percent > 60
                    ? 'var(--color-warning)'
                    : 'var(--color-optimal)';
            }
        } catch (e) {
            console.warn('[Settings] Could not calculate storage:', e);
        }
    },

    updateSystemInfo() {
        const state = AppState.data;

        // Firmware and hardware versions (from telemetry)
        document.getElementById('fw-version').textContent =
            state.system?.fw_version || 'Unknown';
        document.getElementById('hw-version').textContent =
            state.system?.hw_version || 'E350 Rev A';

        // Uptime
        const uptimeMs = state.system?.uptime_ms || 0;
        const days = Math.floor(uptimeMs / (1000 * 60 * 60 * 24));
        const hours = Math.floor((uptimeMs % (1000 * 60 * 60 * 24)) / (1000 * 60 * 60));
        const minutes = Math.floor((uptimeMs % (1000 * 60 * 60)) / (1000 * 60));
        document.getElementById('sys-uptime').textContent =
            `${days}d ${hours}h ${minutes}m`;

        // Free memory
        const freeHeap = state.system?.free_heap_bytes || 0;
        document.getElementById('free-memory').textContent =
            (freeHeap / 1024).toFixed(1) + ' KB';

        // Network signal
        const rssi = state.network?.rssi || 0;
        const strength = this.getRssiDescription(rssi);
        document.getElementById('signal-strength').textContent = `${strength} (${rssi} dBm)`;

        // Last connection (now)
        const now = new Date();
        document.getElementById('last-connection').textContent =
            now.toLocaleTimeString();
    },

    getRssiDescription(rssi) {
        if (rssi >= -30) return 'Excellent';
        if (rssi >= -67) return 'Good';
        if (rssi >= -70) return 'Fair';
        if (rssi >= -80) return 'Weak';
        return 'Very Weak';
    },

    setTheme(theme) {
        this.settings.theme = theme;
        ThemeManager.applyTheme(theme);
        this.saveSettings();

        // Update visual indicator
        document.querySelectorAll('.theme-option').forEach(btn => {
            btn.classList.remove('active');
        });
        document.querySelector(`[data-theme="${theme}"]`).classList.add('active');
    },

    setFontSize(size) {
        this.settings.fontSize = size;
        ThemeManager.setFontSize(size);
        this.saveSettings();
    },

    playAlertSound() {
        // Create a simple beep using Web Audio API
        try {
            const audioContext = new (window.AudioContext || window.webkitAudioContext)();
            const oscillator = audioContext.createOscillator();
            const gainNode = audioContext.createGain();

            oscillator.connect(gainNode);
            gainNode.connect(audioContext.destination);

            oscillator.frequency.value = 800;
            oscillator.type = 'sine';

            gainNode.gain.setValueAtTime(0.3, audioContext.currentTime);
            gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.5);

            oscillator.start(audioContext.currentTime);
            oscillator.stop(audioContext.currentTime + 0.5);
        } catch (e) {
            console.warn('[Settings] Could not play alert sound:', e);
        }
    },

    clearCache() {
        if (confirm('Clear cache and history? This will remove all stored data.')) {
            const keysToKeep = ['themeSettings', 'userSettings'];
            for (const key in localStorage) {
                if (localStorage.hasOwnProperty(key) && !keysToKeep.includes(key)) {
                    localStorage.removeItem(key);
                }
            }
            this.updateStorageInfo();
            AlertManager.add('Cache cleared successfully', 'success', 2000);
        }
    },

    factoryReset() {
        if (confirm('Factory reset will clear ALL settings and data. Continue?') &&
            confirm('Are you sure? This cannot be undone.')) {
            localStorage.clear();
            this.settings = { ...this.defaults };
            this.saveSettings();
            location.reload();
        }
    },

    loadSettings() {
        try {
            const stored = JSON.parse(localStorage.getItem('userSettings') || '{}');
            this.settings = { ...this.defaults, ...stored };
        } catch (e) {
            console.warn('[Settings] Failed to load settings:', e);
            this.settings = { ...this.defaults };
        }

        // Apply theme and font size immediately
        ThemeManager.applyTheme(this.settings.theme);
        ThemeManager.setFontSize(this.settings.fontSize);
    },

    saveSettings() {
        try {
            localStorage.setItem('userSettings', JSON.stringify(this.settings));
        } catch (e) {
            console.warn('[Settings] Failed to save settings:', e);
            AlertManager.add('Failed to save settings (storage full)', 'warning');
        }
    },

    // =====================================================================
    // CONFIGURATION MANAGEMENT
    // =====================================================================

    loadConfiguration() {
        console.log('[Settings] Loading configuration');
        Promise.all([
            this.loadMotionConfig(),
            this.loadVfdConfig(),
            this.loadEncoderConfig()
        ]).catch(err => {
            console.error('[Settings] Failed to load configuration:', err);
        });
    },

    loadMotionConfig() {
        return fetch('/api/config/get?category=0')
            .then(r => r.json())
            .then(data => {
                if (data.success && data.config) {
                    const cfg = data.config;
                    document.getElementById('x-limit-low').value = cfg.soft_limit_low_mm[0];
                    document.getElementById('x-limit-high').value = cfg.soft_limit_high_mm[0];
                    document.getElementById('y-limit-low').value = cfg.soft_limit_low_mm[1];
                    document.getElementById('y-limit-high').value = cfg.soft_limit_high_mm[1];
                    document.getElementById('z-limit-low').value = cfg.soft_limit_low_mm[2];
                    document.getElementById('z-limit-high').value = cfg.soft_limit_high_mm[2];
                    this.setStatusLoaded('motion');
                }
            })
            .catch(err => {
                console.error('[Settings] Motion config load failed:', err);
                this.setStatusError('motion', 'Failed to load motion settings');
            });
    },

    loadVfdConfig() {
        return fetch('/api/config/get?category=1')
            .then(r => r.json())
            .then(data => {
                if (data.success && data.config) {
                    const cfg = data.config;
                    document.getElementById('vfd-min-speed').value = cfg.min_speed_hz;
                    document.getElementById('vfd-min-display').textContent = cfg.min_speed_hz;
                    document.getElementById('vfd-max-speed').value = cfg.max_speed_hz;
                    document.getElementById('vfd-max-display').textContent = cfg.max_speed_hz;
                    document.getElementById('vfd-acc-time').value = cfg.acc_time_ms;
                    document.getElementById('vfd-acc-display').textContent = cfg.acc_time_ms;
                    document.getElementById('vfd-dec-time').value = cfg.dec_time_ms;
                    document.getElementById('vfd-dec-display').textContent = cfg.dec_time_ms;
                    this.setStatusLoaded('vfd');
                }
            })
            .catch(err => {
                console.error('[Settings] VFD config load failed:', err);
                this.setStatusError('vfd', 'Failed to load VFD settings');
            });
    },

    loadEncoderConfig() {
        return fetch('/api/config/get?category=2')
            .then(r => r.json())
            .then(data => {
                if (data.success && data.config) {
                    const cfg = data.config;
                    document.getElementById('x-encoder-ppm').value = cfg.ppm[0];
                    document.getElementById('x-ppm-display').textContent = cfg.ppm[0];
                    document.getElementById('y-encoder-ppm').value = cfg.ppm[1];
                    document.getElementById('y-ppm-display').textContent = cfg.ppm[1];
                    document.getElementById('z-encoder-ppm').value = cfg.ppm[2];
                    document.getElementById('z-ppm-display').textContent = cfg.ppm[2];
                    this.setStatusLoaded('encoder');
                }
            })
            .catch(err => {
                console.error('[Settings] Encoder config load failed:', err);
                this.setStatusError('encoder', 'Failed to load encoder settings');
            });
    },

    saveMotionSettings() {
        console.log('[Settings] Saving motion settings');
        const x_low = parseInt(document.getElementById('x-limit-low').value);
        const x_high = parseInt(document.getElementById('x-limit-high').value);
        const y_low = parseInt(document.getElementById('y-limit-low').value);
        const y_high = parseInt(document.getElementById('y-limit-high').value);
        const z_low = parseInt(document.getElementById('z-limit-low').value);
        const z_high = parseInt(document.getElementById('z-limit-high').value);

        // Validate before saving
        if (x_low >= x_high || y_low >= y_high || z_low >= z_high) {
            this.showError('motion', 'Lower limit must be less than upper limit');
            return;
        }

        const updates = [
            this.setConfig(0, 'soft_limit_low_mm[0]', x_low),
            this.setConfig(0, 'soft_limit_high_mm[0]', x_high),
            this.setConfig(0, 'soft_limit_low_mm[1]', y_low),
            this.setConfig(0, 'soft_limit_high_mm[1]', y_high),
            this.setConfig(0, 'soft_limit_low_mm[2]', z_low),
            this.setConfig(0, 'soft_limit_high_mm[2]', z_high)
        ];

        Promise.all(updates)
            .then(() => {
                AlertManager.add('Motion settings saved successfully', 'success', 2000);
                this.setStatusLoaded('motion');
            })
            .catch(err => {
                console.error('[Settings] Motion save failed:', err);
                this.showError('motion', 'Failed to save motion settings');
                this.setStatusError('motion', 'Save failed');
            });
    },

    resetMotionSettings() {
        if (!confirm('Reset motion settings to defaults?')) return;
        const defaults = { low: 0, high: 500 };
        const updates = [
            this.setConfig(0, 'soft_limit_low_mm[0]', defaults.low),
            this.setConfig(0, 'soft_limit_high_mm[0]', defaults.high),
            this.setConfig(0, 'soft_limit_low_mm[1]', defaults.low),
            this.setConfig(0, 'soft_limit_high_mm[1]', defaults.high),
            this.setConfig(0, 'soft_limit_low_mm[2]', defaults.low),
            this.setConfig(0, 'soft_limit_high_mm[2]', defaults.high)
        ];

        Promise.all(updates)
            .then(() => this.loadMotionConfig())
            .then(() => AlertManager.add('Motion settings reset to defaults', 'success', 2000))
            .catch(err => {
                console.error('[Settings] Motion reset failed:', err);
                this.showError('motion', 'Failed to reset motion settings');
            });
    },

    saveVfdSettings() {
        console.log('[Settings] Saving VFD settings');
        const min_hz = parseInt(document.getElementById('vfd-min-speed').value);
        const max_hz = parseInt(document.getElementById('vfd-max-speed').value);
        const acc_ms = parseInt(document.getElementById('vfd-acc-time').value);
        const dec_ms = parseInt(document.getElementById('vfd-dec-time').value);

        // Validate
        if (min_hz >= max_hz) {
            this.showError('vfd', 'Min speed must be less than max speed');
            return;
        }
        if (min_hz < 1 || max_hz > 105) {
            this.showError('vfd', 'Speed must be between 1 and 105 Hz');
            return;
        }
        if (acc_ms < 200 || dec_ms < 200) {
            this.showError('vfd', 'Ramp time must be at least 200 ms');
            return;
        }

        const updates = [
            this.setConfig(1, 'min_speed_hz', min_hz),
            this.setConfig(1, 'max_speed_hz', max_hz),
            this.setConfig(1, 'acc_time_ms', acc_ms),
            this.setConfig(1, 'dec_time_ms', dec_ms)
        ];

        Promise.all(updates)
            .then(() => {
                AlertManager.add('VFD settings saved successfully', 'success', 2000);
                this.setStatusLoaded('vfd');
            })
            .catch(err => {
                console.error('[Settings] VFD save failed:', err);
                this.showError('vfd', 'Failed to save VFD settings');
                this.setStatusError('vfd', 'Save failed');
            });
    },

    resetVfdSettings() {
        if (!confirm('Reset VFD settings to defaults?')) return;
        const updates = [
            this.setConfig(1, 'min_speed_hz', 1),
            this.setConfig(1, 'max_speed_hz', 105),
            this.setConfig(1, 'acc_time_ms', 600),
            this.setConfig(1, 'dec_time_ms', 400)
        ];

        Promise.all(updates)
            .then(() => this.loadVfdConfig())
            .then(() => AlertManager.add('VFD settings reset to defaults', 'success', 2000))
            .catch(err => {
                console.error('[Settings] VFD reset failed:', err);
                this.showError('vfd', 'Failed to reset VFD settings');
            });
    },

    calibrateEncoder(axis) {
        const axisNames = ['X', 'Y', 'Z'];
        const ppmInput = document.getElementById(`${'xyz'[axis]}-encoder-ppm`);
        const ppm = parseInt(ppmInput.value);

        if (ppm < 50 || ppm > 200) {
            this.showError('encoder', `PPM must be between 50 and 200`);
            return;
        }

        console.log(`[Settings] Calibrating ${axisNames[axis]} encoder with ${ppm} PPM`);

        fetch('/api/encoder/calibrate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ axis, ppm })
        })
        .then(r => r.json())
        .then(data => {
            if (data.success) {
                AlertManager.add(`${axisNames[axis]}-axis calibrated to ${ppm} PPM`, 'success', 2000);
                this.setStatusLoaded('encoder');
            } else {
                this.showError('encoder', data.error || 'Calibration failed');
                this.setStatusError('encoder', 'Calibration failed');
            }
        })
        .catch(err => {
            console.error(`[Settings] Calibration failed:`, err);
            this.showError('encoder', 'Failed to calibrate encoder');
            this.setStatusError('encoder', 'Error');
        });
    },

    resetEncoderSettings() {
        if (!confirm('Reset all encoders to default (100 PPM)?')) return;
        const updates = [
            this.setConfig(2, 'ppm[0]', 100),
            this.setConfig(2, 'ppm[1]', 100),
            this.setConfig(2, 'ppm[2]', 100)
        ];

        Promise.all(updates)
            .then(() => this.loadEncoderConfig())
            .then(() => AlertManager.add('Encoder settings reset to defaults', 'success', 2000))
            .catch(err => {
                console.error('[Settings] Encoder reset failed:', err);
                this.showError('encoder', 'Failed to reset encoder settings');
            });
    },

    setConfig(category, key, value) {
        return fetch('/api/config/set', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ category, key, value })
        })
        .then(r => r.json())
        .then(data => {
            if (!data.success) {
                throw new Error(data.error || 'Set config failed');
            }
            return data;
        });
    },

    showError(section, message) {
        const errorDiv = document.getElementById(`${section}-error`);
        if (errorDiv) {
            errorDiv.textContent = message;
            errorDiv.style.display = 'block';
        }
        console.warn(`[Settings] ${section} error: ${message}`);
    },

    hideError(section) {
        const errorDiv = document.getElementById(`${section}-error`);
        if (errorDiv) {
            errorDiv.style.display = 'none';
        }
    },

    setStatusLoaded(section) {
        const status = document.getElementById(`${section}-status`);
        if (status) {
            status.textContent = '✓ Loaded';
            status.className = 'card-status loaded';
        }
    },

    setStatusError(section, message) {
        const status = document.getElementById(`${section}-status`);
        if (status) {
            status.textContent = '✗ ' + message;
            status.className = 'card-status error';
        }
    },

    updateMockModeUI() {
        const btn = document.getElementById('toggle-mock-mode-btn');
        const statusText = document.getElementById('mock-status-text');
        if (!btn || !statusText) return;

        if (window.MockMode?.enabled) {
            btn.textContent = 'Disable Mock Mode';
            btn.classList.add('active');
            statusText.textContent = '✓ Status: Mock Mode Active';
            statusText.style.color = 'var(--color-optimal)';
        } else {
            btn.textContent = 'Enable Mock Mode';
            btn.classList.remove('active');
            statusText.textContent = '✗ Status: Offline (Click to enable)';
            statusText.style.color = 'var(--text-secondary)';
        }
    },

    // Configuration Import/Export
    setupConfigManagement() {
        // Preset loader
        const loadPresetBtn = document.getElementById('load-preset-btn');
        if (loadPresetBtn) {
            loadPresetBtn.addEventListener('click', () => this.loadPreset());
        }

        // Export buttons
        const exportAllBtn = document.getElementById('export-all-btn');
        if (exportAllBtn) {
            exportAllBtn.addEventListener('click', () => this.exportConfiguration('all'));
        }

        const exportMotionBtn = document.getElementById('export-motion-btn');
        if (exportMotionBtn) {
            exportMotionBtn.addEventListener('click', () => this.exportConfiguration('motion'));
        }

        // Import button
        const importBtn = document.getElementById('import-config-btn');
        if (importBtn) {
            importBtn.addEventListener('click', () => this.importConfiguration());
        }

        // Compare button
        const compareBtn = document.getElementById('compare-config-btn');
        if (compareBtn) {
            compareBtn.addEventListener('click', () => this.compareConfiguration());
        }
    },

    getPresets() {
        return {
            aluminum: {
                motion: { x_min: -100, x_max: 500, y_min: -100, y_max: 500, z_min: 0, z_max: 100 },
                vfd: { min_speed: 20, max_speed: 85, acc_time: 600, dec_time: 400 },
                encoder: { x_ppm: 100, y_ppm: 100, z_ppm: 100, a_ppm: 50 }
            },
            steel: {
                motion: { x_min: -100, x_max: 500, y_min: -100, y_max: 500, z_min: 0, z_max: 100 },
                vfd: { min_speed: 10, max_speed: 50, acc_time: 800, dec_time: 600 },
                encoder: { x_ppm: 100, y_ppm: 100, z_ppm: 100, a_ppm: 50 }
            },
            plastic: {
                motion: { x_min: -100, x_max: 500, y_min: -100, y_max: 500, z_min: 0, z_max: 100 },
                vfd: { min_speed: 30, max_speed: 105, acc_time: 400, dec_time: 300 },
                encoder: { x_ppm: 100, y_ppm: 100, z_ppm: 100, a_ppm: 50 }
            },
            wood: {
                motion: { x_min: -100, x_max: 500, y_min: -100, y_max: 500, z_min: 0, z_max: 100 },
                vfd: { min_speed: 25, max_speed: 95, acc_time: 500, dec_time: 350 },
                encoder: { x_ppm: 100, y_ppm: 100, z_ppm: 100, a_ppm: 50 }
            },
            engraving: {
                motion: { x_min: -100, x_max: 500, y_min: -100, y_max: 500, z_min: 0, z_max: 100 },
                vfd: { min_speed: 40, max_speed: 105, acc_time: 300, dec_time: 200 },
                encoder: { x_ppm: 100, y_ppm: 100, z_ppm: 100, a_ppm: 50 }
            }
        };
    },

    loadPreset() {
        const select = document.getElementById('preset-select');
        if (!select || !select.value) {
            AlertManager.add('Please select a preset', 'warning', 2000);
            return;
        }

        const presets = this.getPresets();
        const preset = presets[select.value];

        if (preset) {
            // Apply preset values to form fields
            if (preset.motion) {
                document.getElementById('x-limit-low').value = preset.motion.x_min;
                document.getElementById('x-limit-high').value = preset.motion.x_max;
                document.getElementById('y-limit-low').value = preset.motion.y_min;
                document.getElementById('y-limit-high').value = preset.motion.y_max;
                document.getElementById('z-limit-low').value = preset.motion.z_min;
                document.getElementById('z-limit-high').value = preset.motion.z_max;
            }

            if (preset.vfd) {
                document.getElementById('vfd-min-speed').value = preset.vfd.min_speed;
                document.getElementById('vfd-max-speed').value = preset.vfd.max_speed;
                document.getElementById('vfd-acc-time').value = preset.vfd.acc_time;
                document.getElementById('vfd-dec-time').value = preset.vfd.dec_time;
                document.getElementById('vfd-min-display').textContent = preset.vfd.min_speed;
                document.getElementById('vfd-max-display').textContent = preset.vfd.max_speed;
                document.getElementById('vfd-acc-display').textContent = preset.vfd.acc_time;
                document.getElementById('vfd-dec-display').textContent = preset.vfd.dec_time;
            }

            AlertManager.add(`Preset '${select.value}' loaded. Review and save changes.`, 'success', 3000);
        }
    },

    exportConfiguration(type) {
        const config = {
            timestamp: new Date().toISOString(),
            firmware_version: '3.1.0',
            type: type,
            data: {}
        };

        if (type === 'all' || type === 'motion') {
            config.data.motion = {
                x_min: document.getElementById('x-limit-low').value || 0,
                x_max: document.getElementById('x-limit-high').value || 500,
                y_min: document.getElementById('y-limit-low').value || 0,
                y_max: document.getElementById('y-limit-high').value || 500,
                z_min: document.getElementById('z-limit-low').value || 0,
                z_max: document.getElementById('z-limit-high').value || 100
            };
        }

        if (type === 'all') {
            config.data.vfd = {
                min_speed: document.getElementById('vfd-min-speed').value || 1,
                max_speed: document.getElementById('vfd-max-speed').value || 105,
                acc_time: document.getElementById('vfd-acc-time').value || 600,
                dec_time: document.getElementById('vfd-dec-time').value || 400
            };

            config.data.encoder = {
                x_ppm: document.getElementById('x-encoder-ppm').value || 100,
                y_ppm: document.getElementById('y-encoder-ppm').value || 100,
                z_ppm: document.getElementById('z-encoder-ppm').value || 100,
                a_ppm: 50
            };
        }

        const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `config-${type}-${Date.now()}.json`;
        a.click();
        window.URL.revokeObjectURL(url);

        AlertManager.add(`Configuration exported as ${a.download}`, 'success', 3000);
    },

    importConfiguration() {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json';
        input.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (file) {
                const reader = new FileReader();
                reader.onload = (event) => {
                    try {
                        const config = JSON.parse(event.target.result);
                        this.applyConfiguration(config);
                    } catch (err) {
                        AlertManager.add('Invalid configuration file', 'critical', 3000);
                    }
                };
                reader.readAsText(file);
            }
        });
        input.click();
    },

    applyConfiguration(config) {
        if (!config || !config.data) {
            AlertManager.add('Invalid configuration format', 'critical', 3000);
            return;
        }

        if (config.data.motion) {
            document.getElementById('x-limit-low').value = config.data.motion.x_min;
            document.getElementById('x-limit-high').value = config.data.motion.x_max;
            document.getElementById('y-limit-low').value = config.data.motion.y_min;
            document.getElementById('y-limit-high').value = config.data.motion.y_max;
            document.getElementById('z-limit-low').value = config.data.motion.z_min;
            document.getElementById('z-limit-high').value = config.data.motion.z_max;
        }

        if (config.data.vfd) {
            document.getElementById('vfd-min-speed').value = config.data.vfd.min_speed;
            document.getElementById('vfd-max-speed').value = config.data.vfd.max_speed;
            document.getElementById('vfd-acc-time').value = config.data.vfd.acc_time;
            document.getElementById('vfd-dec-time').value = config.data.vfd.dec_time;
        }

        if (config.data.encoder) {
            document.getElementById('x-encoder-ppm').value = config.data.encoder.x_ppm;
            document.getElementById('y-encoder-ppm').value = config.data.encoder.y_ppm;
            document.getElementById('z-encoder-ppm').value = config.data.encoder.z_ppm;
        }

        AlertManager.add('Configuration loaded. Review changes and save.', 'success', 3000);

        const statusDiv = document.getElementById('import-status');
        if (statusDiv) {
            statusDiv.style.display = 'block';
            statusDiv.innerHTML = `✓ Loaded configuration from ${new Date().toLocaleString()}`;
        }
    },

    compareConfiguration() {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json';
        input.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (file) {
                const reader = new FileReader();
                reader.onload = (event) => {
                    try {
                        const fileConfig = JSON.parse(event.target.result);
                        const currentConfig = this.getCurrentConfiguration();
                        this.displayComparison(fileConfig, currentConfig);
                    } catch (err) {
                        AlertManager.add('Invalid file for comparison', 'critical', 3000);
                    }
                };
                reader.readAsText(file);
            }
        });
        input.click();
    },

    getCurrentConfiguration() {
        return {
            motion: {
                x_min: document.getElementById('x-limit-low').value || 0,
                x_max: document.getElementById('x-limit-high').value || 500,
                y_min: document.getElementById('y-limit-low').value || 0,
                y_max: document.getElementById('y-limit-high').value || 500
            },
            vfd: {
                min_speed: document.getElementById('vfd-min-speed').value || 1,
                max_speed: document.getElementById('vfd-max-speed').value || 105
            }
        };
    },

    displayComparison(fileConfig, currentConfig) {
        const resultDiv = document.getElementById('comparison-result');
        if (!resultDiv) return;

        let html = '<strong>Configuration Comparison:</strong><br>';

        if (fileConfig.data && fileConfig.data.motion && currentConfig.motion) {
            html += '<br><strong>Motion Settings:</strong><br>';
            Object.keys(fileConfig.data.motion).forEach(key => {
                const fileVal = fileConfig.data.motion[key];
                const currVal = currentConfig.motion[key];
                const match = fileVal === currVal;
                const color = match ? 'green' : 'orange';
                html += `<span style="color: ${color};">${key}: ${currVal} → ${fileVal}</span><br>`;
            });
        }

        resultDiv.innerHTML = html;
        resultDiv.style.display = 'block';
    },

    cleanup() {
        console.log('[Settings] Cleaning up');
    }
};

window.currentPageModule = SettingsModule;
