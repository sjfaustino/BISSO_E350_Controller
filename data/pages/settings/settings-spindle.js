/**
 * settings-spindle.js - Spindle alarm configuration module
 * Lazy-loaded module for spindle alarm settings
 */

// Extend SettingsModule with spindle alarm functions
Object.assign(window.SettingsModule, {
    loadSpindleAlarmConfig() {
        return fetch('/api/spindle/alarm')
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    const tb = document.getElementById('spindle-toolbreak-threshold');
                    const st = document.getElementById('spindle-stall-threshold');
                    const to = document.getElementById('spindle-stall-timeout');

                    if (tb) {
                        tb.value = data.toolbreak_threshold || 5;
                        document.getElementById('toolbreak-value').textContent = parseFloat(data.toolbreak_threshold || 5).toFixed(1);
                    }
                    if (st) {
                        st.value = data.stall_threshold || 25;
                        document.getElementById('stall-threshold-value').textContent = data.stall_threshold || 25;
                    }
                    if (to) {
                        to.value = data.stall_timeout_ms || 2000;
                        document.getElementById('stall-timeout-value').textContent = data.stall_timeout_ms || 2000;
                    }

                    this.updateSpindleAlarmStatus(data);
                    this.setStatusLoaded('spindle-alarm');
                }
            })
            .catch(err => this.setStatusError('spindle-alarm', window.i18n.t('settings.failed_load')));
    },

    updateSpindleAlarmStatus(data) {
        const tbEl = document.getElementById('alarm-toolbreak-status');
        const stEl = document.getElementById('alarm-stall-status');

        if (tbEl) {
            tbEl.textContent = data.alarm_tool_breakage ? window.i18n.t('settings.alarm_status') : window.i18n.t('settings.ok_status');
            tbEl.style.color = data.alarm_tool_breakage ? 'var(--color-critical)' : 'var(--color-optimal)';
        }
        if (stEl) {
            stEl.textContent = data.alarm_stall ? window.i18n.t('settings.alarm_status') : window.i18n.t('settings.ok_status');
            stEl.style.color = data.alarm_stall ? 'var(--color-critical)' : 'var(--color-optimal)';
        }
    },

    saveSpindleAlarmSettings() {
        const toolbreak = parseFloat(document.getElementById('spindle-toolbreak-threshold').value);
        const stallThresh = parseInt(document.getElementById('spindle-stall-threshold').value);
        const stallTimeout = parseInt(document.getElementById('spindle-stall-timeout').value);

        fetch('/api/spindle/alarm', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                toolbreak_threshold: toolbreak,
                stall_threshold: stallThresh,
                stall_timeout_ms: stallTimeout
            })
        })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    AlertManager.add(window.i18n.t('settings.spindle_saved'), 'success', 2000);
                    this.setStatusLoaded('spindle-alarm');
                } else {
                    this.showError('spindle-alarm', data.error || window.i18n.t('settings.save_failed'));
                }
            })
            .catch(err => this.showError('spindle-alarm', window.i18n.t('settings.save_failed')));
    },

    resetSpindleAlarmSettings() {
        if (!confirm(window.i18n.t('settings.reset_spindle_confirm'))) return;
        document.getElementById('spindle-toolbreak-threshold').value = 5;
        document.getElementById('toolbreak-value').textContent = '5.0';
        document.getElementById('spindle-stall-threshold').value = 25;
        document.getElementById('stall-threshold-value').textContent = '25';
        document.getElementById('spindle-stall-timeout').value = 2000;
        document.getElementById('stall-timeout-value').textContent = '2000';
        this.saveSpindleAlarmSettings();
    },

    clearSpindleAlarms() {
        fetch('/api/spindle/alarm/clear', { method: 'POST' })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    AlertManager.add(window.i18n.t('settings.alarms_cleared'), 'success', 2000);
                    const tbEl = document.getElementById('alarm-toolbreak-status');
                    const stEl = document.getElementById('alarm-stall-status');
                    if (tbEl) { tbEl.textContent = window.i18n.t('settings.ok_status'); tbEl.style.color = 'var(--color-optimal)'; }
                    if (stEl) { stEl.textContent = window.i18n.t('settings.ok_status'); stEl.style.color = 'var(--color-optimal)'; }
                } else {
                    AlertManager.add(window.i18n.t('settings.clear_failed') + ' ' + (data.error || 'Error'), 'error');
                }
            })
            .catch(err => AlertManager.add(window.i18n.t('settings.clear_failed'), 'error'));
    }
});

console.log('[Settings] Spindle alarm module loaded');
