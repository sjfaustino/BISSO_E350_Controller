/**
 * Diagnostics Page Module
 */
const DiagnosticsModule = {
    init() {
        console.log('[Diagnostics] Initializing');
        window.addEventListener('state-changed', () => this.onStateChanged());
    },

    onStateChanged() {
        const state = AppState.data;

        // Axis diagnostics (X, Y, Z)
        ['x', 'y', 'z'].forEach((axis, idx) => {
            const metrics = state.axis?.[axis];
            if (metrics) {
                document.getElementById(`diag-${axis}-quality`).textContent = metrics.quality || '--';
                document.getElementById(`diag-${axis}-jitter`).textContent =
                    ((metrics.jitter_mms || 0).toFixed(3)) + ' mm/s';
                document.getElementById(`diag-${axis}-stalls`).textContent = '--';
                document.getElementById(`diag-${axis}-active`).textContent = '--';
            }
        });

        // Encoder health
        if (state.encoders && state.encoders.length > 0) {
            this.updateEncoderHealth(state.encoders);
        }

        // VFD diagnostics
        if (state.vfd) {
            document.getElementById('diag-vfd-current').textContent =
                (state.vfd.current_amps || 0).toFixed(1) + ' A';
            document.getElementById('diag-vfd-freq').textContent =
                (state.vfd.frequency_hz || 0).toFixed(1) + ' Hz';
            document.getElementById('diag-vfd-thermal').textContent =
                (state.vfd.thermal_percent || 0) + '%';
            document.getElementById('diag-vfd-fault').textContent =
                '0x' + ((state.vfd.fault_code || 0).toString(16).padStart(4, '0')).toUpperCase();
        }
    },

    updateEncoderHealth(encoders) {
        const container = document.getElementById('encoder-health');
        const axisNames = ['X', 'Y', 'Z', 'A'];
        const healthStates = ['Optimal', 'Normal', 'Degraded', 'Critical'];

        container.innerHTML = encoders.map((enc, i) => `
            <div class="metric-card ${enc.health > 1 ? 'warning' : ''}">
                <div class="metric-label">Axis ${axisNames[i]}</div>
                <div class="metric-value">${healthStates[enc.health || 0]}</div>
            </div>
        `).join('');
    },

    cleanup() {
        console.log('[Diagnostics] Cleaning up');
    }
};

window.currentPageModule = DiagnosticsModule;
