/**
 * settings-motion.js - Motion, VFD, and Encoder configuration
 * Lazy-loaded module for Settings page hardware configuration
 */

// Extend SettingsModule with motion/VFD/encoder functions
Object.assign(window.SettingsModule, {
    // Motion configuration
    loadMotionConfig() {
        return fetch('/api/config/get?category=0')
            .then(r => r.json())
            .then(data => {
                console.log('[Settings] Motion data received:', data);
                if (data.success && data.config) {
                    const cfg = data.config;
                    console.log('[Settings] Applying motion config:', cfg);
                    document.getElementById('x-limit-low').value = cfg.soft_limit_x_low ?? 0;
                    document.getElementById('x-limit-high').value = cfg.soft_limit_x_high ?? 500;
                    document.getElementById('y-limit-low').value = cfg.soft_limit_y_low ?? 0;
                    document.getElementById('y-limit-high').value = cfg.soft_limit_y_high ?? 500;
                    document.getElementById('z-limit-low').value = cfg.soft_limit_z_low ?? 0;
                    document.getElementById('z-limit-high').value = cfg.soft_limit_z_high ?? 500;
                    this.setStatusLoaded('motion');
                }
            })
            .catch(err => {
                console.error('[Settings] Motion config load failed:', err);
                this.setStatusError('motion', 'Failed to load');
            });
    },

    saveMotionSettings() {
        const x_low = parseInt(document.getElementById('x-limit-low').value);
        const x_high = parseInt(document.getElementById('x-limit-high').value);
        const y_low = parseInt(document.getElementById('y-limit-low').value);
        const y_high = parseInt(document.getElementById('y-limit-high').value);
        const z_low = parseInt(document.getElementById('z-limit-low').value);
        const z_high = parseInt(document.getElementById('z-limit-high').value);

        if (x_low >= x_high || y_low >= y_high || z_low >= z_high) {
            this.showError('motion', 'Lower limit must be less than upper limit');
            return;
        }

        Promise.all([
            this.setConfig(0, 'soft_limit_x_low', x_low),
            this.setConfig(0, 'soft_limit_x_high', x_high),
            this.setConfig(0, 'soft_limit_y_low', y_low),
            this.setConfig(0, 'soft_limit_y_high', y_high),
            this.setConfig(0, 'soft_limit_z_low', z_low),
            this.setConfig(0, 'soft_limit_z_high', z_high)
        ])
            .then(() => {
                AlertManager.add('Motion settings saved', 'success', 2000);
                this.setStatusLoaded('motion');
            })
            .catch(err => {
                this.showError('motion', 'Failed to save');
                this.setStatusError('motion', 'Save failed');
            });
    },

    resetMotionSettings() {
        if (!confirm('Reset motion settings to defaults?')) return;
        Promise.all([
            this.setConfig(0, 'soft_limit_x_low', 0),
            this.setConfig(0, 'soft_limit_x_high', 500),
            this.setConfig(0, 'soft_limit_y_low', 0),
            this.setConfig(0, 'soft_limit_y_high', 500),
            this.setConfig(0, 'soft_limit_z_low', 0),
            this.setConfig(0, 'soft_limit_z_high', 500)
        ])
            .then(() => this.loadMotionConfig())
            .then(() => AlertManager.add('Motion settings reset', 'success', 2000))
            .catch(err => this.showError('motion', 'Reset failed'));
    },

    // VFD configuration
    loadVfdConfig() {
        return fetch('/api/config/get?category=1')
            .then(r => r.json())
            .then(data => {
                console.log('[Settings] VFD data received:', data);
                if (data.success && data.config) {
                    const cfg = data.config;
                    console.log('[Settings] Applying VFD config:', cfg);
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
                this.setStatusError('vfd', 'Failed to load');
            });
    },

    saveVfdSettings() {
        const min_hz = parseInt(document.getElementById('vfd-min-speed').value);
        const max_hz = parseInt(document.getElementById('vfd-max-speed').value);
        const acc_ms = parseInt(document.getElementById('vfd-acc-time').value);
        const dec_ms = parseInt(document.getElementById('vfd-dec-time').value);

        if (min_hz >= max_hz) { this.showError('vfd', 'Min must be less than max'); return; }
        if (min_hz < 1 || max_hz > 105) { this.showError('vfd', 'Speed: 1-105 Hz'); return; }
        if (acc_ms < 200 || dec_ms < 200) { this.showError('vfd', 'Ramp >= 200ms'); return; }

        Promise.all([
            this.setConfig(1, 'min_speed_hz', min_hz),
            this.setConfig(1, 'max_speed_hz', max_hz),
            this.setConfig(1, 'acc_time_ms', acc_ms),
            this.setConfig(1, 'dec_time_ms', dec_ms)
        ])
            .then(() => {
                AlertManager.add('VFD settings saved', 'success', 2000);
                this.setStatusLoaded('vfd');
            })
            .catch(err => this.showError('vfd', 'Save failed'));
    },

    resetVfdSettings() {
        if (!confirm('Reset VFD settings to defaults?')) return;
        Promise.all([
            this.setConfig(1, 'min_speed_hz', 1),
            this.setConfig(1, 'max_speed_hz', 105),
            this.setConfig(1, 'acc_time_ms', 600),
            this.setConfig(1, 'dec_time_ms', 400)
        ])
            .then(() => this.loadVfdConfig())
            .then(() => AlertManager.add('VFD settings reset', 'success', 2000))
            .catch(err => this.showError('vfd', 'Reset failed'));
    },

    // Encoder configuration
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
            .catch(err => this.setStatusError('encoder', 'Failed to load'));
    },

    calibrateEncoder(axis) {
        const axisNames = ['X', 'Y', 'Z'];
        const ppm = parseInt(document.getElementById(`${'xyz'[axis]}-encoder-ppm`).value);
        if (ppm < 50 || ppm > 200) { this.showError('encoder', 'PPM: 50-200'); return; }

        fetch('/api/encoder/calibrate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ axis, ppm })
        })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    AlertManager.add(`${axisNames[axis]} calibrated to ${ppm} PPM`, 'success', 2000);
                    this.setStatusLoaded('encoder');
                } else {
                    this.showError('encoder', data.error || 'Calibration failed');
                }
            })
            .catch(err => this.showError('encoder', 'Calibration failed'));
    },

    resetEncoderSettings() {
        if (!confirm('Reset all encoders to 100 PPM?')) return;
        Promise.all([
            this.setConfig(2, 'ppm_x', 100),
            this.setConfig(2, 'ppm_y', 100),
            this.setConfig(2, 'ppm_z', 100)
        ])
            .then(() => this.loadEncoderConfig())
            .then(() => AlertManager.add('Encoders reset', 'success', 2000))
            .catch(err => this.showError('encoder', 'Reset failed'));
    },

    setConfig(category, key, value) {
        return fetch('/api/config/set', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ category, key, value })
        })
            .then(r => r.json())
            .then(data => { if (!data.success) throw new Error(data.error); return data; });
    }
});

console.log('[Settings] Motion/VFD/Encoder module loaded');
