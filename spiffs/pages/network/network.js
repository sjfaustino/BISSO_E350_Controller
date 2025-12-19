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
        this.updateNetworkStatus();
        this.setupEventListeners();
        this.startLatencyMonitoring();
        window.addEventListener('state-changed', () => this.onStateChanged());
    },

    setupEventListeners() {
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
        // Simulated data - replace with actual API calls
        this.updateWiFiStatus();
        this.updateLatency();
        this.updateModbusStatus();
        this.updateUptimeInfo();

        // Update every 5 seconds
        setInterval(() => this.updateWiFiStatus(), 5000);
        setInterval(() => this.updateUptimeInfo(), 5000);
    },

    updateWiFiStatus() {
        // Replace with actual API call to /api/health or similar
        const wifiStatus = 'Connected';
        const ssid = 'BISSO-Lab-5GHz';
        const signal = Math.round(Math.random() * 30 + 40); // -75 to -45 dBm

        const wifiStatusEl = document.getElementById('wifi-status');
        const wifiSsidEl = document.getElementById('wifi-ssid');
        const signalDbmEl = document.getElementById('signal-dbm');

        if (wifiStatusEl) wifiStatusEl.textContent = wifiStatus;
        if (wifiSsidEl) wifiSsidEl.textContent = ssid;
        if (signalDbmEl) signalDbmEl.textContent = signal + ' dBm';

        // Calculate quality percentage (0-100)
        const quality = Math.max(0, Math.min(100, (signal + 100) * 2));
        const signalBarEl = document.getElementById('signal-bar');
        if (signalBarEl) signalBarEl.style.width = quality + '%';

        const qualityText = quality > 75 ? 'Excellent' : quality > 50 ? 'Good' : quality > 25 ? 'Fair' : 'Poor';
        const signalQualityEl = document.getElementById('signal-quality');
        if (signalQualityEl) signalQualityEl.textContent = qualityText;

        // Color code the bar
        const bar = document.getElementById('signal-bar');
        if (bar) {
            if (quality > 75) {
                bar.style.background = 'var(--color-optimal)';
            } else if (quality > 50) {
                bar.style.background = 'var(--color-normal)';
            } else if (quality > 25) {
                bar.style.background = 'var(--color-warning)';
            } else {
                bar.style.background = 'var(--color-critical)';
            }
        }
    },

    updateLatency() {
        // Replace with actual WebSocket ping measurement
        const latency = Math.round(Math.random() * 40 + 15); // 15-55ms
        this.latencyHistory.push(latency);
        if (this.latencyHistory.length > this.maxHistoryLength) {
            this.latencyHistory.shift();
        }

        const latencyMsEl = document.getElementById('latency-ms');
        if (latencyMsEl) latencyMsEl.textContent = latency + ' ms';

        if (this.latencyHistory.length > 0) {
            const min = Math.min(...this.latencyHistory);
            const max = Math.max(...this.latencyHistory);

            const latencyMinEl = document.getElementById('latency-min');
            const latencyMaxEl = document.getElementById('latency-max');

            if (latencyMinEl) latencyMinEl.textContent = min + ' ms';
            if (latencyMaxEl) latencyMaxEl.textContent = max + ' ms';
        }
    },

    updateModbusStatus() {
        // Simulated Modbus RTU status
        const modbusStatus = 'Connected';
        const vfdLatency = Math.round(Math.random() * 20 + 10); // 10-30ms
        const lastRead = new Date().toLocaleTimeString();

        const modbusStatusEl = document.getElementById('modbus-status');
        const vfdLatencyEl = document.getElementById('vfd-latency');
        const modbusLastReadEl = document.getElementById('modbus-last-read');

        if (modbusStatusEl) modbusStatusEl.textContent = modbusStatus;
        if (vfdLatencyEl) vfdLatencyEl.textContent = vfdLatency + ' ms';
        if (modbusLastReadEl) modbusLastReadEl.textContent = lastRead;
    },

    updateUptimeInfo() {
        // Device uptime
        const now = Date.now();
        const uptimeMs = now - this.connectionStartTime;
        const uptimeHours = Math.floor(uptimeMs / (1000 * 60 * 60));
        const uptimeMins = Math.floor((uptimeMs % (1000 * 60 * 60)) / (1000 * 60));

        const deviceUptimeEl = document.getElementById('device-uptime');
        const connectionDurationEl = document.getElementById('connection-duration');
        const reconnectCountEl = document.getElementById('reconnect-count');

        if (deviceUptimeEl) deviceUptimeEl.textContent = uptimeHours + ' h';
        if (connectionDurationEl) connectionDurationEl.textContent = uptimeMins + ' m';
        if (reconnectCountEl) reconnectCountEl.textContent = this.reconnectCount;

        // IP and MAC (static for now)
        const ipAddressEl = document.getElementById('ip-address');
        const macAddressEl = document.getElementById('mac-address');

        if (ipAddressEl) ipAddressEl.textContent = '192.168.1.100';
        if (macAddressEl) macAddressEl.textContent = 'AA:BB:CC:DD:EE:FF';

        // Packet statistics
        const packetsSent = Math.floor(Math.random() * 5000);
        const packetsReceived = Math.floor(Math.random() * 4800);
        const errors = Math.floor(Math.random() * 10);
        const lossRate = packetsReceived > 0 ? ((1 - packetsReceived / packetsSent) * 100).toFixed(2) : 0;

        const packetsSentEl = document.getElementById('packets-sent');
        const packetsReceivedEl = document.getElementById('packets-received');
        const errorCountEl = document.getElementById('error-count');
        const lossRateEl = document.getElementById('loss-rate');

        if (packetsSentEl) packetsSentEl.textContent = packetsSent;
        if (packetsReceivedEl) packetsReceivedEl.textContent = packetsReceived;
        if (errorCountEl) errorCountEl.textContent = errors;
        if (lossRateEl) lossRateEl.textContent = lossRate + '%';

        // Data received (KB)
        const dataKB = Math.floor(Math.random() * 1000 + 100);
        const dataReceivedEl = document.getElementById('data-received');
        if (dataReceivedEl) dataReceivedEl.textContent = dataKB + ' KB';
    },

    startLatencyMonitoring() {
        // Monitor latency every 3 seconds
        setInterval(() => this.updateLatency(), 3000);
    },

    sendPing() {
        AlertManager.add('Pinging device...', 'info');
        // Replace with actual ping via WebSocket
        setTimeout(() => {
            const latency = Math.round(Math.random() * 40 + 15);
            AlertManager.add(`Ping successful: ${latency}ms`, 'success', 3000);
            this.updateLatency();
        }, 500);
    },

    reconnectDevice() {
        AlertManager.add('Reconnecting...', 'info');
        this.reconnectCount++;
        this.connectionStartTime = Date.now();

        // Simulate WebSocket reconnect
        setTimeout(() => {
            AlertManager.add('Reconnected successfully', 'success', 2000);
        }, 1000);
    },

    runDiagnostics() {
        const modal = document.getElementById('network-diagnostics-modal');
        if (modal) {
            modal.style.display = 'flex';
        }

        const output = document.getElementById('diagnostics-output');
        output.textContent = 'Running diagnostics...\n\n';

        const diagnostics = [
            'WiFi Signal: -45 dBm (Excellent)',
            'IP Address: 192.168.1.100',
            'Gateway: 192.168.1.1',
            'DNS: 8.8.8.8',
            'WebSocket Latency: 25ms',
            'Modbus RTU: Connected at 19200 baud',
            'VFD Response: 18ms',
            'Packet Loss: 0.2%',
            'Data Rate: 150 KB/s',
            'Uptime: 2h 34m',
            'Reconnects: 0',
            '\n✓ All systems operational'
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
        }, 200);
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

    onStateChanged() {
        // Update based on state changes if needed
    },

    cleanup() {
        console.log('[Network] Cleaning up');
    }
};

window.currentPageModule = NetworkModule;
