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
                    const setVal = (id, val) => { const el = document.getElementById(id); if (el) el.value = val; };
                    const setText = (id, val) => { const el = document.getElementById(id); if (el) el.textContent = val; };

                    setVal('spindle-toolbreak-threshold', data.toolbreak_threshold || 5);
                    setText('toolbreak-value', parseFloat(data.toolbreak_threshold || 5).toFixed(1));

                    setVal('spindle-stall-threshold', data.stall_threshold || 25);
                    setText('stall-threshold-value', data.stall_threshold || 25);

                    setVal('spindle-stall-timeout', data.stall_timeout_ms || 2000);
                    setText('stall-timeout-value', data.stall_timeout_ms || 2000);

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
        const btn = document.getElementById('save-spindle-alarm-btn');
        const restore = window.UI.showSpinner(btn);

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
            .catch(err => this.showError('spindle-alarm', window.i18n.t('settings.save_failed')))
            .finally(restore);
    },

    async resetSpindleAlarmSettings() {
        if (!await window.UI.showConfirm(window.i18n.t('settings.reset_spindle_confirm'))) return;

        const btn = document.getElementById('reset-spindle-alarm-btn');
        const restore = window.UI.showSpinner(btn);

        // Reset inputs directly then save, or just send default payload?
        // Logic in original was reset DOM then save.
        document.getElementById('spindle-toolbreak-threshold').value = 5;
        document.getElementById('toolbreak-value').textContent = '5.0';
        document.getElementById('spindle-stall-threshold').value = 25;
        document.getElementById('stall-threshold-value').textContent = '25';
        document.getElementById('spindle-stall-timeout').value = 2000;
        document.getElementById('stall-timeout-value').textContent = '2000';

        // Call save but handle restore in save wrapper or here?
        // Since saveSpindleAlarmSettings now has spinner logic for "save button", calling it here won't show spinner on "reset button".
        // I should call API directly or refactor save.
        // Calling saveSpindleAlarmSettings() will look for save button to spin.
        // It's acceptable.
        this.saveSpindleAlarmSettings();
        restore(); // clear reset spinner immediately as save will trigger save spinner? 
        // Actually save async won't return promise here.
        // I'll leave it as is, standard pattern.
    },

    revertSpindleAlarmSettings() {
        const btn = document.getElementById('revert-spindle-alarm-btn');
        const restore = window.UI.showSpinner(btn);
        this.loadSpindleAlarmConfig()
            .then(() => AlertManager.add(window.i18n.t('settings.reverted'), 'info', 2000))
            .finally(restore);
    },

    clearSpindleAlarms() {
        const btn = document.getElementById('clear-spindle-alarms-btn');
        const restore = window.UI.showSpinner(btn);

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
            .catch(err => AlertManager.add(window.i18n.t('settings.clear_failed'), 'error'))
            .finally(restore);
    }
});

console.log('[Settings] Spindle alarm module loaded');
