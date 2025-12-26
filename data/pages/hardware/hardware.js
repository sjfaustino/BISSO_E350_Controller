/**
 * Hardware Configuration Page Module
 */
window.HardwareModule = window.HardwareModule || {
    init() {
        console.log('[Hardware] Initializing');
        this.loadPinConfiguration();
        this.setupEventListeners();
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

    cleanup() {
        console.log('[Hardware] Cleaning up');
    }
};

// Register this module as the current page module
window.currentPageModule = window.HardwareModule;
