/**
 * Dashboard Page Module
 */
const DashboardModule = {
    history: { cpu: [], memory: [], spindle: [], temperature: [], latency: [], motion: [], timestamps: [] },
    maxHistory: 300,
    currentTimeRange: 300000, // 5 minutes
    chart: null,
    graphs: {}, // Store GraphVisualizer instances

    init() {
        console.log('[Dashboard] Initializing');
        this.setupEventListeners();
        this.initializeGraphs();
        this.updateMetrics();
        window.addEventListener('state-changed', () => this.onStateChanged());
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
    },

    initializeGraphs() {
        // CPU Graph
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

        // Memory Graph
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

        // Spindle Current Graph
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

        // Temperature Graph
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

        // Latency Graph
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

        // Motion Load Graph
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

        console.log('[Dashboard] Graphs initialized');
    },

    onStateChanged() {
        const state = AppState.data;

        // System metrics
        if (state.system) {
            const cpu = state.system.cpu_percent || 0;
            const mem = state.system.free_heap_bytes || 0;

            document.getElementById('cpu-value').textContent = cpu + '%';
            const cpuBar = document.getElementById('cpu-bar');
            cpuBar.style.width = cpu + '%';
            cpuBar.className = 'progress-fill';
            if (cpu > 85) cpuBar.classList.add('warning');
            if (cpu > 95) cpuBar.classList.add('critical');

            document.getElementById('mem-value').textContent = (mem / 1024).toFixed(0) + ' KB';
            const memBar = document.getElementById('mem-bar');
            memBar.style.width = Math.min(100, (mem / 50000) * 100) + '%';
        }

        // Motion status
        if (state.motion) {
            const status = state.motion.moving ? '🔄 Moving' : '⏸️ Stopped';
            document.getElementById('motion-status').textContent = status;
        }

        // Safety status
        if (state.safety) {
            let safetyText = '✓ OK';
            if (state.safety.estop) safetyText = '🛑 E-STOP';
            else if (state.safety.alarm) safetyText = '⚠️ ALARM';
            document.getElementById('safety-status').textContent = safetyText;
        }

        // VFD status
        if (state.vfd) {
            const motorStatus = state.vfd.frequency_hz > 0.5 ? 'RUNNING' : 'IDLE';
            document.getElementById('vfd-status').textContent = motorStatus;
            document.getElementById('vfd-freq').textContent = state.vfd.frequency_hz.toFixed(1) + ' Hz';
            document.getElementById('vfd-current').textContent = state.vfd.current_amps.toFixed(1) + ' A';
        }

        // Network status
        if (state.network) {
            document.getElementById('wifi-signal').textContent = state.network.signal_percent + '%';
            document.getElementById('wifi-bar').style.width = state.network.signal_percent + '%';
            document.getElementById('wifi-status').textContent = state.network.wifi_connected ? '✓ Connected' : '✗ Disconnected';
        }

        // Axis metrics
        if (state.axis) {
            this.updateAxisCard('x', state.axis.x);
            this.updateAxisCard('y', state.axis.y);
            this.updateAxisCard('z', state.axis.z);
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
        document.getElementById(`${prefix}-quality`).textContent = metrics.quality || 0;
        document.getElementById(`${prefix}-bar`).style.width = (metrics.quality || 0) + '%';
        document.getElementById(`${prefix}-jitter`).textContent = (metrics.jitter_mms || 0).toFixed(3) + ' mm/s';
        document.getElementById(`${prefix}-error`).textContent = (metrics.vfd_error_percent || 0).toFixed(1) + '%';

        const stalledEl = document.getElementById(`${prefix}-stalled`);
        if (metrics.stalled) {
            stalledEl.textContent = '⚠️ STALLED';
            stalledEl.style.color = 'var(--color-critical)';
        } else {
            stalledEl.textContent = '✓ OK';
            stalledEl.style.color = 'var(--color-optimal)';
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
        // Add data points to graphs from history
        if (this.history.cpu.length > 0) {
            const cpu = this.history.cpu[this.history.cpu.length - 1];
            this.graphs.cpu?.addDataPoint('CPU', cpu);

            // Update CPU stats
            const cpuStats = this.graphs.cpu?.getStats('CPU');
            if (cpuStats) {
                document.getElementById('cpu-current').textContent = cpu.toFixed(1) + '%';
                document.getElementById('cpu-stat-avg').textContent = cpuStats.avg.toFixed(1) + '%';
                document.getElementById('cpu-stat-max').textContent = cpuStats.max.toFixed(1) + '%';
            }
        }

        if (this.history.memory.length > 0) {
            const mem = this.history.memory[this.history.memory.length - 1];
            this.graphs.memory?.addDataPoint('Memory', mem);

            const memStats = this.graphs.memory?.getStats('Memory');
            if (memStats) {
                document.getElementById('mem-current').textContent = mem.toFixed(0) + ' KB';
                document.getElementById('mem-stat-avg').textContent = memStats.avg.toFixed(0) + ' KB';
                document.getElementById('mem-stat-max').textContent = memStats.max.toFixed(0) + ' KB';
            }
        }

        if (this.history.spindle.length > 0) {
            const spindle = this.history.spindle[this.history.spindle.length - 1];
            this.graphs.spindle?.addDataPoint('Current', spindle);

            const spindleStats = this.graphs.spindle?.getStats('Current');
            if (spindleStats) {
                document.getElementById('spindle-current').textContent = spindle.toFixed(2) + ' A';
                document.getElementById('spindle-stat-avg').textContent = spindleStats.avg.toFixed(2) + ' A';
                document.getElementById('spindle-stat-max').textContent = spindleStats.max.toFixed(2) + ' A';
            }
        }

        if (this.history.temperature.length > 0) {
            const temp = this.history.temperature[this.history.temperature.length - 1];
            this.graphs.temperature?.addDataPoint('Temp', temp);

            const tempStats = this.graphs.temperature?.getStats('Temp');
            if (tempStats) {
                document.getElementById('temp-current').textContent = temp.toFixed(1) + ' °C';
                document.getElementById('temp-stat-avg').textContent = tempStats.avg.toFixed(1) + ' °C';
                document.getElementById('temp-stat-max').textContent = tempStats.max.toFixed(1) + ' °C';
            }
        }

        if (this.history.latency.length > 0) {
            const latency = this.history.latency[this.history.latency.length - 1];
            this.graphs.latency?.addDataPoint('Latency', latency);

            const latencyStats = this.graphs.latency?.getStats('Latency');
            if (latencyStats) {
                document.getElementById('latency-current').textContent = latency.toFixed(0) + ' ms';
                document.getElementById('latency-stat-avg').textContent = latencyStats.avg.toFixed(0) + ' ms';
                document.getElementById('latency-stat-max').textContent = latencyStats.max.toFixed(0) + ' ms';
            }
        }

        if (this.history.motion.length > 0) {
            const motion = this.history.motion[this.history.motion.length - 1];
            this.graphs.motion?.addDataPoint('Quality', motion.quality || 80);
            this.graphs.motion?.addDataPoint('Jitter', motion.jitter || 0.5);

            document.getElementById('motion-quality').textContent = (motion.quality || 80).toFixed(0) + '%';
            document.getElementById('motion-jitter').textContent = (motion.jitter || 0.5).toFixed(2) + ' mm/s';
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

    updateMetrics() {
        setInterval(() => {
            this.onStateChanged();
            this.updateGraphs();
        }, 1000);
    },

    cleanup() {
        console.log('[Dashboard] Cleaning up');
        // Cleanup graphs
        Object.values(this.graphs).forEach(graph => {
            if (graph && typeof graph.destroy === 'function') {
                graph.destroy();
            }
        });
    }
};

// Expose module
window.currentPageModule = DashboardModule;
