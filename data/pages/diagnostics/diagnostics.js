/**
 * Diagnostics Page Module
 * Note: Use window.DiagnosticsModule to avoid "already declared" errors when navigating
 */
window.DiagnosticsModule = window.DiagnosticsModule || {
    updateInterval: null,

    init() {
        console.log('[Diagnostics] Initializing');
        window.addEventListener('state-changed', () => this.onStateChanged());
        this.setupEventListeners();
        this.loadInitialData();

        // Start periodic updates for I/O and faults
        this.updateInterval = setInterval(() => this.loadIOStatus(), 2000);
    },

    setupEventListeners() {
        // I/O Refresh button
        document.getElementById('refresh-io-btn')?.addEventListener('click', () => {
            this.loadIOStatus();
        });

        // Clear Faults button
        document.getElementById('clear-faults-btn')?.addEventListener('click', () => {
            this.clearFaults();
        });
    },

    loadInitialData() {
        // Skip API calls in file:// or mock mode
        if (window.location.protocol === 'file:' || false) {
            console.log('[Diagnostics] Mock mode - using simulated data');
            this.simulateDiagnosticData();
            return;
        }

        this.loadIOStatus();
        this.loadFaultLog();
    },

    onStateChanged() {
        const state = AppState.data;

        // Axis diagnostics (X, Y, Z)
        ['x', 'y', 'z'].forEach((axis) => {
            const metrics = state.axis?.[axis];
            if (metrics) {
                const qualityEl = document.getElementById(`diag-${axis}-quality`);
                const jitterEl = document.getElementById(`diag-${axis}-jitter`);
                const stallsEl = document.getElementById(`diag-${axis}-stalls`);
                const activeEl = document.getElementById(`diag-${axis}-active`);

                if (qualityEl) qualityEl.textContent = metrics.quality?.toFixed(0) || '--';
                if (jitterEl) jitterEl.textContent = (metrics.jitter_mms || 0).toFixed(3) + ' mm/s';
                if (stallsEl) stallsEl.textContent = metrics.stalled ? '⚠ STALL' : '0';
                if (activeEl) activeEl.textContent = '--';
            }
        });

        // Encoder status from state
        this.updateEncoderStatus(state);

        // VFD diagnostics
        if (state.vfd) {
            const currentEl = document.getElementById('diag-vfd-current');
            const freqEl = document.getElementById('diag-vfd-freq');
            const thermalEl = document.getElementById('diag-vfd-thermal');
            const faultEl = document.getElementById('diag-vfd-fault');

            if (currentEl) currentEl.textContent = (state.vfd.current_amps || 0).toFixed(1) + ' A';
            if (freqEl) freqEl.textContent = (state.vfd.frequency_hz || 0).toFixed(1) + ' Hz';
            if (thermalEl) thermalEl.textContent = (state.vfd.thermal_percent || 0) + '%';
            if (faultEl) faultEl.textContent = '0x' + ((state.vfd.fault_code || 0).toString(16).padStart(4, '0')).toUpperCase();
        }

        // E-Stop and safety from state
        if (state.safety) {
            this.setIOIndicator('io-estop', state.safety.estop, true);
        }
    },

    updateEncoderStatus(state) {
        const axes = ['x', 'y', 'z', 'a'];
        axes.forEach(axis => {
            const axisData = state.axis?.[axis];
            const errorEl = document.getElementById(`enc-${axis}-error`);
            const feedbackEl = document.getElementById(`enc-${axis}-feedback`);

            if (errorEl && axisData) {
                const error = axisData.following_error ?? axisData.vfd_error_percent ?? 0;
                const unit = axis === 'a' ? '°' : ' mm';
                errorEl.textContent = error.toFixed(3) + unit;

                // Color based on error magnitude
                if (Math.abs(error) > 0.5) {
                    errorEl.style.color = 'var(--color-critical)';
                } else if (Math.abs(error) > 0.1) {
                    errorEl.style.color = 'var(--color-warning)';
                } else {
                    errorEl.style.color = 'var(--color-optimal)';
                }
            }

            if (feedbackEl) {
                // Encoder feedback enabled status
                feedbackEl.textContent = 'Active';
                feedbackEl.style.color = 'var(--color-optimal)';
            }
        });
    },

    loadIOStatus() {
        // Skip in mock mode
        if (window.location.protocol === 'file:' || false) {
            return;
        }

        fetch('/api/io/status')
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    // Update input indicators
                    this.setIOIndicator('io-estop', data.estop, true);
                    this.setIOIndicator('io-door', data.door);
                    this.setIOIndicator('io-probe', data.probe);
                    this.setIOIndicator('io-limit-x', data.limit_x, true);
                    this.setIOIndicator('io-limit-y', data.limit_y, true);
                    this.setIOIndicator('io-limit-z', data.limit_z, true);

                    // Update output indicators
                    this.setIOIndicator('io-spindle', data.spindle_on);
                    this.setIOIndicator('io-coolant', data.coolant_on);
                    this.setIOIndicator('io-vacuum', data.vacuum_on);
                    this.setIOIndicator('io-alarm', data.alarm_on, true);
                }
            })
            .catch(err => {
                console.warn('[Diagnostics] Failed to load I/O status:', err);
            });
    },

    setIOIndicator(id, isActive, isAlarm = false) {
        const el = document.getElementById(id);
        if (!el) return;

        el.classList.remove('on', 'off', 'alarm');
        if (isActive) {
            el.classList.add(isAlarm ? 'alarm' : 'on');
        } else {
            el.classList.add('off');
        }
    },

    loadFaultLog() {
        // Skip in mock mode
        if (window.location.protocol === 'file:' || false) {
            return;
        }

        fetch('/api/faults')
            .then(r => r.json())
            .then(data => {
                this.updateFaultDisplay(data.faults || []);
            })
            .catch(err => {
                console.warn('[Diagnostics] Failed to load fault log:', err);
            });
    },

    updateFaultDisplay(faults) {
        const countEl = document.getElementById('fault-count');
        const listEl = document.getElementById('fault-list');

        if (countEl) {
            countEl.textContent = faults.length;
            countEl.classList.toggle('has-faults', faults.length > 0);
        }

        if (listEl) {
            if (faults.length === 0) {
                listEl.innerHTML = '<div class="fault-empty">No faults recorded</div>';
            } else {
                listEl.innerHTML = faults.map(fault => `
                    <div class="fault-item">
                        <div>
                            <span class="fault-code">0x${fault.code.toString(16).padStart(2, '0').toUpperCase()}</span>
                            <span class="fault-desc">${fault.description || 'Unknown fault'}</span>
                        </div>
                        <span class="fault-time">${this.formatFaultTime(fault.timestamp)}</span>
                    </div>
                `).join('');
            }
        }
    },

    formatFaultTime(timestamp) {
        if (!timestamp) return '--';
        const date = new Date(timestamp);
        return date.toLocaleTimeString();
    },

    clearFaults() {
        // Skip in mock mode
        if (window.location.protocol === 'file:' || false) {
            this.updateFaultDisplay([]);
            AlertManager.add('Faults cleared (mock)', 'success', 2000);
            return;
        }

        fetch('/api/faults/clear', { method: 'POST' })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    AlertManager.add('Fault log cleared', 'success', 2000);
                    this.updateFaultDisplay([]);
                } else {
                    AlertManager.add('Failed to clear faults', 'error');
                }
            })
            .catch(err => {
                console.error('[Diagnostics] Clear faults failed:', err);
                AlertManager.add('Failed to clear faults', 'error');
            });
    },

    simulateDiagnosticData() {
        // Simulate I/O states for mock mode
        this.setIOIndicator('io-estop', false);
        this.setIOIndicator('io-door', false);
        this.setIOIndicator('io-probe', false);
        this.setIOIndicator('io-limit-x', false);
        this.setIOIndicator('io-limit-y', false);
        this.setIOIndicator('io-limit-z', false);
        this.setIOIndicator('io-spindle', true);
        this.setIOIndicator('io-coolant', false);
        this.setIOIndicator('io-vacuum', false);
        this.setIOIndicator('io-alarm', false);

        // Simulate encoder feedback
        ['x', 'y', 'z', 'a'].forEach(axis => {
            const errorEl = document.getElementById(`enc-${axis}-error`);
            const feedbackEl = document.getElementById(`enc-${axis}-feedback`);
            const unit = axis === 'a' ? '°' : ' mm';
            if (errorEl) {
                errorEl.textContent = (Math.random() * 0.1 - 0.05).toFixed(3) + unit;
                errorEl.style.color = 'var(--color-optimal)';
            }
            if (feedbackEl) {
                feedbackEl.textContent = 'Active';
                feedbackEl.style.color = 'var(--color-optimal)';
            }
        });

        // No faults in mock
        this.updateFaultDisplay([]);
    },

    cleanup() {
        console.log('[Diagnostics] Cleaning up');
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }
    }
};

window.currentPageModule = DiagnosticsModule;

