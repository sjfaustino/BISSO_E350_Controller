/**
 * Network Module
 * Handles WiFi and Ethernet configuration, status monitoring, and diagnostics.
 */
window.NetworkModule = {
    latencyHistory: [],
    maxHistoryLength: 100,
    reconnectCount: 0,
    connectionStartTime: Date.now(),
    stats: { packetsSent: 0, packetsReceived: 0, errors: 0, dataReceived: 0 },

    init() {
        console.log("[Network] Initializing");
        this.loadWiFiConfig();
        this.updateNetworkStatus();
        this.setupEventListeners();
        this.startLatencyMonitoring();
        window.addEventListener("state-changed", () => this.onStateChanged());
    },

    setupEventListeners() {
        const apToggle = document.getElementById("ap-enabled");
        if (apToggle) {
            apToggle.addEventListener("change", (e) => {
                const fields = document.getElementById("ap-settings-fields");
                if (fields) fields.style.display = e.target.checked ? "block" : "none";
            });
        }

        document.getElementById("save-eth-btn")?.addEventListener("click", () => this.saveEthConfig());
        document.getElementById("save-station-btn")?.addEventListener("click", () => this.saveStationConfig());
        document.getElementById("save-ap-btn")?.addEventListener("click", () => this.saveAPConfig());
        document.getElementById("ping-btn")?.addEventListener("click", () => this.sendPing());
        document.getElementById("reconnect-btn")?.addEventListener("click", () => this.reconnectDevice());

        const toggleStation = document.getElementById("toggle-station-pass");
        if (toggleStation) {
            toggleStation.addEventListener("click", (e) => {
                e.preventDefault();
                this.togglePass("station-pass");
            });
        }

        const toggleAp = document.getElementById("toggle-ap-pass");
        if (toggleAp) {
            toggleAp.addEventListener("click", (e) => {
                e.preventDefault();
                this.togglePass("ap-pass");
            });
        }

        document.getElementById("network-test-btn")?.addEventListener("click", () => this.runDiagnostics());
        document.getElementById("close-diagnostics")?.addEventListener("click", () => this.closeDiagnostics());
        document.getElementById("export-diagnostics")?.addEventListener("click", () => this.exportDiagnostics());
    },

    updateNetworkStatus() {
        this.updateWiFiStatus();
        this.updateUptimeInfo();
        // Clear existing intervals to avoid stacking if init called twice? Or just trust single init.
        // Assuming single init.
        setInterval(() => this.updateWiFiStatus(), 5000);
        setInterval(() => this.updateUptimeInfo(), 1000);
    },

    updateWiFiStatus() {
        window.API.get("network/status", null, { silent: true })
            .then(data => {
                const connectedText = window.i18n.t('network.connected');
                const disconnectedText = window.i18n.t('network.disconnected');

                // WiFi Status
                const wifiStatusEl = document.getElementById("wifi-status");
                if (wifiStatusEl) {
                    wifiStatusEl.textContent = data.wifi_connected ? connectedText : disconnectedText;
                    wifiStatusEl.style.color = data.wifi_connected ? "var(--color-optimal)" : "var(--color-critical)";
                }
                Utils.setText("wifi-ssid", data.wifi_ssid || "--");
                Utils.setText("signal-dbm", (data.wifi_rssi || -100) + " dBm");

                const qual = data.signal_quality || 0;
                const signalBar = document.getElementById("signal-bar");
                if (signalBar) {
                    signalBar.style.width = qual + "%";
                    signalBar.style.background = qual > 75 ? "var(--color-optimal)" : qual > 50 ? "var(--color-normal)" : qual > 25 ? "var(--color-warning)" : "var(--color-critical)";
                }

                let qualText = window.i18n.t('network.signal_poor');
                if (qual > 75) qualText = window.i18n.t('network.signal_excellent');
                else if (qual > 50) qualText = window.i18n.t('network.signal_good');
                else if (qual > 25) qualText = window.i18n.t('network.signal_fair');
                Utils.setText("signal-quality", qualText);

                Utils.setText("ip-address", data.wifi_ip || "--");
                Utils.setText("gateway-address", data.wifi_gateway || "--");
                Utils.setText("mac-address", data.wifi_mac || "--");

                // Ethernet Status
                const ethConnected = data.eth_connected;
                const ethStatusEl = document.getElementById("eth-status");
                if (ethStatusEl) {
                    ethStatusEl.textContent = ethConnected ? connectedText : disconnectedText;
                    ethStatusEl.style.color = ethConnected ? "var(--color-optimal)" : "var(--color-critical)";
                }
                Utils.setText("eth-speed", ethConnected ? data.eth_speed + " Mbps" : "--");
                Utils.setText("eth-duplex", ethConnected ? (data.eth_duplex ? window.i18n.t('network.full_duplex') : window.i18n.t('network.half_duplex')) : "--");
                Utils.setText("eth-ip", data.eth_ip || "--");
                Utils.setText("eth-gateway", data.eth_gateway || "--");
                Utils.setText("eth-mac", data.eth_mac || "--");

                // Uptime
                if (data.uptime_ms) {
                    const sec = Math.floor(data.uptime_ms / 1000);
                    const h = Math.floor(sec / 3600);
                    const m = Math.floor((sec % 3600) / 60);
                    Utils.setText("device-uptime", `${h} h ${m} m`);
                }
            })
            .catch(() => { /* Silent error */ });
    },

    updateLatency() {
        const latency = SharedWebSocket.latency || 0;
        if (latency > 0) {
            this.latencyHistory.push(latency);
            if (this.latencyHistory.length > this.maxHistoryLength) this.latencyHistory.shift();

            Utils.setText("latency-ms", latency + " ms");

            const min = Math.min(...this.latencyHistory);
            const max = Math.max(...this.latencyHistory);

            Utils.setText("latency-min", min + " ms");
            Utils.setText("latency-max", max + " ms");
        }
    },

    updateModbusStatus() {
        const data = AppState.data;
        const vfdConnected = data.vfd && data.vfd.connected;
        const statusEl = document.getElementById("modbus-status");

        if (statusEl) {
            statusEl.textContent = vfdConnected ? window.i18n.t('network.connected') : window.i18n.t('network.disconnected');
            statusEl.style.color = vfdConnected ? "var(--color-optimal)" : "var(--color-critical)";
        }

        const naEl = document.getElementById("vfd-na");
        if (naEl) naEl.style.display = vfdConnected ? "none" : "inline";

        Utils.setText("vfd-latency", "-- ms"); // Not implemented in backend yet
        Utils.setText("modbus-last-read", new Date().toLocaleTimeString());
    },

    updateUptimeInfo() {
        const diff = Date.now() - this.connectionStartTime;
        const min = Math.floor(diff / 60000);

        Utils.setText("connection-duration", min + " m");
        Utils.setText("reconnect-count", this.reconnectCount);
        Utils.setText("packets-sent", SharedWebSocket.packetsSent || 0);
        Utils.setText("packets-received", SharedWebSocket.packetsReceived || 0);
        Utils.setText("data-received", ((SharedWebSocket.dataReceivedBytes || 0) / 1024).toFixed(1) + " KB");
    },

    startLatencyMonitoring() {
        setInterval(() => {
            this.updateLatency();
            this.updateModbusStatus();
        }, 2000);
    },

    sendPing() {
        AlertManager.add(window.i18n.t('network.pinging_msg'), "info");
        SharedWebSocket.ping();
    },

    reconnectDevice() {
        // Reconnect via API simply triggers backend to reset WiFi or similar? 
        // Or is it literally just restarting the request?
        // Original code: `fetch("/api/network/reconnect", { method: "POST" })`

        window.API.post("network/reconnect", {}, "reconnect-btn")
            .then(data => {
                if (data.success) {
                    AlertManager.add(window.i18n.t('network.reconnect_success'), "success", 2000);
                    this.reconnectCount++;
                } else {
                    AlertManager.add(window.i18n.t('network.reconnect_failed'), "error");
                }
            })
            .catch(() => AlertManager.add("Reconnect API failed", "error"));
    },

    runDiagnostics() {
        const modal = document.getElementById("network-diagnostics-modal");
        if (modal) modal.style.display = "flex";

        const output = document.getElementById("diagnostics-output");
        if (!output) return;

        output.textContent = window.i18n.t('network.running_diag_msg') + "\n\n";

        window.API.get("network/status")
            .then(data => {
                const sec = Math.floor((data.uptime_ms || 0) / 1000);
                const h = Math.floor(sec / 3600);
                const m = Math.floor((sec % 3600) / 60);
                const qual = data.signal_quality;

                let qualText = window.i18n.t('network.signal_poor');
                if (qual > 75) qualText = window.i18n.t('network.signal_excellent');
                else if (qual > 50) qualText = window.i18n.t('network.signal_good');
                else if (qual > 25) qualText = window.i18n.t('network.signal_fair');

                const reportLines = [
                    "WiFi Status: " + (data.wifi_connected ? window.i18n.t('network.connected') : window.i18n.t('network.disconnected')),
                    `SSID: ${data.wifi_ssid || "N/A"}`,
                    `WiFi Signal: ${data.wifi_rssi} dBm (${qualText})`,
                    `WiFi IP: ${data.wifi_ip || "N/A"}`,
                    `WiFi MAC: ${data.wifi_mac || "N/A"}`,
                    `Gateway: ${data.wifi_gateway || "N/A"}`,
                    `DNS: ${data.wifi_dns || "N/A"}`,
                    `Signal Quality: ${qual}%`,
                    "",
                    "Ethernet Status: " + (data.eth_connected ? window.i18n.t('network.connected') : window.i18n.t('network.disconnected')),
                    `Ethernet IP: ${data.eth_ip || "N/A"}`,
                    `Ethernet MAC: ${data.eth_mac || "N/A"}`,
                    "Link Speed: " + (data.eth_connected ? data.eth_speed + " Mbps" : "N/A"),
                    "Duplex: " + (data.eth_connected ? (data.eth_duplex ? window.i18n.t('network.full_duplex') : window.i18n.t('network.half_duplex')) : "N/A"),
                    "",
                    `Uptime: ${h}h ${m}m`,
                    "",
                    (data.wifi_connected || data.eth_connected) ? "✓ Network operational" : "✗ Network disconnected"
                ];

                let i = 0;
                const typing = setInterval(() => {
                    if (i < reportLines.length) {
                        output.textContent += reportLines[i] + "\n";
                        output.scrollTop = output.scrollHeight;
                        i++;
                    } else {
                        clearInterval(typing);
                    }
                }, 150);
            })
            .catch(e => {
                output.textContent += "Error fetching network status: " + e.message + "\n";
            });
    },

    closeDiagnostics() {
        const modal = document.getElementById("network-diagnostics-modal");
        if (modal) modal.style.display = "none";
    },

    exportDiagnostics() {
        const output = document.getElementById("diagnostics-output");
        const text = output ? output.innerText : "No diagnostics data found.";
        const content = `Network Diagnostics Report\nGenerated: ${(new Date()).toLocaleString()}\n\n${text}\n`;

        const blob = new Blob([content], { type: "text/plain" });
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = `network-diagnostics-${Date.now()}.txt`;
        a.click();
        window.URL.revokeObjectURL(url);

        AlertManager.add(window.i18n.t('network.exported_msg'), "success", 2000);
    },

    loadWiFiConfig() {
        console.log("[Network] Loading WiFi configuration");
        window.API.get("config/get?category=5")
            .then(data => {
                if (data.error) {
                    console.error("[Network] Config load error:", data);
                    this.setStatus("station", "Error: " + data.error, "error");
                    this.setStatus("ap", "Error: " + data.error, "error");
                } else {
                    const cfg = data.config || {};
                    Utils.setValue("station-ssid", cfg.wifi_ssid || "");
                    Utils.setValue("station-pass", cfg.wifi_pass || "");
                    Utils.setValue("ap-ssid", cfg.wifi_ap_ssid || "");
                    Utils.setValue("ap-pass", cfg.wifi_ap_pass || "");

                    const ethEn = document.getElementById("eth-enabled");
                    if (ethEn) ethEn.checked = !!cfg.eth_en;

                    const apEn = document.getElementById("ap-enabled");
                    if (apEn) {
                        apEn.checked = !!cfg.wifi_ap_en;
                        const fields = document.getElementById("ap-settings-fields");
                        if (fields) fields.style.display = apEn.checked ? "block" : "none";
                    }

                    this.setStatus("station", "", "");
                    this.setStatus("ap", "", "");
                }
            })
            .catch(e => {
                console.error("[Network] Failed to load WiFi config:", e);
                this.setStatus("station", "Load failed", "error");
                this.setStatus("ap", "Load failed", "error");
            });
    },

    async saveEthConfig() {
        const enabled = document.getElementById("eth-enabled").checked ? 1 : 0;
        try {
            await this.setConfig("eth_en", enabled);
            AlertManager.add("Ethernet settings saved. Reboot required.", "success");
        } catch (e) {
            // API client already showed error
        }
    },

    async saveStationConfig() {
        const ssid = document.getElementById("station-ssid").value;
        const pass = document.getElementById("station-pass").value;

        if (ssid) {
            this.setStatus("station", window.i18n.t('network.saving'), "");
            try {
                // Batch or sequential? Existing code did sequential await.
                await this.setConfig("wifi_ssid", ssid);
                if (pass) await this.setConfig("wifi_pass", pass);

                AlertManager.add("Station settings saved. Reconnecting...", "success");
                this.setStatus("station", window.i18n.t('network.saved'), "success");
            } catch (e) {
                console.error("[Network] Save station failed:", e);
                this.setStatus("station", window.i18n.t('network.error'), "error");
            }
        } else {
            AlertManager.add(window.i18n.t('network.ssid_required'), "error");
        }
    },

    async saveAPConfig() {
        const enabled = document.getElementById("ap-enabled").checked ? 1 : 0;
        const ssid = document.getElementById("ap-ssid").value;
        const pass = document.getElementById("ap-pass").value;

        if (!enabled || ssid) {
            if (enabled && pass && pass.length < 8) {
                AlertManager.add(window.i18n.t('network.ap_pass_min_error'), "error");
            } else {
                this.setStatus("ap", window.i18n.t('network.saving'), "");
                try {
                    await this.setConfig("wifi_ap_en", enabled);
                    if (enabled) {
                        await this.setConfig("wifi_ap_ssid", ssid);
                        if (pass) await this.setConfig("wifi_ap_pass", pass);
                    }

                    AlertManager.add("AP settings saved. Reboot required.", "success");
                    this.setStatus("ap", window.i18n.t('network.saved'), "success");
                } catch (e) {
                    console.error("[Network] Save AP failed:", e);
                    this.setStatus("ap", window.i18n.t('network.error'), "error");
                }
            }
        } else {
            AlertManager.add(window.i18n.t('network.ap_ssid_required'), "error");
        }
    },

    setConfig(key, value) {
        return window.API.post("config/set", { category: 5, key, value })
            .then(data => {
                if (!data.success) throw new Error(data.error || "Set failed");
                return data;
            });
    },

    togglePass(id) {
        const el = document.getElementById(id);
        if (el) {
            const isPass = el.type === "password";
            el.type = isPass ? "text" : "password";
            const btn = document.getElementById("toggle-" + id);
            if (btn) btn.textContent = isPass ? "🙈" : "👁️";
        }
    },

    setStatus(id, text, type) {
        const el = document.getElementById(`${id}-config-status`);
        if (el) {
            el.textContent = text;
            el.className = "card-status " + (type || "");
        }
    },

    onStateChanged() {
        // Handle global state updates if needed (e.g. from websocket)
    },

    cleanup() {
        console.log("[Network] Cleaning up");
    }
};

window.currentPageModule = NetworkModule;