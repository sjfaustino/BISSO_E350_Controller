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

        // Load I/O diagnostics
        this.loadIODiagnostics();
        this.ioInterval = setInterval(() => this.loadIODiagnostics(), 2000);

        // Setup refresh button
        document.getElementById('refresh-io-diag-btn')?.addEventListener('click', () => {
            this.loadIODiagnostics();
        });
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
            const signalKey = select.dataset.signal;

            // Clear existing options
            select.innerHTML = '<option value="">-- Select Pin --</option>';

            // Add output pin options (1-16) for output signals
            if (signalKey && signalKey.startsWith('output_')) {
                for (let i = 1; i <= 16; i++) {
                    const opt = document.createElement('option');
                    opt.value = i;
                    opt.textContent = `OUT ${i}`;
                    select.appendChild(opt);
                }
            }
            // Add input pin options (1-16) for input signals
            else if (signalKey && signalKey.startsWith('input_')) {
                for (let i = 1; i <= 16; i++) {
                    const opt = document.createElement('option');
                    opt.value = i;
                    opt.textContent = `IN ${i}`;
                    select.appendChild(opt);
                }
            }
            // Use GPIO pins from API data
            else if (this.pinData.length > 0) {
                this.pinData.forEach(pin => {
                    const opt = document.createElement('option');
                    opt.value = pin.gpio;
                    opt.textContent = `GPIO ${pin.gpio} (${pin.silk || pin.note || ''})`;
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
        });

        // Render pin summary table
        this.renderPinSummary();
    },

    renderPinSummary() {
        const container = document.getElementById('pin-summary-table');
        if (!container) return;

        let html = `
            <table style="width: 100%; font-size: 12px; border-collapse: collapse;">
                <thead>
                    <tr style="background: var(--bg-secondary);">
                        <th style="padding: 8px; text-align: left;">Signal</th>
                        <th style="padding: 8px; text-align: left;">Pin</th>
                        <th style="padding: 8px; text-align: left;">Type</th>
                    </tr>
                </thead>
                <tbody>
        `;

        if (this.signalData) {
            this.signalData.forEach(sig => {
                html += `
                    <tr style="border-bottom: 1px solid var(--border-subtle);">
                        <td style="padding: 6px 8px;">${sig.name || sig.key}</td>
                        <td style="padding: 6px 8px;">${sig.current_pin || sig.default_pin || '--'}</td>
                        <td style="padding: 6px 8px;">${sig.type || '--'}</td>
                    </tr>
                `;
            });
        }

        html += '</tbody></table>';
        container.innerHTML = html;
    },

    loadConfigValues() {
        // Load checkbox states for enable/disable options
        fetch('/api/config')
            .then(r => r.json())
            .then(config => {
                // Tower light enabled
                const towerEn = document.getElementById('tower_enabled');
                if (towerEn) {
                    towerEn.checked = config.tower_en === 1 || config.tower_en === "1";
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
            })
            .catch(err => console.warn('[Hardware] Config load error:', err));
    },

    setupEventListeners() {
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

        // Collect all pin assignments
        const assignments = {};
        document.querySelectorAll('.pin-select').forEach(select => {
            if (select.dataset.signal && select.value) {
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
            } else {
                configUpdates[input.dataset.config] = input.value;
            }
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
        // Save each config value
        const promises = Object.entries(updates).map(([key, value]) => {
            return fetch('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ key, value: String(value) })
            });
        });
        return Promise.all(promises);
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

    loadIODiagnostics() {
        // Initialize grids with empty LEDs
        const inputGrid = document.getElementById('io-input-grid');
        const outputGrid = document.getElementById('io-output-grid');

        if (inputGrid && inputGrid.children.length === 0) {
            for (let i = 1; i <= 16; i++) {
                inputGrid.innerHTML += `
                    <div class="io-diag-pin">
                        <div class="pin-led off" id="io-in-${i}"></div>
                        <span>IN${i}</span>
                    </div>`;
            }
        }

        if (outputGrid && outputGrid.children.length === 0) {
            for (let i = 1; i <= 16; i++) {
                outputGrid.innerHTML += `
                    <div class="io-diag-pin">
                        <div class="pin-led off" id="io-out-${i}"></div>
                        <span>OUT${i}</span>
                    </div>`;
            }
        }

        // Fetch actual I/O states
        fetch('/api/hardware/io')
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    // Update input LEDs
                    data.inputs?.forEach((inp, idx) => {
                        const led = document.getElementById(`io-in-${idx + 1}`);
                        if (led) {
                            led.classList.remove('on', 'off');
                            led.classList.add(inp.state ? 'on' : 'off');
                        }
                    });

                    // Update output LEDs
                    data.outputs?.forEach((outp, idx) => {
                        const led = document.getElementById(`io-out-${idx + 1}`);
                        if (led) {
                            led.classList.remove('on', 'off');
                            led.classList.add(outp.state ? 'on' : 'off');
                        }
                    });

                    // Update E-Stop and timestamp
                    const estopEl = document.getElementById('hw-estop-state');
                    const timestampEl = document.getElementById('io-last-update');
                    if (estopEl) estopEl.textContent = data.estop ? '⚠️ ACTIVE' : '✅ OK';
                    if (timestampEl) timestampEl.textContent = new Date().toLocaleTimeString();
                }
            })
            .catch(err => console.warn('[Hardware] Failed to load I/O status:', err));
    },

    cleanup() {
        console.log('[Hardware] Cleaning up');
        if (this.ioInterval) {
            clearInterval(this.ioInterval);
            this.ioInterval = null;
        }
    }
};

// Register this module as the current page module
window.currentPageModule = window.HardwareModule;
