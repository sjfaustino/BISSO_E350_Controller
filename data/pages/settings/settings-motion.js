/**
 * settings-motion.js - Motion, VFD, and Encoder configuration
 * Lazy-loaded module for Settings page hardware configuration
 */

// Extend SettingsModule with motion/VFD/encoder functions
Object.assign(window.SettingsModule, {
    // Motion configuration
    loadMotionConfig() {
        return window.API.get('config/get?category=0')
            .then(data => {
                console.log('[Settings] Motion data received:', data);
                if (data.success && data.config) {
                    const cfg = data.config;
                    const setVal = (id, val, def) => {
                        const el = document.getElementById(id);
                        if (el) el.value = val ?? def;
                    };

                    setVal('x-limit-low', cfg.soft_limit_x_low, 0);
                    setVal('x-limit-high', cfg.soft_limit_x_high, 500);
                    setVal('y-limit-low', cfg.soft_limit_y_low, 0);
                    setVal('y-limit-high', cfg.soft_limit_y_high, 500);
                    setVal('z-limit-low', cfg.soft_limit_z_low, 0);
                    setVal('z-limit-high', cfg.soft_limit_z_high, 500);

                    setVal('x-approach-slow', cfg.x_appr_slow, 5);
                    setVal('x-approach-med', cfg.x_appr_med, 20);
                    setVal('target-margin', cfg.tgt_margin, 0.1);

                    this.setStatusLoaded('motion');
                }
            })
            .catch(err => {
                console.error('[Settings] Motion config load failed:', err);
                this.setStatusError('motion', window.i18n.t('settings.failed_load'));
            });
    },

    saveMotionSettings() {
        const btnId = 'save-motion-btn';

        const getVal = (id) => parseFloat(document.getElementById(id).value);
        const x_low = getVal('x-limit-low'), x_high = getVal('x-limit-high');
        const y_low = getVal('y-limit-low'), y_high = getVal('y-limit-high');
        const z_low = getVal('z-limit-low'), z_high = getVal('z-limit-high');

        if (x_low >= x_high || y_low >= y_high || z_low >= z_high) {
            this.showError('motion', window.i18n.t('settings.limit_order_error'));
            return;
        }

        // Use API.post for batch or individual set calls?
        // Existing implementation calls setConfig in parallel.
        // I'll keep the parallel structure but pass btnId to LAST or FIRST request?
        // Or just let them run. Since Promise.all waits, I can show spinner via API manually?
        // API client: request(..., ..., spinnerTarget).
        // If multiple requests share the same spinnerTarget, it might start/stop rapidly or stack.
        // My ApiClient implementation: `if (spinnerTarget) window.UI.showSpinner(...)`. `stopSpinner` calls `restore`.
        // If called multiple times, `showSpinner` stores original text.
        // Calling it twice on same button might overwrite original text with "Loading..." if not careful.
        // `UI.showSpinner` implementation (assumed) usually handles active state.
        // But to be safe, I'll pass spinner to ONE request or handle it manually.
        // Actual `saveMotionSettings` implementation (previous) used manual spinner.
        // I will use manual spinner here since we do complex Promise.all.

        const btn = document.getElementById(btnId);
        const restore = window.UI.showSpinner(btn, window.i18n.t('settings.saving'));

        Promise.all([
            this.setConfig(0, 'soft_limit_x_low', x_low),
            this.setConfig(0, 'soft_limit_x_high', x_high),
            this.setConfig(0, 'soft_limit_y_low', y_low),
            this.setConfig(0, 'soft_limit_y_high', y_high),
            this.setConfig(0, 'soft_limit_z_low', z_low),
            this.setConfig(0, 'soft_limit_z_high', z_high),
            this.setConfig(0, 'x_appr_slow', getVal('x-approach-slow')),
            this.setConfig(0, 'x_appr_med', getVal('x-approach-med')),
            this.setConfig(0, 'tgt_margin', getVal('target-margin'))
        ])
            .then(() => {
                AlertManager.add(window.i18n.t('settings.motion_saved'), 'success', 2000);
                this.setStatusLoaded('motion');
            })
            .catch(() => {
                this.showError('motion', window.i18n.t('settings.save_failed'));
                this.setStatusError('motion', window.i18n.t('settings.save_failed'));
            })
            .finally(restore);
    },

    // VFD configuration
    loadVfdConfig() {
        return window.API.get('config/get?category=1')
            .then(data => {
                if (data.success && data.config) {
                    const cfg = data.config;
                    const setVal = (id, val) => {
                        const el = document.getElementById(id);
                        if (el) el.value = val;
                    };
                    const setText = (id, val) => {
                        const el = document.getElementById(id);
                        if (el) el.textContent = val;
                    };

                    setVal('vfd-min-speed', cfg.min_speed_hz); setText('vfd-min-display', cfg.min_speed_hz);
                    setVal('vfd-max-speed', cfg.max_speed_hz); setText('vfd-max-display', cfg.max_speed_hz);
                    setVal('vfd-acc-time', cfg.acc_time_ms); setText('vfd-acc-display', cfg.acc_time_ms);
                    setVal('vfd-dec-time', cfg.dec_time_ms); setText('vfd-dec-display', cfg.dec_time_ms);
                    this.setStatusLoaded('vfd');
                }
            })
            .catch(() => this.setStatusError('vfd', window.i18n.t('settings.failed_load')));
    },

    // Encoder configuration
    loadEncoderConfig() {
        return window.API.get('config/get?category=2')
            .then(data => {
                if (data.success && data.config) {
                    const cfg = data.config;
                    const setVal = (id, val) => { const el = document.querySelector(id); if (el) { el.value = val; } };
                    const setText = (id, val) => { const el = document.getElementById(id); if (el) el.textContent = val; };

                    document.getElementById('x-encoder-ppm').value = cfg.ppm[0];
                    setText('x-ppm-display', cfg.ppm[0]);
                    document.getElementById('y-encoder-ppm').value = cfg.ppm[1];
                    setText('y-ppm-display', cfg.ppm[1]);
                    document.getElementById('z-encoder-ppm').value = cfg.ppm[2];
                    setText('z-ppm-display', cfg.ppm[2]);
                    this.setStatusLoaded('encoder');
                }
            })
            .catch(() => this.setStatusError('encoder', window.i18n.t('settings.failed_load')));
    },

    calibrateEncoder(axis) {
        const axisChar = ['x', 'y', 'z'][axis];
        const btnId = `calibrate-${axisChar}-btn`;

        const ppm = parseInt(document.getElementById(`${axisChar}-encoder-ppm`).value);
        if (ppm < 50 || ppm > 200) {
            this.showError('encoder', window.i18n.t('settings.ppm_range'));
            return;
        }

        window.API.post('encoder/calibrate', { axis, ppm }, btnId)
            .then(data => {
                if (data.success) {
                    AlertManager.add(`${axisChar.toUpperCase()} ${window.i18n.t('settings.calibrated_msg')} ${ppm} PPM`, 'success', 2000);
                    this.setStatusLoaded('encoder');
                } else {
                    this.showError('encoder', data.error || window.i18n.t('settings.calibration_failed'));
                }
            })
            .catch(() => this.showError('encoder', window.i18n.t('settings.calibration_failed')));
    },

    setConfig(category, key, value) {
        // Use API.post but silent because caller handles errors?
        // Or let API show errors?
        // Caller `saveMotionSettings` has catch block that shows generic "Save Failed".
        // If API shows specific error, that's better.
        // But `Promise.all` in caller will fail if ANY fail.
        // If API throws, `Promise.all` catches.
        return window.API.post('config/set', { category, key, value }, null);
    }


});

console.log('[Settings] Motion/VFD/Encoder module loaded');
