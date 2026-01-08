/**
 * Network & Connectivity Dashboard Module
 * Note: Use window.NetworkModule to avoid "already declared" errors when navigating
 */
// Force update of module definition to ensure new methods are available
window.NetworkModule = {
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

        // Toggle Password Visibility
        const toggleStation = document.getElementById('toggle-station-pass');
        if (toggleStation) {
            console.log('[Network] Added station toggle listener');
            toggleStation.addEventListener('click', (e) => {
                e.preventDefault(); // Prevent focus loss or other side effects
                console.log('[Network] Station toggle clicked');
                this.togglePass('station-pass');
            });
        } else {
            console.warn('[Network] Station toggle button not found');
        }

        const toggleAp = document.getElementById('toggle-ap-pass');
        if (toggleAp) {
            console.log('[Network] Added AP toggle listener');
            toggleAp.addEventListener('click', (e) => {
                e.preventDefault();
                console.log('[Network] AP toggle clicked');
                this.togglePass('ap-pass');
            });
        } else {
            console.warn('[Network] AP toggle button not found');
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
                Utils.setText('signal-quality', qualityText);

                const bar = document.getElementById('signal-bar');
                if (bar) {
                    if (quality > 75) bar.style.background = 'var(--color-optimal)';
                    else if (quality > 50) bar.style.background = 'var(--color-normal)';
                    else if (quality > 25) bar.style.background = 'var(--color-warning)';
                    else bar.style.background = 'var(--color-critical)';
                }

                // Update WiFi IP and MAC
                Utils.setText('ip-address', data.wifi_ip || '--');
                Utils.setText('gateway-address', data.wifi_gateway || '--');
                Utils.setText('mac-address', data.wifi_mac || '--');

                // Ethernet Status
                const ethConnected = data.eth_connected;
                const ethStatusEl = document.getElementById('eth-status');
                if (ethStatusEl) {
                    ethStatusEl.textContent = ethConnected ? 'Connected' : 'Disconnected';
                    ethStatusEl.style.color = ethConnected ? 'var(--color-optimal)' : 'var(--color-critical)';
                }

                Utils.setText('eth-speed', ethConnected ? data.eth_speed + ' Mbps' : '--');
                Utils.setText('eth-duplex', ethConnected ? (data.eth_duplex ? 'Full' : 'Half') : '--');
                Utils.setText('eth-ip', data.eth_ip || '--');
                Utils.setText('eth-gateway', data.eth_gateway || '--');
                Utils.setText('eth-mac', data.eth_mac || '--');

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
        // Use client-side measured latency from SharedWebSocket
        const latency = SharedWebSocket.latency || 0;

        if (latency > 0) {
            this.latencyHistory.push(latency);
            if (this.latencyHistory.length > this.maxHistoryLength) {
                this.latencyHistory.shift();
            }

            const latencyMsEl = document.getElementById('latency-ms');
            if (latencyMsEl) latencyMsEl.textContent = latency + ' ms';

            const min = Math.min(...this.latencyHistory);
            const max = Math.max(...this.latencyHistory);
            const avg = Math.round(this.latencyHistory.reduce((a, b) => a + b, 0) / this.latencyHistory.length);

            const latencyMinEl = document.getElementById('latency-min');
            const latencyMaxEl = document.getElementById('latency-max');
            const latencyAvgEl = document.getElementById('latency-avg');

            if (latencyMinEl) latencyMinEl.textContent = min + ' ms';
            if (latencyMaxEl) latencyMaxEl.textContent = max + ' ms';
            // Assuming there might be an avg element, or just good to calculate
        }
    },

    updateModbusStatus() {
        const state = AppState.data;
        const connected = state.vfd?.connected === true;
        const modbusStatus = connected ? 'Connected' : 'Disconnected';

        const modbusStatusEl = document.getElementById('modbus-status');
        if (modbusStatusEl) {
            modbusStatusEl.textContent = modbusStatus;
            modbusStatusEl.style.color = connected ? 'var(--color-optimal)' : 'var(--color-critical)';
        }

        const naLabel = document.getElementById('vfd-na');
        if (naLabel) {
            naLabel.style.display = connected ? 'none' : 'inline';
        }

        const vfdLatencyEl = document.getElementById('vfd-latency');
        if (vfdLatencyEl) vfdLatencyEl.textContent = '-- ms';

        const modbusLastReadEl = document.getElementById('modbus-last-read');
        if (modbusLastReadEl) modbusLastReadEl.textContent = new Date().toLocaleTimeString();
    },

    updateUptimeInfo() {
        const now = Date.now();
        const uptimeMs = now - this.connectionStartTime;
        const uptimeMins = Math.floor(uptimeMs / (1000 * 60));

        const connectionDurationEl = document.getElementById('connection-duration');
        const reconnectCountEl = document.getElementById('reconnect-count');

        if (connectionDurationEl) connectionDurationEl.textContent = uptimeMins + ' m';
        if (reconnectCountEl) reconnectCountEl.textContent = this.reconnectCount;

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
        setInterval(() => {
            this.updateLatency();
            this.updateModbusStatus();
        }, 2000);
    },

    sendPing() {
        AlertManager.add('Pinging device via WebSocket...', 'info');
        SharedWebSocket.ping();
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
                    `WiFi IP: ${data.wifi_ip || 'N/A'}`,
                    `WiFi MAC: ${data.wifi_mac || 'N/A'}`,
                    `Gateway: ${data.wifi_gateway || 'N/A'}`,
                    `DNS: ${data.wifi_dns || 'N/A'}`,
                    `Signal Quality: ${data.signal_quality}%`,
                    '',
                    `Ethernet Status: ${data.eth_connected ? 'Connected' : 'Disconnected'}`,
                    `Ethernet IP: ${data.eth_ip || 'N/A'}`,
                    `Ethernet MAC: ${data.eth_mac || 'N/A'}`,
                    `Link Speed: ${data.eth_connected ? data.eth_speed + ' Mbps' : 'N/A'}`,
                    `Duplex: ${data.eth_connected ? (data.eth_duplex ? 'Full' : 'Half') : 'N/A'}`,
                    '',
                    `Uptime: ${hours}h ${mins}m`,
                    '',
                    (data.wifi_connected || data.eth_connected) ? '✓ Network operational' : '✗ Network disconnected'
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
        const output = document.getElementById('diagnostics-output');
        const content = output ? output.innerText : 'No diagnostics data found.';

        const report = `Network Diagnostics Report
Generated: ${new Date().toLocaleString()}

${content}
`;
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
        fetch('/api/config/get?category=5')
            .then(r => r.json())
            .then(data => {
                // Backend returns flat object or {error:...}, not wrapped in success/config
                if (data.error) {
                    console.error('[Network] Config load error:', data);
                    this.setStatus('station', 'Error: ' + data.error, 'error');
                    this.setStatus('ap', 'Error: ' + data.error, 'error');
                    AlertManager.add('Failed to load WiFi config: ' + data.error, 'error');
                } else {
                    const cfg = data; // Flat response
                    Utils.setValue('station-ssid', cfg.wifi_ssid || '');
                    Utils.setValue('station-pass', cfg.wifi_pass || ''); // Fixed: Populate password
                    Utils.setValue('ap-ssid', cfg.wifi_ap_ssid || '');
                    Utils.setValue('ap-pass', cfg.wifi_ap_pass || '');   // Fixed: Populate password

                    const apEn = document.getElementById('ap-enabled');
                    if (apEn) {
                        apEn.checked = !!cfg.wifi_ap_en;
                        const fields = document.getElementById('ap-settings-fields');
                        if (fields) fields.style.display = apEn.checked ? 'block' : 'none';
                    }

                    // Status update removed per user request
                    this.setStatus('station', '', '');
                    this.setStatus('ap', '', '');
                }
            })
            .catch(err => {
                console.error('[Network] Failed to load WiFi config:', err);
                this.setStatus('station', 'Load failed: ' + err.message, 'error');
                this.setStatus('ap', 'Load failed: ' + err.message, 'error');
                AlertManager.add('Network error loading config', 'error');
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
            body: JSON.stringify({ category: 5, key, value })
        })
            .then(r => r.json())
            .then(data => {
                if (!data.success) throw new Error(data.error || 'Set failed');
                return data;
            });
    },

    togglePass(id) {
        const input = document.getElementById(id);
        if (input) {
            input.type = input.type === 'password' ? 'text' : 'password';
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
