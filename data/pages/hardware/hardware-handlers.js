/**
 * hardware-handlers.js - Event Handlers for Hardware Page
 * Extracted from hardware.js to improve maintainability.
 */

window.HardwareHandlers = {
    detectBaud(btnId) {
        const btn = document.getElementById(btnId);
        if (!btn) return;

        const restore = window.UI.showSpinner(btn, window.i18n.t('hardware.detecting'));

        fetch("/api/hardware/wj66/detect", { method: "POST" })
            .then(() => {
                window.Toast?.info(window.i18n.t('hardware.detect_started'));
                setTimeout(() => location.reload(), 8000);
            })
            .catch(() => {
                window.Toast?.error(window.i18n.t('hardware.detect_failed'));
                restore();
            });
    },

    detectRS485(btnId) {
        const btn = document.getElementById(btnId);
        if (!btn) return;

        const vfdEn = document.getElementById("vfd_enabled")?.checked;
        const jxk10En = document.getElementById("jxk10_enabled")?.checked;

        if (!vfdEn && !jxk10En) {
            window.Toast?.error(window.i18n.t('hardware.enable_rs485_first'));
            return;
        }

        const restore = window.UI.showSpinner(btn, window.i18n.t('hardware.searching'));
        window.Toast?.info(window.i18n.t('hardware.scanning'));

        fetch("/api/config/detect-rs485", { method: "POST" })
            .then(res => res.json())
            .then(data => {
                if (data.success) {
                    window.Toast?.success(window.i18n.t('hardware.found_modbus').replace('{baud}', data.baud));
                    const input = document.getElementById("rs485_baud");
                    if (input) input.value = data.baud;
                } else {
                    window.Toast?.error(data.error || window.i18n.t('hardware.detect_failed'));
                }
            })
            .catch(() => window.Toast?.error(window.i18n.t('hardware.comm_error')))
            .finally(restore);
    },

    testI2C(btnId) {
        const btn = document.getElementById(btnId);
        if (!btn) return;

        const restore = window.UI.showSpinner(btn, window.i18n.t('hardware.i2c_testing'));

        fetch("/api/hardware/i2c/test", { method: "POST" })
            .then(res => res.json())
            .then(data => {
                if (data.success) {
                    const msg = window.i18n.t('hardware.i2c_test_success').replace('{count}', data.count);
                    const names = data.devices ? data.devices.map(d => d.name).join(", ") : "";
                    window.Toast?.success(msg + (names ? ": " + names : ""));
                } else {
                    window.Toast?.error(window.i18n.t('hardware.i2c_test_failed'));
                }
            })
            .catch(() => window.Toast?.error(window.i18n.t('hardware.i2c_test_failed')))
            .finally(restore);
    },

    async saveConfiguration(module) {
        console.log("[Hardware] Saving configuration...");
        const saveBtn = document.getElementById("save-config-btn");
        const restore = saveBtn ? window.UI.showSpinner(saveBtn, window.i18n.t('hardware.saving')) : () => { };

        // Validation
        const vfdEn = document.getElementById("vfd_enabled")?.checked;
        const jxk10En = document.getElementById("jxk10_enabled")?.checked;
        const vfdAddr = parseInt(document.getElementById("vfd_addr")?.value || 0);
        const jxk10Addr = parseInt(document.getElementById("jxk10_addr")?.value || 0);

        if (vfdEn && jxk10En && vfdAddr === jxk10Addr) {
            module.showStatus(window.i18n.t('hardware.addr_error'), "error");
            restore();
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

        // Save Pins
        try {
            const pinRes = await fetch("/api/hardware/pins", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(pinConfig)
            });
            const pinData = await pinRes.json();

            if (!pinData.success && !pinData.ok) throw new Error(pinData.error || "Pin save failed");

            // Save Config Values
            const configRes = await fetch("/api/config/batch", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(configValues)
            });
            const configData = await configRes.json();

            if (!configData.success && !configData.ok) console.error("[Hardware] Batch save error:", configData.error);

            module.showStatus(window.i18n.t('hardware.reboot_notice'), "success");

            // Ask for reboot
            const shouldReboot = await window.UI.showConfirm("Configuration saved. A reboot is required to apply changes. Reboot now?");
            if (shouldReboot) {
                fetch("/api/system/reboot", { method: "POST" });
            }

        } catch (e) {
            console.error("[Hardware] Save error:", e);
            module.showStatus(window.i18n.t('network.error_prefix') + e.message, "error");
        } finally {
            restore();
        }
    },

    async resetConfiguration(module) {
        const confirmed = await window.UI.showConfirm(window.i18n.t('hardware.reset_confirm'));
        if (!confirmed) return;

        module.showStatus(window.i18n.t('hardware.resetting'), "info");
        const restore = window.UI.showSpinner("reset-defaults-btn");

        try {
            const res = await fetch("/api/hardware/pins/reset", { method: "POST" });
            const data = await res.json();

            if (!data.success) throw new Error(data.error || "Reset failed");

            module.showStatus(window.i18n.t('hardware.reset_success'), "success");
            setTimeout(() => location.reload(), 3000);
        } catch (e) {
            console.error("[Hardware] Reset failed:", e);
            module.showStatus("Reset failed: " + e.message, "error");
            restore();
        }
    }
};
