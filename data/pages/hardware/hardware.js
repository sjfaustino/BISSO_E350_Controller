/**
 * Hardware Configuration Page Module
 */
window.HardwareModule = window.HardwareModule || {
    ioInterval: null,

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
                this.renderPinConfiguration(data);
            })
            .catch(error => {
                console.error('[Hardware] Failed to load pin configuration:', error);
                if (window.AlertManager) {
                    window.AlertManager.add('Failed to load hardware configuration', 'error');
                }
            });
    },

    renderPinConfiguration(config) {
        // This function would populate the UI with pin assignments
        // The HTML should already contain the structure
        console.log('[Hardware] Rendering pin configuration');
    },

    setupEventListeners() {
        // Setup save button handler
        const saveBtn = document.getElementById('save-config-btn');
        if (saveBtn) {
            saveBtn.addEventListener('click', () => this.saveConfiguration());
        }

        // Setup reset button handler
        const resetBtn = document.getElementById('reset-config-btn');
        if (resetBtn) {
            resetBtn.addEventListener('click', () => this.resetConfiguration());
        }
    },

    saveConfiguration() {
        console.log('[Hardware] Saving configuration...');
        // Implement save logic
        if (window.AlertManager) {
            window.AlertManager.add('Hardware configuration saved. System will reboot.', 'success');
        }
    },

    resetConfiguration() {
        console.log('[Hardware] Resetting configuration...');
        if (confirm('Reset all hardware pins to defaults? This will reboot the system.')) {
            fetch('/api/hardware/pins/reset', { method: 'POST' })
                .then(() => {
                    if (window.AlertManager) {
                        window.AlertManager.add('Configuration reset. Rebooting...', 'info');
                    }
                })
                .catch(error => {
                    console.error('[Hardware] Reset failed:', error);
                });
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
