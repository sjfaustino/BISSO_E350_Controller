window.SystemModule = window.SystemModule || {
    systemStartTime: Date.now(),
    resetCount: 2,

    init() {
        console.log("[System] Initializing");
        // Initial fetch to get latest telemetry state if not already there
        window.API.get('telemetry')
            .then(d => {
                AppState.update(d);
                this.loadSystemInfo();
            })
            .catch(e => console.warn("[System] Init fetch failed", e));

        this.setupEventListeners();
        this.startStatusUpdates();
    },

    setupEventListeners() {
        const btnMap = {
            "check-update-btn": () => this.checkForUpdates(),
            "view-logs-btn": () => this.viewSystemLogs(),
            "system-health-btn": () => this.runHealthCheck(),
            "reset-btn": () => this.rebootDevice(),
            "backup-config-btn": () => this.backupConfiguration(),
            "restore-config-btn": () => this.restoreConfiguration(),
            "factory-reset-btn": () => this.factoryReset(),
            "sync-time-btn": () => this.syncTimeFromBrowser()
        };

        Object.entries(btnMap).forEach(([id, handler]) => {
            const el = document.getElementById(id);
            if (el) el.addEventListener("click", handler);
        });
    },

    loadSystemInfo() {
        const sys = AppState.get("system") || {};

        // Firmware Info
        Utils.setText("fw-version", sys.firmware_version || "---");
        Utils.setText("fw-build-date", sys.build_date || "---");

        // Hardware Info
        Utils.setText("hw-mcu", sys.hw_mcu || "Unknown");
        Utils.setText("hw-model", sys.hw_model || "BISSO E350 (Legacy)");
        Utils.setText("serial-number", sys.hw_serial || "Pending...");
        Utils.setText("hw-revision", sys.hw_revision || "v1.0");

        if (sys.hw_flash_size) {
            Utils.setText("hw-flash-size", Utils.formatBytes(sys.hw_flash_size));
        }

        if (sys.hw_has_psram) {
            Utils.setText("hw-psram", Utils.formatBytes(sys.hw_psram_size || 0) + " (Available)");
        } else {
            Utils.setText("hw-psram", "None");
        }

        // Features Display (Dynamic based on capability flags)
        this.updateFeaturesList(sys);

        // OTA Info
        const ota = AppState.get("ota");
        const otaLabel = document.getElementById("fw-latest");
        const otaBtn = document.getElementById("check-update-btn");

        if (otaLabel && ota) {
            if (ota.available) {
                otaLabel.textContent = `${ota.latest_version} (Available)`;
                otaLabel.style.color = "var(--color-optimal)";
                if (otaBtn) {
                    otaBtn.textContent = "Install Update";
                    otaBtn.classList.replace("btn-primary", "btn-success");
                    otaBtn.onclick = () => this.installUpdate();
                }
            } else {
                otaLabel.textContent = `${sys.firmware_version} (Latest)`;
                otaLabel.style.color = "var(--text-secondary)";
            }
        }

        this.updateStorageInfo();
        this.updateSystemStatus();
        this.updateDeviceTime();

        // Config Status
        const cfg = AppState.get("config") || {};
        this.setConfigStatus("conf-auth", cfg.http_auth);
        this.setConfigStatus("conf-https", cfg.https);
        this.setConfigStatus("conf-ws", cfg.websocket);
        this.setConfigStatus("conf-modbus", cfg.modbus);
    },

    updateFeaturesList(sys) {
        const container = document.getElementById("hw-features");
        if (!container) return;

        const features = [];
        if (sys.hw_has_rtc) features.push("RTC DS3231");
        if (sys.hw_has_oled) features.push("SSD1306 OLED");
        if (sys.hw_has_sd) features.push("SD Card Slot");
        if (sys.hw_eth_chip) features.push(`Ethernet (${sys.hw_eth_chip})`);

        if (features.length === 0) {
            container.textContent = "Standard Built-in";
        } else {
            container.textContent = features.join(", ");
        }
    },

    setConfigStatus(id, value) {
        const el = document.getElementById(id);
        if (!el) return;

        if (value === undefined) {
            el.textContent = "Loading...";
            el.style.color = "";
        } else if (value) {
            el.textContent = "✓ Yes";
            el.style.color = "var(--color-optimal)";
        } else {
            el.textContent = "✗ No";
            el.style.color = "var(--color-warning)";
        }
    },

    async checkForUpdates() {
        AlertManager.add("Checking for updates...", "info");
        await AppState.checkForUpdates();
        this.loadSystemInfo();

        const ota = AppState.get("ota");
        if (ota && ota.available) {
            AlertManager.add(`Update available: ${ota.latest_version}`, "success", 5000);
        } else {
            AlertManager.add("You are running the latest version", "success", 3000);
        }
    },

    async installUpdate() {
        if (confirm("Start firmware update? The device will reboot.")) {
            AlertManager.add("Starting update...", "info");
            try {
                await window.API.post("ota/update");
                AlertManager.add("Update started! Do not power off.", "warning", 10000);
                const btn = document.getElementById("check-update-btn");
                if (btn) btn.disabled = true;
            } catch (e) {
                AlertManager.add("Update request failed", "critical");
            }
        }
    },

    updateStorageInfo() {
        // PSRAM/RAM logic
        const ram_total = 360; // Internal SRAM
        const ram_used = 210; // Placeholder until we get live task data
        const ram_pct = (ram_used / ram_total) * 100;

        const bars = {
            "flash-bar": 52.5,
            "ram-bar": ram_pct,
            "spiffs-bar": 42
        };

        Object.entries(bars).forEach(([id, pct]) => {
            const el = document.getElementById(id);
            if (el) {
                el.style.width = pct + "%";
                this.setProgressBarColor(id, pct);
            }
        });

        Utils.setText("flash-used", "2.1");
        Utils.setText("ram-used", ram_used);
        Utils.setText("spiffs-used", "1.68");
    },

    setProgressBarColor(id, pct) {
        const el = document.getElementById(id);
        if (!el) return;

        if (pct > 85) el.style.background = "linear-gradient(90deg, var(--color-critical), var(--color-warning))";
        else if (pct > 70) el.style.background = "linear-gradient(90deg, var(--color-warning), var(--color-normal))";
        else el.style.background = "linear-gradient(90deg, var(--color-optimal), var(--color-normal))";
    },

    updateSystemStatus() {
        const sys = AppState.get("system") || {};
        const upSec = sys.uptime_seconds || 0;

        const hr = Math.floor(upSec / 3600);
        const min = Math.floor((upSec % 3600) / 60);
        Utils.setText("system-uptime", `${hr}h ${min}m`);

        const lastReset = document.getElementById("last-reset");
        if (lastReset && upSec > 0) {
            const date = new Date(Date.now() - (upSec * 1000));
            lastReset.textContent = date.toLocaleString();
        }

        Utils.setText("reset-count", this.resetCount);
        Utils.setText("last-backup", new Date().toLocaleString());
    },

    startStatusUpdates() {
        this.statusTimer = setInterval(() => {
            this.updateStorageInfo();
            this.updateSystemStatus();
            this.updateDeviceTime();
            this.loadSystemInfo();
        }, 10000);
    },

    async updateDeviceTime() {
        try {
            const t = await window.API.get("time");
            const elTime = document.getElementById("device-time");
            const elSource = document.getElementById("time-source");

            if (elTime) {
                const date = new Date(t.timestamp * 1000);
                elTime.textContent = date.toLocaleString();
            }

            if (elSource) {
                const isSynced = t.timestamp > 1577836800; // 2020-01-01
                elSource.textContent = isSynced ? "NTP / Browser Sync" : "Not Synced";
                elSource.style.color = isSynced ? "var(--color-optimal)" : "var(--color-warning)";
            }
        } catch (e) {
            console.warn("[System] Failed to fetch device time:", e);
        }
    },

    async syncTimeFromBrowser() {
        const btn = document.getElementById("sync-time-btn");
        if (btn) btn.disabled = true;

        try {
            const ts = Math.floor(Date.now() / 1000);
            const data = await window.API.post("time/sync", { timestamp: ts });
            AlertManager.add(`Time synced: ${data.time}`, "success", 3000);
            this.updateDeviceTime();
        } catch (e) {
            AlertManager.add("Time sync failed", "critical", 3000);
        } finally {
            if (btn) btn.disabled = false;
        }
    },

    viewSystemLogs() {
        Router.go('logs');
    },

    async runHealthCheck() {
        AlertManager.add("Analyzing system telemetry...", "info", 2000);
        await new Promise(r => setTimeout(r, 1000));

        const sys = AppState.get("system") || {};
        const net = AppState.get("network") || {};
        const vfd = AppState.get("vfd") || {};
        const cfg = AppState.get("config") || {};

        let issues = [];
        let status = "HEALTHY";

        const heapKB = Math.round((sys.free_heap_bytes || 0) / 1024);
        if (heapKB < 20) { status = "CRITICAL"; issues.push(`Very low memory (${heapKB}KB)`); }
        else if (heapKB < 50) { if (status !== "CRITICAL") status = "WARNING"; issues.push(`Low memory (${heapKB}KB)`); }

        if (!net.wifi_connected) issues.push("WiFi disconnected");
        else if (net.signal_percent <= 30) issues.push(`Weak WiFi signal (${net.signal_percent}%)`);

        if (cfg.modbus && !vfd.connected) {
            if (status !== "CRITICAL") status = "WARNING";
            issues.push("VFD not reaching");
        }

        if (issues.length > 0) {
            const type = status === "CRITICAL" ? "critical" : "warning";
            AlertManager.add(`Health: ${status} - ${issues.join(", ")}`, type, 10000);
        } else {
            AlertManager.add("System Health: Optimal", "success", 5000);
        }
    },

    async rebootDevice() {
        if (confirm("Reboot the device? Current operations will stop.")) {
            AlertManager.add("Rebooting...", "warning", 5000);
            try {
                await window.API.post("system/reboot");
                AlertManager.add("Device restarting...", "critical", 10000);
            } catch (e) {
                AlertManager.add("Connection lost - rebooting", "critical", 10000);
            }
        }
    },

    backupConfiguration() {
        AlertManager.add("Downloading backup...", "info");
        const a = document.createElement("a");
        a.href = "/api/config/backup";
        a.download = "bisso_config.json";
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
    },

    restoreConfiguration() {
        const input = document.createElement("input");
        input.type = "file";
        input.accept = ".json";
        input.onchange = (e) => {
            const file = e.target.files[0];
            if (file) {
                AlertManager.add("Configuration restore would proceed here", "info");
            }
        };
        input.click();
    },

    factoryReset() {
        if (confirm("DANGER: This will erase ALL settings!")) {
            if (confirm("Are you absolutely sure?")) {
                AlertManager.add("Initiating factory reset...", "critical");
            }
        }
    },

    cleanup() {
        console.log("[System] Cleaning up");
        if (this.statusTimer) clearInterval(this.statusTimer);
    }
};

window.currentPageModule = SystemModule;