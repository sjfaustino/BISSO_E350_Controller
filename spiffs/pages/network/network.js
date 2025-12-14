/**
 * Network & Connectivity Dashboard Module
 */
const NetworkModule = {
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

        document.getElementById('wifi-status').textContent = wifiStatus;
        document.getElementById('wifi-ssid').textContent = ssid;
        document.getElementById('signal-dbm').textContent = signal + ' dBm';

        // Calculate quality percentage (0-100)
        const quality = Math.max(0, Math.min(100, (signal + 100) * 2));
        document.getElementById('signal-bar').style.width = quality + '%';

        const qualityText = quality > 75 ? 'Excellent' : quality > 50 ? 'Good' : quality > 25 ? 'Fair' : 'Poor';
        document.getElementById('signal-quality').textContent = qualityText;

        // Color code the bar
        const bar = document.getElementById('signal-bar');
        if (quality > 75) {
            bar.style.background = 'var(--color-optimal)';
        } else if (quality > 50) {
            bar.style.background = 'var(--color-normal)';
        } else if (quality > 25) {
            bar.style.background = 'var(--color-warning)';
        } else {
            bar.style.background = 'var(--color-critical)';
        }
    },

    updateLatency() {
        // Replace with actual WebSocket ping measurement
        const latency = Math.round(Math.random() * 40 + 15); // 15-55ms
        this.latencyHistory.push(latency);
        if (this.latencyHistory.length > this.maxHistoryLength) {
            this.latencyHistory.shift();
        }

        document.getElementById('latency-ms').textContent = latency + ' ms';

        if (this.latencyHistory.length > 0) {
            const min = Math.min(...this.latencyHistory);
            const max = Math.max(...this.latencyHistory);
            document.getElementById('latency-min').textContent = min + ' ms';
            document.getElementById('latency-max').textContent = max + ' ms';
        }
    },

    updateModbusStatus() {
        // Simulated Modbus RTU status
        const modbusStatus = 'Connected';
        const vfdLatency = Math.round(Math.random() * 20 + 10); // 10-30ms
        const lastRead = new Date().toLocaleTimeString();

        document.getElementById('modbus-status').textContent = modbusStatus;
        document.getElementById('vfd-latency').textContent = vfdLatency + ' ms';
        document.getElementById('modbus-last-read').textContent = lastRead;
    },

    updateUptimeInfo() {
        // Device uptime
        const now = Date.now();
        const uptimeMs = now - this.connectionStartTime;
        const uptimeHours = Math.floor(uptimeMs / (1000 * 60 * 60));
        const uptimeMins = Math.floor((uptimeMs % (1000 * 60 * 60)) / (1000 * 60));

        document.getElementById('device-uptime').textContent = uptimeHours + ' h';
        document.getElementById('connection-duration').textContent = uptimeMins + ' m';
        document.getElementById('reconnect-count').textContent = this.reconnectCount;

        // IP and MAC (static for now)
        document.getElementById('ip-address').textContent = '192.168.1.100';
        document.getElementById('mac-address').textContent = 'AA:BB:CC:DD:EE:FF';

        // Packet statistics
        const packetsSent = Math.floor(Math.random() * 5000);
        const packetsReceived = Math.floor(Math.random() * 4800);
        const errors = Math.floor(Math.random() * 10);
        const lossRate = packetsReceived > 0 ? ((1 - packetsReceived / packetsSent) * 100).toFixed(2) : 0;

        document.getElementById('packets-sent').textContent = packetsSent;
        document.getElementById('packets-received').textContent = packetsReceived;
        document.getElementById('error-count').textContent = errors;
        document.getElementById('loss-rate').textContent = lossRate + '%';

        // Data received (KB)
        const dataKB = Math.floor(Math.random() * 1000 + 100);
        document.getElementById('data-received').textContent = dataKB + ' KB';
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
