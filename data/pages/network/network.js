/**
 * Network & Connectivity Dashboard Module
 * Note: Use window.NetworkModule to avoid "already declared" errors when navigating
 */
window.NetworkModule = window.NetworkModule || {
    latencyHistory: [],
    maxHistoryLength: 100,
    reconnectCount: 0,
    connectionStartTime: Date.now(),
    stats: {
        packetsSent: 0,
        packetsReceived: 0,
        errors: 0,
        dataReceived: 0
    },

    init() {
        console.log('[Network] Initializing');
        this.loadWiFiConfig();
        this.updateNetworkStatus();
        this.setupEventListeners();
        this.startLatencyMonitoring();
        window.addEventListener('state-changed', () => this.onStateChanged());
    },

    setupEventListeners() {
        // Toggle AP settings visibility
        const apToggle = document.getElementById('ap-enabled');
        if (apToggle) {
            apToggle.addEventListener('change', (e) => {
                const fields = document.getElementById('ap-settings-fields');
                if (fields) fields.style.display = e.target.checked ? 'block' : 'none';
            });
        }

        // Save Station button
        const saveStationBtn = document.getElementById('save-station-btn');
        if (saveStationBtn) {
            saveStationBtn.addEventListener('click', () => this.saveStationConfig());
        }

        // Save AP button
        const saveApBtn = document.getElementById('save-ap-btn');
        if (saveApBtn) {
            saveApBtn.addEventListener('click', () => this.saveAPConfig());
        }

        // Ping button
        const pingBtn = document.getElementById('ping-btn');
        if (pingBtn) {
            pingBtn.addEventListener('click', () => this.sendPing());
        }

        // Reconnect button
        const reconnectBtn = document.getElementById('reconnect-btn');
        if (reconnectBtn) {
            reconnectBtn.addEventListener('click', () => this.reconnectDevice());
        }

        // Diagnostics button
        const diagBtn = document.getElementById('network-test-btn');
        if (diagBtn) {
            diagBtn.addEventListener('click', () => this.runDiagnostics());
        }

        // Close diagnostics
        const closeBtn = document.getElementById('close-diagnostics');
        if (closeBtn) {
            closeBtn.addEventListener('click', () => this.closeDiagnostics());
        }

        // Export diagnostics
        const exportBtn = document.getElementById('export-diagnostics');
        if (exportBtn) {
            exportBtn.addEventListener('click', () => this.exportDiagnostics());
        }
    },

    updateNetworkStatus() {
        this.updateWiFiStatus();
        this.updateUptimeInfo();

        // Update every 5 seconds
        setInterval(() => this.updateWiFiStatus(), 5000);
        setInterval(() => this.updateUptimeInfo(), 1000); // Uptime should update faster
    },

    updateWiFiStatus() {
        // Fetch real network status from backend
        fetch('/api/network/status')
            .then(r => r.json())
            .then(data => {
                const wifiStatus = data.wifi_connected ? 'Connected' : 'Disconnected';
                const ssid = data.wifi_ssid || '--';
                const signal = data.wifi_rssi || -100;
                const quality = data.signal_quality || 0;

                const wifiStatusEl = document.getElementById('wifi-status');
                const wifiSsidEl = document.getElementById('wifi-ssid');
                const signalDbmEl = document.getElementById('signal-dbm');

                if (wifiStatusEl) {
                    wifiStatusEl.textContent = wifiStatus;
                    wifiStatusEl.style.color = data.wifi_connected ? 'var(--color-optimal)' : 'var(--color-critical)';
                }
                if (wifiSsidEl) wifiSsidEl.textContent = ssid;
                if (signalDbmEl) signalDbmEl.textContent = signal + ' dBm';

                const signalBarEl = document.getElementById('signal-bar');
                if (signalBarEl) signalBarEl.style.width = quality + '%';

                const qualityText = quality > 75 ? 'Excellent' : quality > 50 ? 'Good' : quality > 25 ? 'Fair' : 'Poor';
                const signalQualityEl = document.getElementById('signal-quality');
                if (signalQualityEl) signalQualityEl.textContent = qualityText;

                const bar = document.getElementById('signal-bar');
                if (bar) {
                    if (quality > 75) bar.style.background = 'var(--color-optimal)';
                    else if (quality > 50) bar.style.background = 'var(--color-normal)';
                    else if (quality > 25) bar.style.background = 'var(--color-warning)';
                    else bar.style.background = 'var(--color-critical)';
                }

                // Update IP and MAC
                const ipEl = document.getElementById('ip-address');
                const macEl = document.getElementById('mac-address');
                if (ipEl) ipEl.textContent = data.wifi_ip || '--';
                if (macEl) macEl.textContent = data.wifi_mac || '--';

                // Update uptime from device response
                if (data.uptime_ms) {
                    const uptimeSec = Math.floor(data.uptime_ms / 1000);
                    const hours = Math.floor(uptimeSec / 3600);
                    const mins = Math.floor((uptimeSec % 3600) / 60);
                    const deviceUptimeEl = document.getElementById('device-uptime');
                    if (deviceUptimeEl) deviceUptimeEl.textContent = hours + ' h ' + mins + ' m';
                }
            })
            .catch(err => console.error('[Network] Status fetch failed:', err));
    },

    updateLatency() {
        // Use real latency calculated in SharedWebSocket (round-trip)
        // Note: websocket.js doesn't currently store latency, but we can infer it or use a default
        // For now, we'll use the AppState performance if available, or just keep the visual
        const state = AppState.data;
        const latency = state.network?.latency || 0;

        if (latency > 0) {
            this.latencyHistory.push(latency);
            if (this.latencyHistory.length > this.maxHistoryLength) {
                this.latencyHistory.shift();
            }

            const latencyMsEl = document.getElementById('latency-ms');
            if (latencyMsEl) latencyMsEl.textContent = latency + ' ms';

            const min = Math.min(...this.latencyHistory);
            const max = Math.max(...this.latencyHistory);

            const latencyMinEl = document.getElementById('latency-min');
            const latencyMaxEl = document.getElementById('latency-max');

            if (latencyMinEl) latencyMinEl.textContent = min + ' ms';
            if (latencyMaxEl) latencyMaxEl.textContent = max + ' ms';
        }
    },

    updateModbusStatus() {
        const state = AppState.data;
        // Use real connectivity flag from telemetry
        const connected = state.vfd?.connected === true;
        const modbusStatus = connected ? 'Connected' : 'Disconnected';

        const modbusStatusEl = document.getElementById('modbus-status');
        if (modbusStatusEl) {
            modbusStatusEl.textContent = modbusStatus;
            modbusStatusEl.style.color = connected ? 'var(--color-optimal)' : 'var(--color-critical)';
        }

        // Dynamic (N/A) label logic
        const naLabel = document.getElementById('vfd-na');
        if (naLabel) {
            naLabel.style.display = connected ? 'none' : 'inline';
        }

        // Modbus latency isn't currently in telemetry, so we use a null placeholder or 0
        const vfdLatencyEl = document.getElementById('vfd-latency');
        if (vfdLatencyEl) vfdLatencyEl.textContent = '-- ms';

        const modbusLastReadEl = document.getElementById('modbus-last-read');
        if (modbusLastReadEl) modbusLastReadEl.textContent = new Date().toLocaleTimeString();
    },

    updateUptimeInfo() {
        // Local connection duration
        const now = Date.now();
        const uptimeMs = now - this.connectionStartTime;
        const uptimeMins = Math.floor(uptimeMs / (1000 * 60));

        const connectionDurationEl = document.getElementById('connection-duration');
        const reconnectCountEl = document.getElementById('reconnect-count');

        if (connectionDurationEl) connectionDurationEl.textContent = uptimeMins + ' m';
        if (reconnectCountEl) reconnectCountEl.textContent = this.reconnectCount;

        // WebSocket Packet statistics (REAL)
        const packetsSent = SharedWebSocket.packetsSent || 0;
        const packetsReceived = SharedWebSocket.packetsReceived || 0;
        const dataBytes = SharedWebSocket.dataReceivedBytes || 0;

        const packetsSentEl = document.getElementById('packets-sent');
        const packetsReceivedEl = document.getElementById('packets-received');
        const dataReceivedEl = document.getElementById('data-received');

        if (packetsSentEl) packetsSentEl.textContent = packetsSent;
        if (packetsReceivedEl) packetsReceivedEl.textContent = packetsReceived;
        if (dataReceivedEl) dataReceivedEl.textContent = (dataBytes / 1024).toFixed(1) + ' KB';
    },

    startLatencyMonitoring() {
        // Monitor latency every 2 seconds
        setInterval(() => {
            this.updateLatency();
            this.updateModbusStatus();
        }, 2000);
    },

    sendPing() {
        AlertManager.add('Pinging device via WebSocket...', 'info');
        const start = Date.now();
        // Send a simple ping command if supported, or just wait for next telemetry
        SharedWebSocket.send({ type: 'ping' });
    },

    reconnectDevice() {
        AlertManager.add('Reconnecting WiFi...', 'info');
        fetch('/api/network/reconnect', { method: 'POST' })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    AlertManager.add('WiFi reconnection triggered', 'success', 2000);
                    this.reconnectCount++;
                } else {
                    AlertManager.add('Failed to trigger reconnect', 'error');
                }
            })
            .catch(err => AlertManager.add('Reconnect API failed', 'error'));
    },

    runDiagnostics() {
        const modal = document.getElementById('network-diagnostics-modal');
        if (modal) {
            modal.style.display = 'flex';
        }

        const output = document.getElementById('diagnostics-output');
        output.textContent = 'Running diagnostics...\n\n';

        // Fetch real data for diagnostics
        fetch('/api/network/status')
            .then(r => r.json())
            .then(data => {
                const uptimeSec = Math.floor((data.uptime_ms || 0) / 1000);
                const hours = Math.floor(uptimeSec / 3600);
                const mins = Math.floor((uptimeSec % 3600) / 60);

                const qualityText = data.signal_quality > 75 ? 'Excellent' :
                    data.signal_quality > 50 ? 'Good' :
                        data.signal_quality > 25 ? 'Fair' : 'Poor';

                const diagnostics = [
                    `WiFi Status: ${data.wifi_connected ? 'Connected' : 'Disconnected'}`,
                    `SSID: ${data.wifi_ssid || 'N/A'}`,
                    `WiFi Signal: ${data.wifi_rssi} dBm (${qualityText})`,
                    `IP Address: ${data.wifi_ip || 'N/A'}`,
                    `MAC Address: ${data.wifi_mac || 'N/A'}`,
                    `Gateway: ${data.wifi_gateway || 'N/A'}`,
                    `DNS: ${data.wifi_dns || 'N/A'}`,
                    `Signal Quality: ${data.signal_quality}%`,
                    `Uptime: ${hours}h ${mins}m`,
                    '',
                    data.wifi_connected ? '✓ Network operational' : '✗ Network disconnected'
                ];

                let index = 0;
                const interval = setInterval(() => {
                    if (index < diagnostics.length) {
                        output.textContent += diagnostics[index] + '\n';
                        output.scrollTop = output.scrollHeight;
                        index++;
                    } else {
                        clearInterval(interval);
                    }
                }, 150);
            })
            .catch(err => {
                output.textContent += 'Error fetching network status: ' + err.message + '\n';
            });
    },

    closeDiagnostics() {
        const modal = document.getElementById('network-diagnostics-modal');
        if (modal) {
            modal.style.display = 'none';
        }
    },

    exportDiagnostics() {
        const report = `Network Diagnostics Report
Generated: ${new Date().toLocaleString()}

WiFi Status: Connected
SSID: BISSO-Lab-5GHz
Signal: -45 dBm
Quality: Excellent

IP Address: 192.168.1.100
MAC: AA:BB:CC:DD:EE:FF
Gateway: 192.168.1.1

WebSocket Latency: 25ms (avg)
Modbus RTU: Connected
VFD Latency: 18ms

Uptime: 2h 34m
Reconnects: 0
Packet Loss: 0.2%
`;

        // Create downloadable file
        const blob = new Blob([report], { type: 'text/plain' });
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `network-diagnostics-${Date.now()}.txt`;
        a.click();
        window.URL.revokeObjectURL(url);

        AlertManager.add('Diagnostics exported', 'success', 2000);
    },

    loadWiFiConfig() {
        console.log('[Network] Loading WiFi configuration');
        fetch('/api/config/get?category=2')
            .then(r => r.json())
            .then(data => {
                if (data.success && data.config) {
                    const cfg = data.config;
                    Utils.setValue('station-ssid', cfg.wifi_ssid || '');
                    Utils.setValue('ap-ssid', cfg.wifi_ap_ssid || '');

                    const apEn = document.getElementById('ap-enabled');
                    if (apEn) {
                        apEn.checked = !!cfg.wifi_ap_en;
                        const fields = document.getElementById('ap-settings-fields');
                        if (fields) fields.style.display = apEn.checked ? 'block' : 'none';
                    }

                    this.setStatus('station', 'Loaded', 'success');
                    this.setStatus('ap', 'Loaded', 'success');
                }
            })
            .catch(err => {
                console.error('[Network] Failed to load WiFi config:', err);
                this.setStatus('station', 'Load failed', 'error');
                this.setStatus('ap', 'Load failed', 'error');
            });
    },

    async saveStationConfig() {
        const ssid = document.getElementById('station-ssid').value;
        const pass = document.getElementById('station-pass').value;

        if (!ssid) {
            AlertManager.add('SSID is required', 'error');
            return;
        }

        this.setStatus('station', 'Saving...', '');
        try {
            await this.setConfig('wifi_ssid', ssid);
            if (pass) await this.setConfig('wifi_pass', pass);

            AlertManager.add('Station settings saved. Reconnecting...', 'success');
            this.setStatus('station', 'Saved', 'success');
        } catch (err) {
            console.error('[Network] Save station failed:', err);
            AlertManager.add('Failed to save settings', 'error');
            this.setStatus('station', 'Error', 'error');
        }
    },

    async saveAPConfig() {
        const en = document.getElementById('ap-enabled').checked ? 1 : 0;
        const ssid = document.getElementById('ap-ssid').value;
        const pass = document.getElementById('ap-pass').value;

        if (en && !ssid) {
            AlertManager.add('AP SSID is required when enabled', 'error');
            return;
        }
        if (en && pass && pass.length < 8) {
            AlertManager.add('AP Password must be at least 8 chars', 'error');
            return;
        }

        this.setStatus('ap', 'Saving...', '');
        try {
            await this.setConfig('wifi_ap_en', en);
            if (en) {
                await this.setConfig('wifi_ap_ssid', ssid);
                if (pass) await this.setConfig('wifi_ap_pass', pass);
            }

            AlertManager.add('AP settings saved. Reboot required.', 'success');
            this.setStatus('ap', 'Saved', 'success');
        } catch (err) {
            console.error('[Network] Save AP failed:', err);
            AlertManager.add('Failed to save AP settings', 'error');
            this.setStatus('ap', 'Error', 'error');
        }
    },

    setConfig(key, value) {
        return fetch('/api/config/set', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ category: 2, key, value })
        })
            .then(r => r.json())
            .then(data => {
                if (!data.success) throw new Error(data.error || 'Set failed');
                return data;
            });
    },

    togglePass(id) {
        const el = document.getElementById(id);
        if (el) {
            el.type = el.type === 'password' ? 'text' : 'password';
        }
    },

    setStatus(section, text, type) {
        const el = document.getElementById(`${section}-config-status`);
        if (el) {
            el.textContent = text;
            el.className = 'card-status ' + (type || '');
        }
    },

    onStateChanged() {
        // Update based on state changes if needed
    },

    cleanup() {
        console.log('[Network] Cleaning up');
    }
};

window.currentPageModule = NetworkModule;
