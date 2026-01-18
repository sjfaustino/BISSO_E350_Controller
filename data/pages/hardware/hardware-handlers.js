/**
 * hardware-handlers.js - Event Handlers for Hardware Page
 * Extracted from hardware.js to improve maintainability.
 */

window.HardwareHandlers = {
    detectBaud(btnId) {
        window.API.post("hardware/wj66/detect", {}, btnId)
            .then(() => {
                window.Toast?.info(window.i18n.t('hardware.detect_started'));
                setTimeout(() => location.reload(), 8000);
            })
            .catch(() => { /* API handles error toast */ });
    },

    detectRS485(btnId) {
        const vfdEn = document.getElementById("vfd_enabled")?.checked;
        const jxk10En = document.getElementById("jxk10_enabled")?.checked;

        if (!vfdEn && !jxk10En) {
            window.Toast?.error(window.i18n.t('hardware.enable_rs485_first'));
            return;
        }

        window.Toast?.info(window.i18n.t('hardware.scanning'));

        window.API.post("config/detect-rs485", {}, btnId)
            .then(data => {
                if (data.success) {
                    window.Toast?.success(window.i18n.t('hardware.found_modbus').replace('{baud}', data.baud));
                    const input = document.getElementById("rs485_baud");
                    if (input) input.value = data.baud;
                } else {
                    window.Toast?.error(data.error || window.i18n.t('hardware.detect_failed'));
                }
            })
            .catch(() => window.Toast?.error(window.i18n.t('hardware.comm_error')));
    },

    testI2C(btnId) {
        window.API.post("hardware/i2c/test", {}, btnId)
            .then(data => {
                if (data.success) {
                    const msg = window.i18n.t('hardware.i2c_test_success').replace('{count}', data.count);
                    const names = data.devices ? data.devices.map(d => d.name).join(", ") : "";
                    window.Toast?.success(msg + (names ? ": " + names : ""));
                } else {
                    window.Toast?.error(window.i18n.t('hardware.i2c_test_failed'));
                }
            })
            .catch(() => { });
    },

    async saveConfiguration(module) {
        console.log("[Hardware] Saving configuration...");
        // Validation
        const vfdEn = document.getElementById("vfd_enabled")?.checked;
        const jxk10En = document.getElementById("jxk10_enabled")?.checked;
        const vfdAddr = parseInt(document.getElementById("vfd_addr")?.value || 0);
        const jxk10Addr = parseInt(document.getElementById("jxk10_addr")?.value || 0);

        if (vfdEn && jxk10En && vfdAddr === jxk10Addr) {
            module.showStatus(window.i18n.t('hardware.addr_error'), "error");
            return;
        }

        // Pin Config Collection
        const pinConfig = {};
        document.querySelectorAll(".pin-select").forEach(el => {
            if (el.dataset.signal && el.value && !el.disabled) {
                pinConfig[el.dataset.signal] = parseInt(el.value);
            }
        });

        // Value Config Collection
        const configValues = {};
        document.querySelectorAll("[data-config]").forEach(el => {
            if (el.type === "checkbox") configValues[el.dataset.config] = el.checked ? 1 : 0;
            else if (el.type === "number") configValues[el.dataset.config] = parseInt(el.value) || 0;
            else configValues[el.dataset.config] = el.value;
        });
        document.querySelectorAll("select[data-config]").forEach(el => {
            configValues[el.dataset.config] = el.value;
        });

        // Save Pins & Config
        try {
            // Using save-config-btn as spinner target for the chain
            const pinData = await window.API.post("hardware/pins", pinConfig, "save-config-btn");
            if (!pinData.success && !pinData.ok) throw new Error(pinData.error || "Pin save failed");

            // Config Values (silent=true to avoid double error if pin save succeeded but this fails? No, normal error is fine)
            // But we don't want to spin twice/stop twice awkwardly.
            // Actually API.post starts/stops spinner. Since we await, it will start/stop/start/stop. That's fine.
            const configData = await window.API.post("config/batch", configValues, "save-config-btn");

            if (!configData.success && !configData.ok) console.error("[Hardware] Batch save error:", configData.error);

            module.showStatus(window.i18n.t('hardware.reboot_notice'), "success");

            // Ask for reboot
            const shouldReboot = await window.UI.showConfirm("Configuration saved. A reboot is required to apply changes. Reboot now?");
            if (shouldReboot) {
                window.API.post("system/reboot");
            }

        } catch (e) {
            console.error("[Hardware] Save error:", e);
            module.showStatus(window.i18n.t('network.error_prefix') + e.message, "error");
        }
    },

    async resetConfiguration(module) {
        const confirmed = await window.UI.showConfirm(window.i18n.t('hardware.reset_confirm'));
        if (!confirmed) return;

        module.showStatus(window.i18n.t('hardware.resetting'), "info");

        try {
            const data = await window.API.post("hardware/pins/reset", {}, "reset-defaults-btn");
            if (!data.success) throw new Error(data.error || "Reset failed");

            module.showStatus(window.i18n.t('hardware.reset_success'), "success");
            setTimeout(() => location.reload(), 3000);
        } catch (e) {
            console.error("[Hardware] Reset failed:", e);
            module.showStatus("Reset failed: " + e.message, "error");
        }
    }
};
