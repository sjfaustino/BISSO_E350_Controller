/**
 * Settings Page Module
 * Manages user preferences, alerts, and system configuration
 */
const SettingsModule = {
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
        this.updateDisplay();
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

    cleanup() {
        console.log('[Settings] Cleaning up');
    }
};

window.currentPageModule = SettingsModule;
