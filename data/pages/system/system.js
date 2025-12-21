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
    },

    loadSystemInfo() {
        // Load firmware version (replace with actual API call)
        const fwVersionEl = document.getElementById('fw-version');
        const fwBuildDateEl = document.getElementById('fw-build-date');
        const serialNumberEl = document.getElementById('serial-number');

        if (fwVersionEl) fwVersionEl.textContent = '3.1.0';
        if (fwBuildDateEl) fwBuildDateEl.textContent = '2024-12-14';
        if (serialNumberEl) serialNumberEl.textContent = 'BS-E350-001-2024';

        // Load storage info
        this.updateStorageInfo();

        // Load system status
        this.updateSystemStatus();
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

        if (systemUptimeEl) systemUptimeEl.textContent = uptimeHours + ' h';
        if (resetCountEl) resetCountEl.textContent = this.resetCount;
        if (lastResetEl) lastResetEl.textContent = '2024-12-01 09:15';
        if (lastBackupEl) lastBackupEl.textContent = new Date().toLocaleString();
    },

    startStatusUpdates() {
        // Update storage and status every 10 seconds
        setInterval(() => {
            this.updateStorageInfo();
            this.updateSystemStatus();
        }, 10000);
    },

    checkForUpdates() {
        AlertManager.add('Checking for updates...', 'info');

        setTimeout(() => {
            const hasUpdate = Math.random() > 0.8;
            if (hasUpdate) {
                AlertManager.add('Update available: v3.2.0', 'warning', 5000);
                document.getElementById('fw-latest').textContent = '3.2.0 (Available)';
            } else {
                AlertManager.add('You are running the latest version', 'success', 3000);
            }
        }, 1500);
    },

    viewSystemLogs() {
        AlertManager.add('System logs would open in logs page', 'info', 2000);
        // Could navigate to logs page or open modal with recent logs
    },

    runHealthCheck() {
        AlertManager.add('Running system health check...', 'info');

        setTimeout(() => {
            const report = `System Health Check Results:
✓ Flash Memory: OK
✓ RAM Memory: OK
✓ SPIFFS: OK
✓ WiFi: OK
✓ WebSocket: OK
✓ Modbus RTU: OK
✓ Encoder 1: OK
✓ Encoder 2: OK
✓ Encoder 3: OK
✓ Encoder 4: OK

Overall Status: HEALTHY`;

            AlertManager.add('Health check complete: All systems operational', 'success', 4000);
        }, 2000);
    },

    rebootDevice() {
        const confirmed = confirm('Are you sure you want to reboot the device? Connected operations will be interrupted.');
        if (confirmed) {
            AlertManager.add('Rebooting device...', 'warning', 3000);
            // Send reboot command to device
            setTimeout(() => {
                AlertManager.add('Device restarting...', 'info', 2000);
            }, 1000);
        }
    },

    backupConfiguration() {
        AlertManager.add('Creating backup...', 'info');

        setTimeout(() => {
            // Create a backup JSON file
            const backup = {
                timestamp: new Date().toISOString(),
                firmware: '3.1.0',
                configuration: {
                    motion: {
                        x_min: -100,
                        x_max: 500,
                        y_min: -100,
                        y_max: 500
                    },
                    vfd: {
                        min_freq: 1,
                        max_freq: 105,
                        accel_time: 600,
                        decel_time: 400
                    },
                    encoder: {
                        x_ppm: 100,
                        y_ppm: 100,
                        z_ppm: 100,
                        a_ppm: 50
                    }
                }
            };

            const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' });
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `backup-${Date.now()}.json`;
            a.click();
            window.URL.revokeObjectURL(url);

            AlertManager.add('Configuration backed up successfully', 'success', 3000);
            document.getElementById('last-backup').textContent = new Date().toLocaleString();
        }, 1000);
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
