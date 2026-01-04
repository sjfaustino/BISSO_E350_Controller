/**
 * Hardware Configuration Page Module
 * Complete implementation with pin dropdown population and save functionality
 */
window.HardwareModule = window.HardwareModule || {
    ioInterval: null,
    pinData: null,
    signalData: null,

    init() {
        console.log('[Hardware] Initializing');
        this.loadPinConfiguration();
        this.setupEventListeners();

        this.setupEventListeners();
    },

    loadPinConfiguration() {
        // Fetch current pin configuration from API
        fetch('/api/hardware/pins')
            .then(response => response.json())
            .then(data => {
                console.log('[Hardware] Pin configuration loaded:', data);
                this.pinData = data.pins || [];
                this.signalData = data.signals || [];
                this.renderPinConfiguration(data);
                this.renderPinConfiguration(data);
                // loadConfigValues will call renderPinSummary after loading
                this.loadConfigValues();
            })
            .catch(error => {
                console.error('[Hardware] Failed to load pin configuration:', error);
                this.showStatus('Failed to load hardware configuration', 'error');
            });
    },

    renderPinConfiguration(config) {
        console.log('[Hardware] Rendering pin configuration');

        // Get all pin select dropdowns
        const selects = document.querySelectorAll('.pin-select');

        selects.forEach(select => {
            // Skip fixed/disabled pins (preserve HTML hardcoding)
            if (select.disabled) return;

            const signalKey = select.dataset.signal;

            // Clear existing options
            select.innerHTML = '<option value="">-- Select Pin --</option>';

            // Add output pin options (Y1-Y16 = Virtual 116-131) for output signals
            if (signalKey && signalKey.startsWith('output_')) {
                for (let i = 1; i <= 16; i++) {
                    const opt = document.createElement('option');
                    opt.value = 115 + i;  // Virtual pins 116-131
                    opt.textContent = `Y${i}`;  // Match KC868 silkscreen
                    select.appendChild(opt);
                }
            }
            // Add input pin options (X1-X16 = Virtual 100-115) for input signals
            else if (signalKey && signalKey.startsWith('input_')) {
                for (let i = 1; i <= 16; i++) {
                    const opt = document.createElement('option');
                    opt.value = 99 + i;  // Virtual pins 100-115
                    opt.textContent = `X${i}`;  // Match KC868 silkscreen
                    select.appendChild(opt);
                }
            }
            // For other signals (WJ66, etc.), use filtered GPIO list
            else if (this.pinData && this.pinData.length > 0) {
                // Only show direct GPIO pins (< 100) for non-PLC signals
                this.pinData.filter(pin => pin.gpio < 100).forEach(pin => {
                    const opt = document.createElement('option');
                    opt.value = pin.gpio;
                    opt.textContent = `${pin.silk} (GPIO ${pin.gpio})`;
                    select.appendChild(opt);
                });
            }

            // Set current value from signal data
            if (this.signalData) {
                const signal = this.signalData.find(s => s.key === signalKey);
                if (signal && signal.current_pin) {
                    select.value = signal.current_pin;
                }
            }

            // Apply defaults if no value selected (User Request)
            if (!select.value) {
                switch (signalKey) {
                    case 'wj66_rx': select.value = 14; break; // HT1
                    case 'wj66_tx': select.value = 33; break; // HT2
                    case 'output_status_green': select.value = 124; break; // Y9 (115+9)
                    case 'output_status_yellow': select.value = 125; break; // Y10
                    case 'output_status_red': select.value = 126; break; // Y11
                    case 'output_buzzer': select.value = 127; break; // Y12
                }
            }
        });

        // renderPinSummary will be called after config load
    },

    renderPinSummary() {
        const container = document.getElementById('pin-summary-table');
        if (!container) return;

        // Enabled states
        const ethEnabled = document.getElementById('eth_enabled')?.checked;
        const lcdEnabled = document.getElementById('lcd_enabled')?.checked;
        const vfdEnabled = document.getElementById('vfd_enabled')?.checked;
        const jxk10Enabled = document.getElementById('jxk10_enabled')?.checked;
        const statusEnabled = document.getElementById('status_light_enabled')?.checked;
        const buzzerEnabled = document.getElementById('buzzer_enabled')?.checked;

        // 1. Define Master Pin List (KC868-A16)
        const masterPinList = [
            // Digital Inputs
            ...Array.from({ length: 16 }, (_, i) => ({
                label: `X${i + 1}`,
                type: 'Digital Input',
                pin: 100 + i // Virtual 100-115
            })),
            // Digital Outputs
            ...Array.from({ length: 16 }, (_, i) => ({
                label: `Y${i + 1}`,
                type: 'Digital Output',
                pin: 116 + i // Virtual 116-131
            })),
            // Analog Inputs
            { label: 'CH1', type: '4/20mA Input', pin: 36 },
            { label: 'CH2', type: '4/20mA Input', pin: 34 },
            { label: 'CH3', type: '0-5V Input', pin: 35 },
            { label: 'CH4', type: '0-5V Input', pin: 39 },
            // Sensors / IO
            { label: 'HT1', type: 'Sensor/GPIO', pin: 14 },
            { label: 'HT2', type: 'Sensor/GPIO', pin: 33 }, // Typically TX
            { label: 'HT3', type: 'Sensor/GPIO', pin: 32 },
            // Comm
            { label: 'RS485_A', type: 'RS485', pin: 16 }, // RXD2
            { label: 'RS485_B', type: 'RS485', pin: 13 }, // TXD2
            { label: 'I2C_SDA', type: 'I2C', pin: 4 },
            { label: 'I2C_SCL', type: 'I2C', pin: 5 },
            // RF (Placeholder logic: if signals use them, they map here. Using -1 will show row but empty unless signal uses it)
            { label: '433MHz_RX', type: 'RF Input', pin: -1 },
            { label: '433MHz_TX', type: 'RF Output', pin: -1 }
        ];

        // 2. Build Usage Map
        const usageMap = new Map(); // pin -> [descriptions]

        // Helper to add usage
        const addUsage = (pin, desc) => {
            if (pin < 0) return;
            if (!usageMap.has(pin)) usageMap.set(pin, []);
            usageMap.get(pin).push(desc);
        };

        // Add Dynamic Signals
        if (this.signalData) {
            this.signalData.forEach(sig => {
                // Check enabled state for specific groups - REMOVED per user request
                // if (sig.key.includes('status') && !statusEnabled) return;
                // if (sig.key.includes('buzzer') && !buzzerEnabled) return;
                // if (sig.key.includes('vfd') && !vfdEnabled) return;
                // if (sig.key.includes('jxk10') && !jxk10Enabled) return;

                const pinVal = sig.current_pin !== undefined ? sig.current_pin : sig.default_pin;
                if (pinVal !== undefined) {
                    addUsage(pinVal, sig.name || sig.key);
                }
            });
        }

        // Add Fixed/Enabled Devices
        // Always show fixed devices even if disabled
        // if (ethEnabled) {
        addUsage(23, 'Ethernet MDC');
        addUsage(18, 'Ethernet MDIO');
        addUsage(17, 'Ethernet CLK/Pwr');
        // Standard RMII
        addUsage(19, 'Ethernet TXD0');
        addUsage(21, 'Ethernet TX_EN');
        addUsage(22, 'Ethernet TXD1');
        addUsage(25, 'Ethernet RXD0');
        addUsage(26, 'Ethernet RXD1');
        addUsage(27, 'Ethernet CRS_DV');
        // }
        // if (lcdEnabled) {
        addUsage(4, 'LCD Display (SDA)');
        addUsage(5, 'LCD Display (SCL)');
        // }
        // if (vfdEnabled) {
        addUsage(16, 'VFD (Altivar31)');
        addUsage(13, 'VFD (Altivar31)');
        // }
        // if (jxk10Enabled) {
        addUsage(16, 'JXK-10 Current Monitor');
        addUsage(13, 'JXK-10 Current Monitor');
        // }

        // 3. Render Table
        let html = `
            <table style="width: 100%; font-size: 12px; border-collapse: collapse;">
                <thead>
                    <tr style="background: var(--bg-secondary);">
                        <th style="padding: 8px; text-align: left; width: 70px;">Pin Label</th>
                        <th style="padding: 8px; text-align: left; width: 100px;">Type</th>
                        <th style="padding: 8px; text-align: left;">Connected Function</th>
                    </tr>
                </thead>
                <tbody>
        `;

        masterPinList.forEach((entry, index) => {
            const usages = usageMap.get(entry.pin) || [];
            // Remove duplicates
            const uniqueUsages = [...new Set(usages)];
            const usageText = uniqueUsages.length > 0 ? uniqueUsages.join(' + ') : '';

            // Styling for used vs unused
            const rowStyle = uniqueUsages.length > 0
                ? 'border-bottom: 1px solid var(--border-subtle);'
                : 'border-bottom: 1px solid var(--border-subtle); color: var(--text-secondary); opacity: 0.6;';

            const usageStyle = uniqueUsages.length > 0 ? 'font-weight: 500; color: var(--text-primary);' : '';

            html += `
                    <td style="padding: 6px 8px; font-family: monospace; font-weight: bold; color: var(--accent-primary);">${entry.label}</td>
                    <td style="padding: 6px 8px;">${entry.type}</td>
                    <td style="padding: 6px 8px; ${usageStyle} white-space: normal; word-break: break-word;">${usageText || '-'}</td>
                </tr>
            `;
        });

        html += '</tbody></table>';
        container.innerHTML = html;
    },

    getPinLabel(gpio) {
        if (gpio === undefined || gpio === null) return '--';

        // Find in pinData
        const pin = this.pinData.find(p => p.gpio === gpio);
        if (pin) {
            return pin.silk || pin.note || `GPIO ${gpio}`;
        }

        // Fallback for virtual pins if they aren't in pinData for some reason
        if (gpio >= 100 && gpio <= 115) return `X${gpio - 99}`;
        if (gpio >= 116 && gpio <= 131) return `Y${gpio - 115}`;
        if (gpio === 36) return 'CH1';
        if (gpio === 34) return 'CH2';
        if (gpio === 35) return 'CH3';
        if (gpio === 39) return 'CH4';

        return `GPIO ${gpio}`;
    },

    loadConfigValues() {
        // Load checkbox states for enable/disable options
        fetch('/api/config')
            .then(r => r.json())
            .then(config => {
                // Status light enabled
                const statusLightEn = document.getElementById('status_light_enabled');
                if (statusLightEn) {
                    statusLightEn.checked = config.status_light_en === 1 || config.status_light_en === "1";
                }

                // Ethernet enabled
                const ethEn = document.getElementById('eth_enabled');
                if (ethEn) {
                    ethEn.checked = config.eth_en !== 0 && config.eth_en !== "0";
                }

                // LCD enabled
                const lcdEn = document.getElementById('lcd_enabled');
                if (lcdEn) {
                    lcdEn.checked = config.lcd_en !== 0 && config.lcd_en !== "0";
                }

                // Buzzer enabled
                const buzzerEn = document.getElementById('buzzer_enabled');
                if (buzzerEn) {
                    buzzerEn.checked = config.buzzer_en !== 0 && config.buzzer_en !== "0";
                }

                // VFD enabled and address
                const vfdEn = document.getElementById('vfd_enabled');
                if (vfdEn) {
                    vfdEn.checked = config.vfd_en !== 0 && config.vfd_en !== "0";
                }
                const vfdAddr = document.getElementById('vfd_addr');
                if (vfdAddr && config.vfd_addr) {
                    vfdAddr.value = config.vfd_addr;
                }

                // JXK-10 enabled and address
                const jxk10En = document.getElementById('jxk10_enabled');
                if (jxk10En) {
                    jxk10En.checked = config.jxk10_en !== 0 && config.jxk10_en !== "0";
                }
                const jxk10Addr = document.getElementById('jxk10_addr');
                if (jxk10Addr && config.jxk10_addr) {
                    jxk10Addr.value = config.jxk10_addr;
                }

                // WJ66 Baud Rate
                const baudSelect = document.getElementById('wj66_baud');
                if (baudSelect) {
                    baudSelect.value = config.enc_baud || '9600';
                }

                // Update Pin Summary now that config (enabled states) is loaded
                this.renderPinSummary();
            })
            .catch(err => console.warn('[Hardware] Config load error:', err));
    },

    setupEventListeners() {
        // WJ66 Autodetect Button
        const detectBtn = document.getElementById('btn-detect-baud');
        if (detectBtn) {
            detectBtn.onclick = () => {
                detectBtn.textContent = 'Detecting...';
                detectBtn.disabled = true;

                fetch('/api/hardware/wj66/detect', { method: 'POST' })
                    .then(() => {
                        setTimeout(() => location.reload(), 8000);
                    })
                    .catch(() => {
                        detectBtn.textContent = 'Autodetect';
                        detectBtn.disabled = false;
                    });
            };
        }

        // Setup save button handler
        document.getElementById('save-config-btn')?.addEventListener('click', () => {
            this.saveConfiguration();
        });

        // Setup reload button handler
        document.getElementById('reload-config-btn')?.addEventListener('click', () => {
            this.loadPinConfiguration();
            this.showStatus('Configuration reloaded', 'info');
        });

        // Setup reset button handler
        document.getElementById('reset-defaults-btn')?.addEventListener('click', () => {
            this.resetConfiguration();
        });
    },

    saveConfiguration() {
        console.log('[Hardware] Saving configuration...');
        this.showStatus('Saving...', 'info');

        // Validate RS485 addresses (User Request)
        const vfdEn = document.getElementById('vfd_enabled')?.checked;
        const jxk10En = document.getElementById('jxk10_enabled')?.checked;
        const vfdAddr = parseInt(document.getElementById('vfd_addr')?.value || 0);
        const jxk10Addr = parseInt(document.getElementById('jxk10_addr')?.value || 0);

        if (vfdEn && jxk10En && vfdAddr === jxk10Addr) {
            this.showStatus('Error: VFD and JXK-10 cannot have the same Modbus Address', 'error');
            return;
        }

        // Collect all pin assignments
        const assignments = {};
        document.querySelectorAll('.pin-select').forEach(select => {
            if (select.dataset.signal && select.value && !select.disabled) {
                assignments[select.dataset.signal] = parseInt(select.value);
            }
        });

        // Collect config checkboxes and number inputs
        const configUpdates = {};
        document.querySelectorAll('input[data-config]').forEach(input => {
            if (input.type === 'checkbox') {
                configUpdates[input.dataset.config] = input.checked ? 1 : 0;
            } else if (input.type === 'number') {
                configUpdates[input.dataset.config] = parseInt(input.value) || 0;
            } else if (input.tagName === 'SELECT') { // Handle selects with data-config
                configUpdates[input.dataset.config] = input.value;
            } else {
                configUpdates[input.dataset.config] = input.value;
            }
        });
        // Handle selects explicitly if querySelectorAll didn't catch them above (it checks input[data-config])
        document.querySelectorAll('select[data-config]').forEach(select => {
            configUpdates[select.dataset.config] = select.value;
        });

        // Save pin assignments
        fetch('/api/hardware/pins', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(assignments)
        })
            .then(r => r.json())
            .then(data => {
                if (data.success || data.ok) {
                    // Also save config values
                    return this.saveConfigValues(configUpdates);
                } else {
                    throw new Error(data.error || 'Save failed');
                }
            })
            .then(() => {
                this.showStatus('Configuration saved! Reboot required for some changes.', 'success');
            })
            .catch(err => {
                console.error('[Hardware] Save error:', err);
                this.showStatus('Save failed: ' + err.message, 'error');
            });
    },

    saveConfigValues(updates) {
        // Batch save all config values in one request (reduces NVS wear)
        return fetch('/api/config/batch', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(updates)
        }).then(r => r.json()).then(data => {
            if (!data.success && !data.ok) {
                console.error('[Hardware] Batch save error:', data.error);
            }
            return data;
        });
    },

    resetConfiguration() {
        console.log('[Hardware] Resetting configuration...');
        if (!confirm('Reset all hardware pins to defaults? This will reboot the system.')) {
            return;
        }

        this.showStatus('Resetting...', 'info');

        fetch('/api/hardware/pins/reset', { method: 'POST' })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    this.showStatus('Configuration reset. Rebooting...', 'success');
                    setTimeout(() => location.reload(), 3000);
                } else {
                    throw new Error(data.error || 'Reset failed');
                }
            })
            .catch(error => {
                console.error('[Hardware] Reset failed:', error);
                this.showStatus('Reset failed: ' + error.message, 'error');
            });
    },

    showStatus(message, type) {
        const el = document.getElementById('status-message');
        if (el) {
            el.textContent = message;
            el.className = 'status-message ' + type;
            el.style.display = 'block';

            // Auto-hide after 5 seconds for non-error messages
            if (type !== 'error') {
                setTimeout(() => { el.style.display = 'none'; }, 5000);
            }
        }
    },

    cleanup() {
        console.log('[Hardware] Cleaning up');
    }
};

// Register this module as the current page module
window.currentPageModule = window.HardwareModule;
