/**
 * Diagnostics Page Module
 * Note: Use window.DiagnosticsModule to avoid "already declared" errors when navigating
 */
window.DiagnosticsModule = window.DiagnosticsModule || {
    updateInterval: null,
    trendInterval: null,
    spindleInterval: null,
    spindleData: [],
    spindlePaused: false,
    spindleMaxPoints: 120,  // 60 seconds at 500ms intervals

    // Helper to set N/A values with red styling
    setNA(element, suffix = '') {
        if (!element) return;
        element.innerHTML = '<span class="value-na">n/a</span>' + suffix;
    },

    // Helper to add/remove (N/A) in red to card titles
    updateHeaderNA(headerId, originalTitle, isPresent) {
        const header = document.getElementById(headerId);
        if (!header) return;
        if (isPresent) {
            header.innerHTML = originalTitle;
        } else {
            header.innerHTML = `${originalTitle} <span class="value-na" style="font-size: 0.8em; margin-left: 8px;">(N/A)</span>`;
        }
    },

    init() {
        console.log('[Diagnostics] Initializing');
        window.addEventListener('state-changed', () => this.onStateChanged());
        this.setupEventListeners();
        this.loadInitialData();

        // Start periodic updates for I/O and faults
        this.updateInterval = setInterval(() => this.loadIOStatus(), 2000);

        // Start trend chart updates
        this.loadTrendData();
        this.trendInterval = setInterval(() => this.loadTrendData(), 5000);

        // Start spindle current graph updates (500ms for smooth real-time)
        this.loadSpindleData();
        this.spindleInterval = setInterval(() => this.loadSpindleData(), 500);

        // Start I/O Diagnostics updates
        this.loadIODiagnostics();
        this.ioDiagInterval = setInterval(() => this.loadIODiagnostics(), 2000);
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

        // Trend refresh button
        document.getElementById('refresh-trends-btn')?.addEventListener('click', () => {
            this.loadTrendData();
        });

        // I/O Diagnostics refresh button
        document.getElementById('refresh-io-diag-btn')?.addEventListener('click', () => {
            this.loadIODiagnostics();
        });

        // Spindle graph pause button
        document.getElementById('spindle-graph-pause')?.addEventListener('click', () => {
            this.spindlePaused = !this.spindlePaused;
            const btn = document.getElementById('spindle-graph-pause');
            if (btn) btn.textContent = this.spindlePaused ? 'Resume' : 'Pause';
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
            const connected = state.motion?.dro_connected ?? false;

            // Update Header (N/A)
            const axisTitle = 'Axis ' + axis.toUpperCase() + ' Diagnostics';
            this.updateHeaderNA(`diag-header-axis-${axis}`, axisTitle, connected);
            if (axis === 'x') this.updateHeaderNA('diag-header-encoder', 'Encoder Status', connected);

            const qualityEl = document.getElementById(`diag-${axis}-quality`);
            const jitterEl = document.getElementById(`diag-${axis}-jitter`);
            const stallsEl = document.getElementById(`diag-${axis}-stalls`);
            const activeEl = document.getElementById(`diag-${axis}-active`);

            if (connected && metrics) {
                if (qualityEl) qualityEl.textContent = (metrics.quality || 0).toFixed(0);
                if (jitterEl) jitterEl.textContent = (metrics.jitter_mms || 0).toFixed(3) + ' mm/s';
                if (stallsEl) stallsEl.textContent = metrics.stalled ? '⚠️ STALL' : '0';
                if (activeEl) activeEl.textContent = '--';
            } else {
                this.setNA(qualityEl);
                this.setNA(jitterEl, ' mm/s');
                this.setNA(stallsEl);
                if (activeEl) this.setNA(activeEl); // Also set active to N/A if not connected/metrics
            }
        });

        // Encoder status from state
        this.updateEncoderStatus(state);

        // VFD diagnostics
        if (state.vfd) {
            // Update VFD Headers (N/A)
            this.updateHeaderNA('diag-header-vfd', 'VFD Diagnostics', state.vfd.connected);
            this.updateHeaderNA('diag-header-spindle-rt', '🪚 Saw Blade Current (Real-time)', state.vfd.connected);
            this.updateHeaderNA('diag-header-spindle-trend', 'Spindle Current (A)', state.vfd.connected);

            const currentEl = document.getElementById('diag-vfd-current');
            const freqEl = document.getElementById('diag-vfd-freq');
            const thermalEl = document.getElementById('diag-vfd-thermal');
            const faultEl = document.getElementById('diag-vfd-fault');

            if (state.vfd.connected) {
                if (currentEl) currentEl.textContent = (state.vfd.current_amps || 0).toFixed(1) + ' A';
                if (freqEl) freqEl.textContent = (state.vfd.frequency_hz || 0).toFixed(1) + ' Hz';
                if (thermalEl) thermalEl.textContent = (state.vfd.thermal_percent || 0) + '%';
                if (faultEl) faultEl.textContent = '0x' + ((state.vfd.fault_code || 0).toString(16).padStart(4, '0')).toUpperCase();
            } else {
                if (currentEl) currentEl.textContent = 'N/A';
                if (freqEl) freqEl.textContent = 'N/A';
                if (thermalEl) thermalEl.textContent = 'N/A';
                if (faultEl) faultEl.textContent = 'N/A';
            }
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

    // Trend chart functions
    loadTrendData() {
        if (window.location.protocol === 'file:') return;

        fetch('/api/history/telemetry')
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    this.renderTrendCharts(data);
                }
            })
            .catch(err => {
                console.warn('[Diagnostics] Failed to load trend data:', err);
            });
    },

    renderTrendCharts(data) {
        // CPU Chart
        if (data.cpu && data.cpu.length > 0) {
            this.drawLineChart('chart-cpu', data.cpu, 0, 100, '#3b82f6');
            const current = data.cpu[data.cpu.length - 1];
            const avg = data.cpu.reduce((a, b) => a + b, 0) / data.cpu.length;
            const max = Math.max(...data.cpu);
            document.getElementById('diag-cpu-current').textContent = current;
            document.getElementById('diag-cpu-avg').textContent = avg.toFixed(0);
            document.getElementById('diag-cpu-max').textContent = max;
        } else {
            this.setNA(document.getElementById('diag-cpu-current'));
            this.setNA(document.getElementById('diag-cpu-avg'));
            this.setNA(document.getElementById('diag-cpu-max'));
        }

        // Memory Chart (convert bytes to KB)
        if (data.heap && data.heap.length > 0) {
            const heapKB = data.heap.map(v => v / 1024);
            const minHeap = Math.min(...heapKB);
            const maxHeap = Math.max(...heapKB);
            this.drawLineChart('chart-memory', heapKB, minHeap * 0.9, maxHeap * 1.1, '#10b981');
            const current = heapKB[heapKB.length - 1];
            document.getElementById('diag-mem-current').textContent = current.toFixed(0);
            document.getElementById('diag-mem-min').textContent = minHeap.toFixed(0);
        } else {
            this.setNA(document.getElementById('diag-mem-current'));
            this.setNA(document.getElementById('diag-mem-min'));
        }

        // Spindle Current Chart
        const vfdConnected = AppState.data.vfd?.connected;
        if (vfdConnected && data.spindle_amps && data.spindle_amps.length > 0) {
            const maxAmps = Math.max(...data.spindle_amps, 5);
            this.drawLineChart('chart-spindle', data.spindle_amps, 0, maxAmps * 1.2, '#f59e0b');
            const current = data.spindle_amps[data.spindle_amps.length - 1];
            const avg = data.spindle_amps.reduce((a, b) => a + b, 0) / data.spindle_amps.length;
            const max = Math.max(...data.spindle_amps);
            document.getElementById('diag-spindle-current').textContent = current.toFixed(1);
            document.getElementById('diag-spindle-avg').textContent = avg.toFixed(1);
            document.getElementById('diag-spindle-max').textContent = max.toFixed(1);
        } else {
            this.setNA(document.getElementById('diag-spindle-current'));
            this.setNA(document.getElementById('diag-spindle-avg'));
            this.setNA(document.getElementById('diag-spindle-max'));
            // Clear chart if disconnected
            const canvas = document.getElementById('chart-spindle');
            if (canvas) {
                const ctx = canvas.getContext('2d');
                ctx.clearRect(0, 0, canvas.width, canvas.height);
            }
        }
    },

    drawLineChart(canvasId, data, minVal, maxVal, color) {
        const canvas = document.getElementById(canvasId);
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        const width = canvas.width;
        const height = canvas.height;
        const padding = 5;

        // Clear canvas
        ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--bg-tertiary') || '#1e293b';
        ctx.fillRect(0, 0, width, height);

        if (data.length < 2) return;

        // Draw line
        ctx.beginPath();
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;

        const range = maxVal - minVal || 1;
        for (let i = 0; i < data.length; i++) {
            const x = padding + (i / (data.length - 1)) * (width - 2 * padding);
            const y = height - padding - ((data[i] - minVal) / range) * (height - 2 * padding);

            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        }
        ctx.stroke();

        // Draw gradient fill
        ctx.lineTo(width - padding, height - padding);
        ctx.lineTo(padding, height - padding);
        ctx.closePath();
        const gradient = ctx.createLinearGradient(0, 0, 0, height);
        gradient.addColorStop(0, color + '40');
        gradient.addColorStop(1, color + '00');
        ctx.fillStyle = gradient;
        ctx.fill();
    },

    // Spindle current real-time chart
    loadSpindleData() {
        if (this.spindlePaused) return;

        fetch('/api/spindle')
            .then(response => response.json())
            .then(data => {
                // Add new data point
                this.spindleData.push({
                    time: Date.now(),
                    current: data.current_amps || 0,
                    peak: data.peak_amps || 0,
                    threshold: data.threshold_amps || 30,
                    autoPauseThreshold: data.auto_pause_threshold || 25,
                    autoPauseCount: data.auto_pause_count || 0,
                    overcurrent: data.overcurrent || false
                });

                // Keep buffer size limited
                while (this.spindleData.length > this.spindleMaxPoints) {
                    this.spindleData.shift();
                }

                // Update stats display
                const latest = this.spindleData[this.spindleData.length - 1];
                document.getElementById('spindle-current-now').textContent = latest.current.toFixed(1) + ' A';
                document.getElementById('spindle-current-peak').textContent = latest.peak.toFixed(1) + ' A';
                document.getElementById('spindle-threshold-pause').textContent = latest.autoPauseThreshold + ' A';
                document.getElementById('spindle-threshold-estop').textContent = latest.threshold + ' A';
                document.getElementById('spindle-pause-count').textContent = latest.autoPauseCount;

                // Update status badge
                const badge = document.getElementById('spindle-status-badge');
                if (badge) {
                    if (latest.overcurrent || latest.current > latest.threshold) {
                        badge.className = 'badge badge-danger';
                        badge.textContent = 'OVERLOAD';
                    } else if (latest.current > latest.autoPauseThreshold) {
                        badge.className = 'badge badge-warning';
                        badge.textContent = 'HIGH';
                    } else {
                        badge.className = 'badge badge-success';
                        badge.textContent = 'OK';
                    }
                }

                // Draw chart
                this.drawSpindleChart();
            })
            .catch(err => console.error('[Diagnostics] Spindle fetch error:', err));
    },

    drawSpindleChart() {
        const canvas = document.getElementById('spindle-current-chart');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        const w = canvas.width;
        const h = canvas.height;
        const data = this.spindleData;

        // Get thresholds
        const pauseThreshold = data.length > 0 ? data[data.length - 1].autoPauseThreshold : 25;
        const estopThreshold = data.length > 0 ? data[data.length - 1].threshold : 30;
        const maxY = Math.max(estopThreshold * 1.2, 35);

        // Clear
        ctx.fillStyle = '#1a1a2e';
        ctx.fillRect(0, 0, w, h);

        // Draw grid lines
        ctx.strokeStyle = '#333';
        ctx.lineWidth = 0.5;
        for (let i = 0; i <= 5; i++) {
            const y = h - (i / 5) * h;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(w, y);
            ctx.stroke();

            // Y-axis labels
            ctx.fillStyle = '#888';
            ctx.font = '10px sans-serif';
            ctx.fillText((maxY * i / 5).toFixed(0) + 'A', 5, y - 2);
        }

        // Draw pause threshold line (orange dashed)
        const pauseY = h - (pauseThreshold / maxY) * h;
        ctx.strokeStyle = '#ff9800';
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 5]);
        ctx.beginPath();
        ctx.moveTo(0, pauseY);
        ctx.lineTo(w, pauseY);
        ctx.stroke();

        // Draw E-Stop threshold line (red dashed)
        const estopY = h - (estopThreshold / maxY) * h;
        ctx.strokeStyle = '#f44336';
        ctx.beginPath();
        ctx.moveTo(0, estopY);
        ctx.lineTo(w, estopY);
        ctx.stroke();
        ctx.setLineDash([]);

        // Draw current line
        if (data.length < 2) return;

        ctx.strokeStyle = '#4ade80';
        ctx.lineWidth = 2;
        ctx.beginPath();

        for (let i = 0; i < data.length; i++) {
            const x = (i / (this.spindleMaxPoints - 1)) * w;
            const y = h - (data[i].current / maxY) * h;

            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        }
        ctx.stroke();

        // Fill area under curve
        ctx.lineTo(w, h);
        ctx.lineTo(0, h);
        ctx.closePath();
        ctx.fillStyle = 'rgba(74, 222, 128, 0.2)';
        ctx.fill();
    },

    cleanup() {
        console.log('[Diagnostics] Cleaning up');
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }
        if (this.trendInterval) {
            clearInterval(this.trendInterval);
            this.trendInterval = null;
        }
        if (this.spindleInterval) {
            clearInterval(this.spindleInterval);
            this.spindleInterval = null;
        }
        this.spindleData = [];

        if (this.ioDiagInterval) {
            clearInterval(this.ioDiagInterval);
            this.ioDiagInterval = null;
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
                        <span>X${i}</span>
                    </div>`;
            }
        }

        if (outputGrid && outputGrid.children.length === 0) {
            for (let i = 1; i <= 16; i++) {
                outputGrid.innerHTML += `
                    <div class="io-diag-pin">
                        <div class="pin-led off" id="io-out-${i}"></div>
                        <span>Y${i}</span>
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
            .catch(err => console.warn('[Diagnostics] Failed to load I/O diagnostics:', err));
    }
};

window.currentPageModule = DiagnosticsModule;

