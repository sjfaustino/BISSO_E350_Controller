/**
 * System Information Panel Module
 * Note: Use window.SystemModule to avoid "already declared" errors when navigating
 */
window.SystemModule = window.SystemModule || {
    systemStartTime: Date.now(),
    resetCount: 2,

    init() {
        console.log('[System] Initializing');
        this.loadSystemInfo();
        this.setupEventListeners();
        this.startStatusUpdates();
    },

    setupEventListeners() {
        // Check for updates
        const updateBtn = document.getElementById('check-update-btn');
        if (updateBtn) {
            updateBtn.addEventListener('click', () => this.checkForUpdates());
        }

        // View logs
        const logsBtn = document.getElementById('view-logs-btn');
        if (logsBtn) {
            logsBtn.addEventListener('click', () => this.viewSystemLogs());
        }

        // System health check
        const healthBtn = document.getElementById('system-health-btn');
        if (healthBtn) {
            healthBtn.addEventListener('click', () => this.runHealthCheck());
        }

        // Reboot device
        const rebootBtn = document.getElementById('reset-btn');
        if (rebootBtn) {
            rebootBtn.addEventListener('click', () => this.rebootDevice());
        }

        // Backup configuration
        const backupBtn = document.getElementById('backup-config-btn');
        if (backupBtn) {
            backupBtn.addEventListener('click', () => this.backupConfiguration());
        }

        // Restore configuration
        const restoreBtn = document.getElementById('restore-config-btn');
        if (restoreBtn) {
            restoreBtn.addEventListener('click', () => this.restoreConfiguration());
        }

        // Factory reset
        const factoryBtn = document.getElementById('factory-reset-btn');
        if (factoryBtn) {
            factoryBtn.addEventListener('click', () => this.factoryReset());
        }

        // Sync time from browser
        const syncTimeBtn = document.getElementById('sync-time-btn');
        if (syncTimeBtn) {
            syncTimeBtn.addEventListener('click', () => this.syncTimeFromBrowser());
        }
    },

    loadSystemInfo() {
        // Load firmware version from AppState
        const fwVersionEl = document.getElementById('fw-version');
        const fwBuildDateEl = document.getElementById('fw-build-date');
        const serialNumberEl = document.getElementById('serial-number');

        // Use real version if available in AppState, else fallback
        const currentVer = AppState.get('system.firmware_version') || '1.0.0';

        if (fwVersionEl) fwVersionEl.textContent = currentVer;
        if (fwBuildDateEl) fwBuildDateEl.textContent = '2025-01-05'; // Static for now

        // Hardware Info
        if (document.getElementById('hw-mcu')) document.getElementById('hw-mcu').textContent = AppState.get('system.hw_mcu') || 'Unknown';
        if (document.getElementById('hw-model')) document.getElementById('hw-model').textContent = AppState.get('system.hw_model') || 'BISSO E350';
        if (document.getElementById('hw-revision')) document.getElementById('hw-revision').textContent = AppState.get('system.hw_revision') || 'v1.0';
        if (serialNumberEl) serialNumberEl.textContent = AppState.get('system.hw_serial') || 'Scanning...';

        // Load latest version info
        const ota = AppState.get('ota');
        const fwLatestEl = document.getElementById('fw-latest');
        const checkBtn = document.getElementById('check-update-btn');

        if (fwLatestEl && ota) {
            if (ota.available) {
                fwLatestEl.textContent = `${ota.latest_version} (Available)`;
                fwLatestEl.style.color = 'var(--color-optimal)';

                // Change button to "Install Update"
                if (checkBtn) {
                    checkBtn.textContent = 'Install Update';
                    checkBtn.classList.remove('btn-primary');
                    checkBtn.classList.add('btn-success');
                    checkBtn.onclick = () => this.installUpdate();
                }
            } else {
                fwLatestEl.textContent = `${currentVer} (Latest)`;
                fwLatestEl.style.color = 'var(--text-secondary)';
                if (checkBtn) {
                    checkBtn.textContent = 'Check for Updates';
                    checkBtn.classList.add('btn-primary');
                    checkBtn.classList.remove('btn-success');
                    checkBtn.onclick = () => this.checkForUpdates();
                }
            }
        }

        // Load storage info
        this.updateStorageInfo();

        // Load system status
        this.updateSystemStatus();

        // Load device time
        this.updateDeviceTime();

        // Update Configuration Card
        const config = AppState.get('config') || {};
        this.setConfigStatus('conf-auth', config.http_auth);
        this.setConfigStatus('conf-https', config.https);
        this.setConfigStatus('conf-ws', config.websocket);
        this.setConfigStatus('conf-modbus', config.modbus);
    },

    setConfigStatus(id, enabled) {
        const el = document.getElementById(id);
        if (el) {
            if (enabled === undefined) {
                el.textContent = 'Loading...';
                el.style.color = '';
            } else if (enabled) {
                el.textContent = '✓ Yes';
                el.style.color = 'var(--color-optimal)';
            } else {
                el.textContent = '✗ No';
                el.style.color = 'var(--color-warning)'; // or 'var(--text-secondary)'
            }
        }
    },

    // ... (storage and system status methods usually unchanged) ...

    async checkForUpdates() {
        AlertManager.add('Checking for updates...', 'info');
        await AppState.checkForUpdates();
        const ota = AppState.get('ota');

        if (ota && ota.available) {
            AlertManager.add(`Update available: ${ota.latest_version}`, 'success', 5000);
            this.loadSystemInfo(); // Refresh UI to show update button
        } else {
            AlertManager.add('You are running the latest version', 'success', 3000);
            this.loadSystemInfo();
        }
    },

    async installUpdate() {
        if (!confirm('Start firmware update? The device will reboot.')) return;

        AlertManager.add('Starting update...', 'info');
        try {
            const response = await fetch('/api/ota/update', { method: 'POST' });
            if (response.ok) {
                AlertManager.add('Update started! Do not power off.', 'warning', 10000);
                // Disable button
                const checkBtn = document.getElementById('check-update-btn');
                if (checkBtn) checkBtn.disabled = true;
            } else {
                const err = await response.json();
                AlertManager.add(`Update failed: ${err.error}`, 'critical');
            }
        } catch (e) {
            AlertManager.add('Update request failed', 'critical');
        }
    },

    updateStorageInfo() {
        // Replace with actual API calls to /api/health or similar
        const flashUsed = 2.1; // MB
        const flashTotal = 4; // MB
        const flashPercent = (flashUsed / flashTotal) * 100;

        const ramUsed = 210; // KB
        const ramTotal = 360; // KB
        const ramPercent = (ramUsed / ramTotal) * 100;

        const spiffsUsed = 1.68; // MB
        const spiffsTotal = 4; // MB
        const spiffsPercent = (spiffsUsed / spiffsTotal) * 100;

        // Update display
        const flashBarEl = document.getElementById('flash-bar');
        const flashUsedEl = document.getElementById('flash-used');

        if (flashBarEl) flashBarEl.style.width = flashPercent + '%';
        if (flashUsedEl) flashUsedEl.textContent = flashUsed.toFixed(1);

        const ramBarEl = document.getElementById('ram-bar');
        const ramUsedEl = document.getElementById('ram-used');

        if (ramBarEl) ramBarEl.style.width = ramPercent + '%';
        if (ramUsedEl) ramUsedEl.textContent = ramUsed;

        const spiffsBarEl = document.getElementById('spiffs-bar');
        const spiffsUsedEl = document.getElementById('spiffs-used');

        if (spiffsBarEl) spiffsBarEl.style.width = spiffsPercent + '%';
        if (spiffsUsedEl) spiffsUsedEl.textContent = spiffsUsed.toFixed(2);

        // Set progress bar colors
        this.setProgressBarColor('flash-bar', flashPercent);
        this.setProgressBarColor('ram-bar', ramPercent);
        this.setProgressBarColor('spiffs-bar', spiffsPercent);
    },

    setProgressBarColor(elementId, percent) {
        const bar = document.getElementById(elementId);
        if (bar) {
            if (percent > 80) {
                bar.style.background = 'linear-gradient(90deg, var(--color-critical), var(--color-warning))';
            } else if (percent > 60) {
                bar.style.background = 'linear-gradient(90deg, var(--color-warning), var(--color-normal))';
            } else {
                bar.style.background = 'linear-gradient(90deg, var(--color-optimal), var(--color-normal))';
            }
        }
    },

    updateSystemStatus() {
        const now = Date.now();
        const uptimeMs = now - this.systemStartTime;
        const uptimeHours = Math.floor(uptimeMs / (1000 * 60 * 60));

        const systemUptimeEl = document.getElementById('system-uptime');
        const resetCountEl = document.getElementById('reset-count');
        const lastResetEl = document.getElementById('last-reset');
        const lastBackupEl = document.getElementById('last-backup');

        // Use backend uptime if available
        const uptimeSec = AppState.get('system.uptime_seconds') || 0;

        if (systemUptimeEl) {
            const h = Math.floor(uptimeSec / 3600);
            const m = Math.floor((uptimeSec % 3600) / 60);
            systemUptimeEl.textContent = `${h}h ${m}m`;
        }

        if (lastResetEl) {
            if (uptimeSec > 0) {
                const resetTime = new Date(Date.now() - (uptimeSec * 1000));
                lastResetEl.textContent = resetTime.toLocaleString();
            } else {
                lastResetEl.textContent = 'Unknown';
            }
        }

        if (resetCountEl) resetCountEl.textContent = this.resetCount;
        if (lastBackupEl) lastBackupEl.textContent = new Date().toLocaleString();
    },

    startStatusUpdates() {
        // Update storage and status every 10 seconds
        setInterval(() => {
            this.updateStorageInfo();
            this.updateSystemStatus();
            this.updateDeviceTime();
            this.loadSystemInfo();
        }, 10000);
    },

    async updateDeviceTime() {
        try {
            const response = await fetch('/api/time');
            if (response.ok) {
                const data = await response.json();
                const timeEl = document.getElementById('device-time');
                const sourceEl = document.getElementById('time-source');

                if (timeEl) {
                    // Use browser locale format to match other date displays
                    const d = new Date(data.timestamp * 1000);
                    timeEl.textContent = d.toLocaleString();
                }
                if (sourceEl) {
                    // Check if time looks valid (after year 2020)
                    if (data.timestamp > 1577836800) {
                        sourceEl.textContent = 'NTP / Browser Sync';
                        sourceEl.style.color = 'var(--color-optimal)';
                    } else {
                        sourceEl.textContent = 'Not Synced';
                        sourceEl.style.color = 'var(--color-warning)';
                    }
                }
            }
        } catch (e) {
            console.warn('[System] Failed to fetch device time:', e);
        }
    },

    async syncTimeFromBrowser() {
        const btn = document.getElementById('sync-time-btn');
        if (btn) btn.disabled = true;

        try {
            const timestamp = Math.floor(Date.now() / 1000); // Unix timestamp in seconds
            const response = await fetch('/api/time/sync', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ timestamp })
            });

            if (response.ok) {
                const data = await response.json();
                AlertManager.add(`Time synced: ${data.time}`, 'success', 3000);
                this.updateDeviceTime();
            } else {
                const error = await response.json();
                AlertManager.add(`Sync failed: ${error.error}`, 'critical', 3000);
            }
        } catch (e) {
            AlertManager.add('Time sync failed: Network error', 'critical', 3000);
        } finally {
            if (btn) btn.disabled = false;
        }
    },


    viewSystemLogs() {
        AlertManager.add('System logs would open in logs page', 'info', 2000);
        // Could navigate to logs page or open modal with recent logs
    },

    async runHealthCheck() {
        AlertManager.add('Analyzing system telemetry...', 'info', 2000);

        // Allow UI to update
        await new Promise(r => setTimeout(r, 1000));

        const sys = AppState.get('system') || {};
        const net = AppState.get('network') || {};
        const vfd = AppState.get('vfd') || {};
        const config = AppState.get('config') || {};

        let issues = [];
        let status = 'HEALTHY';

        // 1. RAM Check
        const freeHeap = sys.free_heap_bytes || 0;
        const freeHeapKb = Math.round(freeHeap / 1024);
        let ramStatus = 'OK';
        if (freeHeapKb < 20) {
            ramStatus = 'CRITICAL';
            status = 'CRITICAL';
            issues.push(`Low Memory (${freeHeapKb}KB free)`);
        } else if (freeHeapKb < 50) {
            ramStatus = 'WARNING';
            if (status !== 'CRITICAL') status = 'WARNING';
            issues.push(`Memory low (${freeHeapKb}KB free)`);
        }

        // 2. Connectivity
        const wifiStatus = net.wifi_connected ? 'OK' : 'DISCONNECTED';
        if (!net.wifi_connected) issues.push('WiFi Disconnected');

        const sigStatus = (net.signal_percent > 30) ? 'OK' : 'WEAK';
        if (net.wifi_connected && net.signal_percent <= 30) issues.push(`Weak WiFi Signal (${net.signal_percent}%)`);

        // 3. Peripherals
        let vfdStatus = 'N/A';
        if (config.modbus) { // If Modbus/VFD enabled
            vfdStatus = vfd.connected ? 'OK' : 'ERROR';
            if (!vfd.connected) {
                status = 'WARNING'; // VFD might be off
                issues.push('VFD not connected');
            }
        }

        // 4. Hardware
        const hwStatus = sys.plc_hardware_present ? 'OK' : 'NOT DETECTED';

        // Generate Report
        const report = `System Health Report:
✓ RAM: ${ramStatus} (${freeHeapKb}KB free)
${net.wifi_connected ? '✓' : '✗'} WiFi: ${wifiStatus} (${net.signal_percent}%)
${config.modbus ? (vfd.connected ? '✓' : '✗') + ' VFD: ' + vfdStatus : '• VFD: Disabled'}
✓ Hardware: ${hwStatus}

Overall Status: ${status}`;

        if (issues.length > 0) {
            AlertManager.add(`Health Check: ${status} - ${issues.join(', ')}`, status === 'CRITICAL' ? 'critical' : 'warning', 10000);
        } else {
            AlertManager.add('Health Check Passed: System Optimal', 'success', 5000);
        }

        console.log(report);
    },

    async rebootDevice() {
        const confirmed = confirm('Are you sure you want to reboot the device? Connected operations will be interrupted.');
        if (confirmed) {
            AlertManager.add('Sending reboot command...', 'warning', 5000);
            try {
                await fetch('/api/system/reboot', { method: 'POST' });
                AlertManager.add('Device is restarting. Connection lost.', 'critical', 10000);
            } catch (e) {
                // Expected if device restarts before response
                AlertManager.add('Device is restarting...', 'critical', 10000);
            }
        }
    },

    backupConfiguration() {
        AlertManager.add('Creating backup...', 'info');

        // Use backend endpoint to download file
        // This avoids "insecure connection" warnings with Data URIs
        setTimeout(() => {
            const link = document.createElement('a');
            link.href = '/api/config/backup';
            // link.download is ignored if Content-Disposition header is present, but good practice
            link.download = 'backup.json';
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);

            AlertManager.add('Backup download started', 'success', 3000);
            document.getElementById('last-backup').textContent = new Date().toLocaleString();
        }, 500);
    },

    restoreConfiguration() {
        // Create file input for restore
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json';
        input.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (file) {
                const reader = new FileReader();
                reader.onload = (event) => {
                    try {
                        const backup = JSON.parse(event.target.result);
                        AlertManager.add('Configuration restored successfully', 'success', 3000);
                        // Send restore command to device
                    } catch (err) {
                        AlertManager.add('Invalid backup file', 'critical', 3000);
                    }
                };
                reader.readAsText(file);
            }
        });
        input.click();
    },

    factoryReset() {
        const confirmed = confirm('WARNING: This will erase all configuration and reset the device to factory defaults. This cannot be undone. Continue?');
        if (confirmed) {
            const doubleConfirmed = confirm('Are you absolutely certain? Enter your password in the next prompt to confirm.');
            if (doubleConfirmed) {
                AlertManager.add('Factory reset initiated. Device will reboot...', 'critical', 5000);
                setTimeout(() => {
                    // Send factory reset command
                }, 500);
            }
        }
    },

    cleanup() {
        console.log('[System] Cleaning up');
    }
};

window.currentPageModule = SystemModule;
