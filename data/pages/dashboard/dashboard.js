/**
 * Dashboard Page Module
 * Note: Use window.DashboardModule to avoid "already declared" errors when navigating
 */
window.DashboardModule = window.DashboardModule || {
    history: { cpu: [], memory: [], spindle: [], temperature: [], latency: [], motion: [], timestamps: [] },
    maxHistory: 300,
    currentTimeRange: 300000, // 5 minutes
    chart: null,
    graphs: {}, // Store GraphVisualizer instances
    lastTelemetry: null, // Store last telemetry for VFD/spindle data
    stateChangeHandler: null, // Store bound event handler for cleanup
    updateInterval: null, // Store interval ID for cleanup
    historicalDataLoaded: false, // Track if historical data has been bulk-loaded

    init() {
        console.log('[Dashboard] Initializing');
        this.setupEventListeners();
        this.initializeGraphs();
        this.historicalDataLoaded = false; // Reset on init

        // Store bound handler so we can remove it later
        this.stateChangeHandler = () => this.onStateChanged();
        window.addEventListener('state-changed', this.stateChangeHandler);

        // Store interval ID for cleanup
        this.updateInterval = setInterval(() => {
            this.onStateChanged();
            this.updateGraphs();
        }, 1000);
    },

    setupEventListeners() {
        // Card toggles
        document.querySelectorAll('.card-toggle').forEach(btn => {
            btn.addEventListener('click', () => {
                const content = btn.closest('.card').querySelector('.card-content');
                content.classList.toggle('collapsed');
                btn.textContent = content.classList.contains('collapsed') ? '+' : '−';
            });
        });

        // Time range selector for advanced graphs
        const timeRangeSelect = document.getElementById('graph-time-range');
        if (timeRangeSelect) {
            timeRangeSelect.addEventListener('change', (e) => {
                this.currentTimeRange = parseInt(e.target.value) * 1000;
                console.log('[Dashboard] Time range changed to', this.currentTimeRange / 1000, 'seconds');
            });
        }

        // Export graphs button
        const exportBtn = document.getElementById('export-graphs-btn');
        if (exportBtn) {
            exportBtn.addEventListener('click', () => this.exportGraphsData());
        }

        // Time range selector
        document.querySelectorAll('.time-btn').forEach(btn => {
            btn.addEventListener('click', () => {
                document.querySelectorAll('.time-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                this.currentTimeRange = parseInt(btn.dataset.range);
                this.drawChart();
            });
        });

        // Work Coordinate Toggle (Machine/Work)
        const coordModeBtn = document.getElementById('dro-coord-mode');
        const workOffsetSelect = document.getElementById('dro-work-offset');

        if (coordModeBtn) {
            // Initialize state
            this.workCoordMode = localStorage.getItem('droCoordMode') || 'machine';
            this.workOffset = localStorage.getItem('droWorkOffset') || 'G54';
            this.updateCoordModeUI();

            coordModeBtn.addEventListener('click', () => {
                this.workCoordMode = this.workCoordMode === 'machine' ? 'work' : 'machine';
                localStorage.setItem('droCoordMode', this.workCoordMode);
                this.updateCoordModeUI();
                this.onStateChanged(); // Refresh DRO display
            });
        }

        if (workOffsetSelect) {
            workOffsetSelect.value = this.workOffset || 'G54';
            workOffsetSelect.addEventListener('change', (e) => {
                this.workOffset = e.target.value;
                localStorage.setItem('droWorkOffset', this.workOffset);
                this.onStateChanged(); // Refresh DRO display
            });
        }
    },

    // Update Work Coordinate Mode UI
    updateCoordModeUI() {
        const modeBtn = document.getElementById('dro-coord-mode');
        const modeText = document.getElementById('dro-mode-text');
        const offsetSelect = document.getElementById('dro-work-offset');

        if (modeText) {
            modeText.textContent = this.workCoordMode === 'machine' ? 'Machine' : 'Work';
        }
        if (modeBtn) {
            modeBtn.classList.toggle('work-mode', this.workCoordMode === 'work');
        }
        if (offsetSelect) {
            offsetSelect.disabled = this.workCoordMode === 'machine';
        }
    },

    initializeGraphs() {
        // CPU Graph
        if (document.getElementById('cpu-graph')) {
            try {
                this.graphs.cpu = new GraphVisualizer('cpu-graph', {
                    title: 'CPU Usage (%)',
                    yMin: 0,
                    yMax: 100,
                    unit: '%',
                    timeWindow: this.currentTimeRange
                });
                this.graphs.cpu.addSeries('CPU', '#10b981');
            } catch (e) { console.warn('CPU graph init failed:', e); }
        }

        // Memory Graph
        if (document.getElementById('memory-graph')) {
            try {
                this.graphs.memory = new GraphVisualizer('memory-graph', {
                    title: 'Free Memory (KB)',
                    yMin: 0,
                    yMax: 360000,
                    unit: ' KB',
                    timeWindow: this.currentTimeRange
                });
                this.graphs.memory.addSeries('Memory', '#3b82f6');
            } catch (e) { console.warn('Memory graph init failed:', e); }
        }

        // Spindle Current Graph
        if (document.getElementById('spindle-graph')) {
            try {
                this.graphs.spindle = new GraphVisualizer('spindle-graph', {
                    title: 'Spindle Current (A)',
                    yMin: 0,
                    yMax: 20,
                    unit: ' A',
                    timeWindow: this.currentTimeRange
                });
                this.graphs.spindle.addSeries('Current', '#f59e0b');
            } catch (e) { console.warn('Spindle graph init failed:', e); }
        }

        // Temperature Graph
        if (document.getElementById('temperature-graph')) {
            try {
                this.graphs.temperature = new GraphVisualizer('temperature-graph', {
                    title: 'System Temperature (°C)',
                    yMin: 20,
                    yMax: 80,
                    unit: ' °C',
                    timeWindow: this.currentTimeRange
                });
                this.graphs.temperature.addSeries('Temp', '#ef4444');
            } catch (e) { console.warn('Temperature graph init failed:', e); }
        }

        // Latency Graph
        if (document.getElementById('latency-graph')) {
            try {
                this.graphs.latency = new GraphVisualizer('latency-graph', {
                    title: 'WebSocket Latency (ms)',
                    yMin: 0,
                    yMax: 100,
                    unit: ' ms',
                    timeWindow: this.currentTimeRange
                });
                this.graphs.latency.addSeries('Latency', '#8b5cf6');
            } catch (e) { console.warn('Latency graph init failed:', e); }
        }

        // Motion Load Graph
        if (document.getElementById('motion-load-graph')) {
            try {
                this.graphs.motion = new GraphVisualizer('motion-load-graph', {
                    title: 'Motion System Load',
                    yMin: 0,
                    yMax: 100,
                    unit: '%',
                    timeWindow: this.currentTimeRange
                });
                this.graphs.motion.addSeries('Quality', '#10b981');
                this.graphs.motion.addSeries('Jitter', '#f59e0b');
            } catch (e) { console.warn('Motion graph init failed:', e); }
        }

        console.log('[Dashboard] Graphs initialized');
    },



    onStateChanged() {
        const state = AppState.data;

        // Store last telemetry for VFD/spindle data
        this.lastTelemetry = state;

        // System metrics
        if (state.system) {
            const cpu = state.system.cpu_percent || 0;
            const mem = state.system.free_heap_bytes || 0;
            const health = state.system.health || 'UNKNOWN';
            const status = state.system.status || 'IDLE';

            // System Health Card
            const healthValueEl = document.getElementById('health-value');
            if (healthValueEl) {
                healthValueEl.textContent = health;
                healthValueEl.className = 'card-value ' + health.toLowerCase();
            }
            const healthDetailEl = document.getElementById('health-detail');
            if (healthDetailEl) healthDetailEl.textContent = 'Status: ' + status;

            const healthBar = document.getElementById('health-bar');
            if (healthBar) {
                healthBar.className = 'progress-fill ' + health.toLowerCase();
            }

            // CPU Usage Card
            const cpuValueEl = document.getElementById('cpu-value');
            if (cpuValueEl) cpuValueEl.textContent = cpu + '%';

            const cpuBar = document.getElementById('cpu-bar');
            if (cpuBar) {
                cpuBar.style.width = cpu + '%';
                cpuBar.className = 'progress-fill';
                if (cpu > 85) cpuBar.classList.add('warning');
                if (cpu > 95) cpuBar.classList.add('critical');
            }

            // Memory Card
            const memValueEl = document.getElementById('mem-value');
            if (memValueEl) memValueEl.textContent = (mem / 1024).toFixed(0) + ' KB';

            const memBar = document.getElementById('mem-bar');
            if (memBar) {
                // Assuming 320KB internal heap for scaling (typical ESP32)
                const totalHeap = 320000;
                const percent = Math.min(100, (mem / totalHeap) * 100);
                memBar.style.width = percent + '%';
            }
        }

        // Motion status
        if (state.motion) {
            const status = state.motion.moving ? '🔄 Moving' : '⏸️ Stopped';
            const motionStatusEl = document.getElementById('motion-status');
            if (motionStatusEl) motionStatusEl.textContent = status;
        }

        // Safety status
        if (state.safety) {
            let safetyText = '✓ OK';
            if (state.safety.estop) safetyText = '🛑 E-STOP';
            else if (state.safety.alarm) safetyText = '⚠️ ALARM';
            const safetyStatusEl = document.getElementById('safety-status');
            if (safetyStatusEl) safetyStatusEl.textContent = safetyText;
        }

        // VFD status
        if (state.vfd) {
            const motorStatus = state.vfd.frequency_hz > 0.5 ? 'RUNNING' : 'IDLE';
            const vfdStatusEl = document.getElementById('vfd-status');
            if (vfdStatusEl) vfdStatusEl.textContent = motorStatus;

            const vfdFreqEl = document.getElementById('vfd-freq');
            if (vfdFreqEl) vfdFreqEl.textContent = state.vfd.frequency_hz.toFixed(1) + ' Hz';

            const vfdCurrentEl = document.getElementById('vfd-current');
            if (vfdCurrentEl) vfdCurrentEl.textContent = state.vfd.current_amps.toFixed(1) + ' A';
        }

        // Network status
        if (state.network) {
            const wifiSignalEl = document.getElementById('wifi-signal');
            if (wifiSignalEl) wifiSignalEl.textContent = state.network.signal_percent + '%';

            const wifiBarEl = document.getElementById('wifi-bar');
            if (wifiBarEl) wifiBarEl.style.width = state.network.signal_percent + '%';

            const wifiStatusEl = document.getElementById('wifi-status');
            if (wifiStatusEl) wifiStatusEl.textContent = state.network.wifi_connected ? '✓ Connected' : '✗ Disconnected';
        }

        // Axis metrics
        if (state.axis) {
            this.updateAxisCard('x', state.axis.x);
            this.updateAxisCard('y', state.axis.y);
            this.updateAxisCard('z', state.axis.z);

            // Update DRO (Digital Readout) display
            this.updateDRO(state.axis);
        }

        // Update history for chart
        this.history.cpu.push(state.system?.cpu_percent || 0);
        this.history.memory.push((state.system?.free_heap_bytes || 0) / 1024);
        this.history.spindle.push(state.vfd?.current_amps || 0);
        this.history.temperature.push(state.system?.temperature || 25);
        this.history.latency.push(state.network?.latency || 0);

        // Motion load from average axis quality
        let avgQuality = 80;
        if (state.axis) {
            const qualities = [];
            if (state.axis.x?.quality) qualities.push(state.axis.x.quality);
            if (state.axis.y?.quality) qualities.push(state.axis.y.quality);
            if (state.axis.z?.quality) qualities.push(state.axis.z.quality);
            if (qualities.length > 0) {
                avgQuality = qualities.reduce((a, b) => a + b, 0) / qualities.length;
            }
        }

        let avgJitter = 0.5;
        if (state.axis) {
            const jitters = [];
            if (state.axis.x?.jitter_mms) jitters.push(state.axis.x.jitter_mms);
            if (state.axis.y?.jitter_mms) jitters.push(state.axis.y.jitter_mms);
            if (state.axis.z?.jitter_mms) jitters.push(state.axis.z.jitter_mms);
            if (jitters.length > 0) {
                avgJitter = jitters.reduce((a, b) => a + b, 0) / jitters.length;
            }
        }

        this.history.motion.push({ quality: avgQuality, jitter: avgJitter });
        this.history.timestamps.push(Date.now());

        if (this.history.cpu.length > this.maxHistory) {
            this.history.cpu.shift();
            this.history.memory.shift();
            this.history.spindle.shift();
            this.history.temperature.shift();
            this.history.latency.shift();
            this.history.motion.shift();
            this.history.timestamps.shift();
        }

        this.drawChart();
    },

    updateAxisCard(axis, metrics) {
        if (!metrics) return;

        const prefix = `axis-${axis}`;

        const qualityEl = document.getElementById(`${prefix}-quality`);
        if (qualityEl) qualityEl.textContent = metrics.quality || 0;

        const barEl = document.getElementById(`${prefix}-bar`);
        if (barEl) barEl.style.width = (metrics.quality || 0) + '%';

        const jitterEl = document.getElementById(`${prefix}-jitter`);
        if (jitterEl) jitterEl.textContent = (metrics.jitter_mms || 0).toFixed(3) + ' mm/s';

        const errorEl = document.getElementById(`${prefix}-error`);
        if (errorEl) errorEl.textContent = (metrics.vfd_error_percent || 0).toFixed(1) + '%';

        const stalledEl = document.getElementById(`${prefix}-stalled`);
        if (stalledEl) {
            if (metrics.stalled) {
                stalledEl.textContent = '⚠️ STALLED';
                stalledEl.style.color = 'var(--color-critical)';
            } else {
                stalledEl.textContent = '✓ OK';
                stalledEl.style.color = 'var(--color-optimal)';
            }
        }
    },

    updateDRO(axisData) {
        const axes = ['x', 'y', 'z', 'a'];

        axes.forEach(axis => {
            const el = document.getElementById(`dro-${axis}`);
            if (el) {
                // Get position from axis data (position_mm or position field)
                const pos = axisData[axis]?.position_mm ?? axisData[axis]?.position ?? 0;
                el.textContent = pos.toFixed(3);

                // Add negative class for styling
                el.classList.toggle('negative', pos < 0);
            }
        });

        // Update live status indicator
        const statusEl = document.getElementById('dro-status');
        if (statusEl) {
            statusEl.textContent = 'Live';
            statusEl.classList.remove('offline');
        }
    },

    drawChart() {
        const canvas = document.getElementById('trendsChart');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        const rect = canvas.getBoundingClientRect();
        canvas.width = rect.width;
        canvas.height = rect.height;

        const padding = 40;
        const width = canvas.width - padding * 2;
        const height = canvas.height - padding * 2;

        // Clear
        ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--bg-primary').trim() || '#fff';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Get visible data
        const sampleSize = Math.min(this.history.cpu.length, this.currentTimeRange * 60);
        const visibleCpu = this.history.cpu.slice(-sampleSize);
        const visibleMem = this.history.memory.slice(-sampleSize);
        const visibleSpindle = this.history.spindle.slice(-sampleSize);

        // Draw lines
        const drawLine = (data, color, scale) => {
            ctx.strokeStyle = color;
            ctx.lineWidth = 2;
            ctx.beginPath();

            for (let i = 0; i < data.length; i++) {
                const value = Math.min(100, data[i] / scale);
                const x = padding + (i / Math.max(1, data.length - 1)) * width;
                const y = canvas.height - padding - (value / 100) * height;

                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();
        };

        const cpuColor = getComputedStyle(document.documentElement).getPropertyValue('--chart-cpu').trim();
        const memColor = getComputedStyle(document.documentElement).getPropertyValue('--chart-mem').trim();
        const spindleColor = getComputedStyle(document.documentElement).getPropertyValue('--chart-spindle').trim();

        drawLine(visibleCpu, cpuColor, 1);
        drawLine(visibleMem, memColor, 1000);
        drawLine(visibleSpindle, spindleColor, 25);

        // Draw axes
        ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--text-primary').trim();
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(padding, padding);
        ctx.lineTo(padding, canvas.height - padding);
        ctx.lineTo(canvas.width - padding, canvas.height - padding);
        ctx.stroke();
    },

    updateGraphs() {
        // Detect if we're using MiniChart (single-arg addDataPoint) or GraphVisualizer (two-arg)
        const isMiniChart = this.graphs.cpu && this.graphs.cpu.constructor.name === 'MiniChart';

        // If using MiniCharts and we have historical data that hasn't been loaded yet, bulk-load it
        if (isMiniChart && !this.historicalDataLoaded && this.history.cpu.length > 1) {
            console.log('[Dashboard] Bulk-loading', this.history.cpu.length, 'historical data points to charts');

            // Bulk load all historical data points
            for (let i = 0; i < this.history.cpu.length; i++) {
                if (this.history.cpu[i] !== undefined) {
                    this.graphs.cpu?.addDataPoint(this.history.cpu[i]);
                }
                if (this.history.memory[i] !== undefined) {
                    this.graphs.memory?.addDataPoint(this.history.memory[i]);
                }
                if (this.history.spindle[i] !== undefined) {
                    this.graphs.spindle?.addDataPoint(this.history.spindle[i]);
                }
                if (this.history.temperature[i] !== undefined) {
                    this.graphs.temperature?.addDataPoint(this.history.temperature[i]);
                }
            }

            this.historicalDataLoaded = true;
            console.log('[Dashboard] Historical data bulk-load complete');
            return; // Don't add the last point again
        }

        // Add data points to graphs from history
        if (this.history.cpu.length > 0) {
            const cpu = this.history.cpu[this.history.cpu.length - 1];

            // MiniChart uses addDataPoint(value), GraphVisualizer uses addDataPoint(series, value)
            if (isMiniChart) {
                this.graphs.cpu?.addDataPoint(cpu);
            } else {
                this.graphs.cpu?.addDataPoint('CPU', cpu);
            }

            // Update CPU stats (only if elements exist and using GraphVisualizer)
            if (!isMiniChart) {
                const cpuStats = this.graphs.cpu?.getStats('CPU');
                if (cpuStats) {
                    const cpuCurrentEl = document.getElementById('cpu-current');
                    const cpuAvgEl = document.getElementById('cpu-stat-avg');
                    const cpuMaxEl = document.getElementById('cpu-stat-max');

                    if (cpuCurrentEl) cpuCurrentEl.textContent = cpu.toFixed(1) + '%';
                    if (cpuAvgEl) cpuAvgEl.textContent = cpuStats.avg.toFixed(1) + '%';
                    if (cpuMaxEl) cpuMaxEl.textContent = cpuStats.max.toFixed(1) + '%';
                }
            }
        }

        if (this.history.memory.length > 0) {
            const mem = this.history.memory[this.history.memory.length - 1];

            if (isMiniChart) {
                this.graphs.memory?.addDataPoint(mem);
            } else {
                this.graphs.memory?.addDataPoint('Memory', mem);
            }

            if (!isMiniChart) {
                const memStats = this.graphs.memory?.getStats('Memory');
                if (memStats) {
                    const memCurrentEl = document.getElementById('mem-current');
                    const memAvgEl = document.getElementById('mem-stat-avg');
                    const memMaxEl = document.getElementById('mem-stat-max');

                    if (memCurrentEl) memCurrentEl.textContent = mem.toFixed(0) + ' KB';
                    if (memAvgEl) memAvgEl.textContent = memStats.avg.toFixed(0) + ' KB';
                    if (memMaxEl) memMaxEl.textContent = memStats.max.toFixed(0) + ' KB';
                }
            }
        }

        if (this.history.spindle.length > 0) {
            const spindle = this.history.spindle[this.history.spindle.length - 1];

            if (isMiniChart) {
                this.graphs.spindle?.addDataPoint(spindle);
            } else {
                this.graphs.spindle?.addDataPoint('Current', spindle);
            }

            if (!isMiniChart) {
                const spindleStats = this.graphs.spindle?.getStats('Current');
                if (spindleStats) {
                    const spindleCurrentEl = document.getElementById('spindle-current');
                    if (spindleCurrentEl) spindleCurrentEl.textContent = spindle.toFixed(2) + ' A';

                    // Update spindle card progress bar and additional metrics
                    const spindleBar = document.getElementById('spindle-bar');
                    const spindleFreq = document.getElementById('spindle-freq');
                    const spindleThermal = document.getElementById('spindle-thermal');

                    if (spindleBar) {
                        // Progress bar shows current as percentage of threshold (assume 30A max)
                        const threshold = 30.0;
                        const percent = Math.min((spindle / threshold) * 100, 100);
                        spindleBar.style.width = percent + '%';

                        // Color based on load
                        if (percent > 80) {
                            spindleBar.style.background = 'var(--color-critical)';
                        } else if (percent > 60) {
                            spindleBar.style.background = 'var(--color-warning)';
                        } else {
                            spindleBar.style.background = 'var(--color-optimal)';
                        }
                    }

                    // Update frequency and thermal from last telemetry
                    if (this.lastTelemetry && this.lastTelemetry.vfd) {
                        if (spindleFreq) {
                            spindleFreq.textContent = this.lastTelemetry.vfd.frequency_hz.toFixed(1) + ' Hz';
                        }
                        if (spindleThermal) {
                            spindleThermal.textContent = (this.lastTelemetry.vfd.thermal_percent || 0) + '%';
                        }
                    }

                    // Legacy stat fields (if they exist)
                    const statAvg = document.getElementById('spindle-stat-avg');
                    const statMax = document.getElementById('spindle-stat-max');
                    if (statAvg) statAvg.textContent = spindleStats.avg.toFixed(2) + ' A';
                    if (statMax) statMax.textContent = spindleStats.max.toFixed(2) + ' A';
                }
            }
        }

        if (this.history.temperature.length > 0) {
            const temp = this.history.temperature[this.history.temperature.length - 1];

            if (isMiniChart) {
                this.graphs.temperature?.addDataPoint(temp);
            } else {
                this.graphs.temperature?.addDataPoint('Temp', temp);
            }

            if (!isMiniChart) {
                const tempStats = this.graphs.temperature?.getStats('Temp');
                if (tempStats) {
                    const tempCurrentEl = document.getElementById('temp-current');
                    const tempAvgEl = document.getElementById('temp-stat-avg');
                    const tempMaxEl = document.getElementById('temp-stat-max');

                    if (tempCurrentEl) tempCurrentEl.textContent = temp.toFixed(1) + ' °C';
                    if (tempAvgEl) tempAvgEl.textContent = tempStats.avg.toFixed(1) + ' °C';
                    if (tempMaxEl) tempMaxEl.textContent = tempStats.max.toFixed(1) + ' °C';
                }
            }
        }

        if (this.history.latency.length > 0 && !isMiniChart) {
            const latency = this.history.latency[this.history.latency.length - 1];
            this.graphs.latency?.addDataPoint('Latency', latency);

            const latencyStats = this.graphs.latency?.getStats('Latency');
            if (latencyStats) {
                const latencyCurrentEl = document.getElementById('latency-current');
                const latencyAvgEl = document.getElementById('latency-stat-avg');
                const latencyMaxEl = document.getElementById('latency-stat-max');

                if (latencyCurrentEl) latencyCurrentEl.textContent = latency.toFixed(0) + ' ms';
                if (latencyAvgEl) latencyAvgEl.textContent = latencyStats.avg.toFixed(0) + ' ms';
                if (latencyMaxEl) latencyMaxEl.textContent = latencyStats.max.toFixed(0) + ' ms';
            }
        }

        if (this.history.motion.length > 0 && !isMiniChart) {
            const motion = this.history.motion[this.history.motion.length - 1];
            this.graphs.motion?.addDataPoint('Quality', motion.quality || 80);
            this.graphs.motion?.addDataPoint('Jitter', motion.jitter || 0.5);

            const motionQualityEl = document.getElementById('motion-quality');
            const motionJitterEl = document.getElementById('motion-jitter');

            if (motionQualityEl) motionQualityEl.textContent = (motion.quality || 80).toFixed(0) + '%';
            if (motionJitterEl) motionJitterEl.textContent = (motion.jitter || 0.5).toFixed(2) + ' mm/s';
        }
    },

    exportGraphsData() {
        const csv = 'Graph Data Export\n' +
            'Generated: ' + new Date().toLocaleString() + '\n\n';

        // Collect all graph exports
        let allData = csv;
        Object.entries(this.graphs).forEach(([name, graph]) => {
            if (graph && typeof graph.exportData === 'function') {
                allData += `\n=== ${name.toUpperCase()} ===\n`;
                allData += graph.exportData();
            }
        });

        // Create downloadable file
        const blob = new Blob([allData], { type: 'text/csv' });
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `graphs-export-${Date.now()}.csv`;
        a.click();
        window.URL.revokeObjectURL(url);

        AlertManager.add('Graph data exported', 'success', 2000);
    },

    cleanup() {
        console.log('[Dashboard] Cleaning up');

        // Remove event listener
        if (this.stateChangeHandler) {
            window.removeEventListener('state-changed', this.stateChangeHandler);
            this.stateChangeHandler = null;
        }

        // Clear update interval
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }

        // Cleanup graphs
        Object.values(this.graphs).forEach(graph => {
            if (graph && typeof graph.destroy === 'function') {
                graph.destroy();
            }
        });

        // Clear graph references
        this.graphs = {};
    }
};

// Expose module
window.currentPageModule = DashboardModule;
