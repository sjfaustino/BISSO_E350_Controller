/**
 * Dashboard Page Module
 * Note: Use window.DashboardModule to avoid "already declared" errors when navigating
 */
window.DashboardModule = window.DashboardModule || {
    history: { cpu: [], memory: [], spindle: [], temperature: [], latency: [], motion: [], wifi: [], timestamps: [] },
    maxHistory: 300,
    currentTimeRange: 300000, // 5 minutes
    chart: null,
    graphs: {}, // Store GraphVisualizer instances
    lastTelemetry: null, // Store last telemetry for VFD/spindle data
    stateChangeHandler: null, // Store bound event handler for cleanup
    updateInterval: null, // Store interval ID for cleanup
    historicalDataLoaded: false, // Track if historical data has been bulk-loaded

    // Helper to set N/A values with red styling (only n/a is red, suffix is normal)
    setNA(element, suffix = '') {
        if (!element) return;
        element.classList.remove('value-na'); // Ensure parent doesn't inherit red
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

    // Helper to set normal value and remove N/A styling
    setValue(element, value) {
        if (!element) return;
        element.textContent = value;
        element.classList.remove('value-na');
    },

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

        this.loadHistoryData();
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

                // Update all active graphs with new time window
                Object.values(this.graphs).forEach(graph => {
                    if (graph && graph.config) {
                        graph.config.timeWindow = this.currentTimeRange;
                        if (typeof graph.draw === 'function') graph.draw(); // Trigger immediate redraw
                    }
                });
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

    loadHistoryData() {
        if (window.location.protocol === 'file:') return;

        fetch('/api/history/telemetry')
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    console.log('[Dashboard] Loading', data.cpu?.length, 'history samples');

                    // Populate history arrays
                    if (data.cpu) this.history.cpu = data.cpu;
                    if (data.heap) this.history.memory = data.heap.map(v => v / 1024);
                    if (data.spindle_amps) this.history.spindle = data.spindle_amps;

                    // Create dummy timestamps if not provided (spaced 5s apart)
                    if (data.cpu && data.cpu.length > 0) {
                        const now = Date.now();
                        this.history.timestamps = data.cpu.map((_, i) => now - (data.cpu.length - 1 - i) * 5000);
                    }

                    this.historicalDataLoaded = false; // Trigger bulk-load on next updateGraphs
                    this.updateGraphs();
                }
            })
            .catch(err => console.warn('[Dashboard] Failed to load history:', err));
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
                    yMax: 350, // ~350 KB typical free heap, auto-scales if exceeded
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
                    title: 'CPU Temperature (°C)',
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

        // WiFi Signal Graph
        if (document.getElementById('wifi-graph')) {
            try {
                this.graphs.wifi = new GraphVisualizer('wifi-graph', {
                    title: 'WiFi Signal Quality (%)',
                    yMin: 0,
                    yMax: 100,
                    unit: '%',
                    timeWindow: this.currentTimeRange
                });
                this.graphs.wifi.addSeries('Signal', '#3b82f6');
            } catch (e) { console.warn('WiFi graph init failed:', e); }
        }

        console.log('[Dashboard] Graphs initialized');

        // Restore historical data to graphs if we have any
        this.restoreHistoricalData();
    },

    restoreHistoricalData() {
        // If we have historical data, restore it to the newly created graphs
        const now = Date.now();
        const timeStep = 1000; // Assume 1 second intervals

        if (this.history.cpu.length > 0) {
            console.log(`[Dashboard] Restoring ${this.history.cpu.length} historical data points`);

            // For each data point, we need to calculate a timestamp
            // Work backwards from now
            for (let i = 0; i < this.history.cpu.length; i++) {
                const age = (this.history.cpu.length - 1 - i) * timeStep;
                const timestamp = now - age;

                // Add data to each graph if it exists
                if (this.graphs.cpu && this.history.cpu[i] !== undefined) {
                    this.graphs.cpu.addDataPoint('CPU', this.history.cpu[i], timestamp);
                }
                if (this.graphs.memory && this.history.memory[i] !== undefined) {
                    this.graphs.memory.addDataPoint('Memory', this.history.memory[i], timestamp);
                }
                if (this.graphs.spindle && this.history.spindle[i] !== undefined) {
                    this.graphs.spindle.addDataPoint('Current', this.history.spindle[i], timestamp);
                }
                if (this.graphs.temperature && this.history.temperature[i] !== undefined) {
                    this.graphs.temperature.addDataPoint('Temp', this.history.temperature[i], timestamp);
                }
                if (this.graphs.latency && this.history.latency[i] !== undefined) {
                    this.graphs.latency.addDataPoint('Latency', this.history.latency[i], timestamp);
                }
                if (this.graphs.wifi && this.history.wifi[i] !== undefined) {
                    this.graphs.wifi.addDataPoint('Signal', this.history.wifi[i], timestamp);
                }
            }
        }
    },



    onStateChanged() {
        const state = AppState.data;
        this.lastTelemetry = state;

        this.updateSystemStatus(state);
        this.updateMotionStatus(state);
        this.updateVFDStatus(state);
        this.updateNetworkStatus(state);

        // Axis metrics
        if (state.axis) {
            this.updateAxisCard('x', state.axis.x);
            this.updateAxisCard('y', state.axis.y);
            this.updateAxisCard('z', state.axis.z);

            // Update DRO (Digital Readout) display
            this.updateDRO(state.axis);
        }

        this.updateHistoryData(state);
        this.drawChart();
    },

    updateSystemStatus(state) {
        if (!state.system) return;

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
        if (cpuValueEl) cpuValueEl.textContent = cpu.toFixed(1) + '%';

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
    },

    updateMotionStatus(state) {
        const plcPresent = state.system?.plc_hardware_present !== false; // Default true

        // Update Motion Header
        this.updateHeaderNA('header-motion', 'Motion Status', plcPresent);
        this.updateHeaderNA('header-motion-load', 'Motion System Load', plcPresent);

        if (state.motion) {
            const motionStatusEl = document.getElementById('motion-status');
            if (motionStatusEl) {
                if (!plcPresent) {
                    this.setNA(motionStatusEl);
                } else {
                    motionStatusEl.textContent = state.motion.moving ? '🔄 Moving' : '⏸️ Stopped';
                    motionStatusEl.style.color = '';
                }
            }
        }

        // Safety status
        if (state.safety) {
            const safetyStatusEl = document.getElementById('safety-status');
            if (safetyStatusEl) {
                if (!plcPresent) {
                    this.setNA(safetyStatusEl);
                } else {
                    let safetyText = '✓ OK';
                    if (state.safety.estop) safetyText = '🛑 E-STOP';
                    else if (state.safety.alarm) safetyText = '⚠️ ALARM';
                    safetyStatusEl.textContent = safetyText;
                    safetyStatusEl.style.color = '';
                }
            }
        }
    },

    updateVFDStatus(state) {
        if (!state.vfd) return;

        // Update VFD Headers
        this.updateHeaderNA('header-vfd', 'Axis Drive', state.vfd.connected);
        this.updateHeaderNA('header-spindle-trend', 'Spindle Current Trend', state.vfd.connected);

        const vfdStatusEl = document.getElementById('vfd-status');
        const vfdRpmEl = document.getElementById('spindle-rpm');
        const vfdSpeedEl = document.getElementById('spindle-speed');
        const vfdCurrentEl = document.getElementById('spindle-current');

        if (state.vfd.connected) {
            const motorStatus = (state.vfd.rpm > 0) ? 'RUNNING' : 'IDLE';
            if (vfdStatusEl) vfdStatusEl.textContent = motorStatus;

            if (vfdRpmEl) vfdRpmEl.textContent = (state.vfd.rpm || 0).toFixed(0);
            if (vfdSpeedEl) vfdSpeedEl.textContent = (state.vfd.speed_m_s || 0).toFixed(1) + ' m/s';
            if (vfdCurrentEl) vfdCurrentEl.textContent = (state.vfd.current_amps || 0).toFixed(2) + ' A';

            const bar = document.getElementById('spindle-bar');
            if (bar) {
                const pct = Math.min(100, ((state.vfd.current_amps || 0) / 30.0) * 100);
                bar.style.width = pct + '%';
            }
        } else {
            if (vfdStatusEl) vfdStatusEl.textContent = 'DISCONNECTED';
            this.setNA(vfdRpmEl);
            this.setNA(vfdSpeedEl, ' m/s');
            this.setNA(vfdCurrentEl, ' A');

            const bar = document.getElementById('spindle-bar');
            if (bar) bar.style.width = '0%';
        }

        // Axis Drive Card (Middle section)
        const vfdFreqEl = document.getElementById('vfd-freq');
        if (state.vfd.connected) {
            if (vfdFreqEl) vfdFreqEl.textContent = (state.vfd.frequency_hz || 0).toFixed(1) + ' Hz';
        } else {
            this.setNA(vfdFreqEl, ' Hz');
        }
    },

    updateNetworkStatus(state) {
        if (!state.network) return;

        const wifiSignalEl = document.getElementById('wifi-signal');
        if (wifiSignalEl) wifiSignalEl.textContent = state.network.signal_percent + '%';

        const wifiBarEl = document.getElementById('wifi-bar');
        if (wifiBarEl) wifiBarEl.style.width = state.network.signal_percent + '%';

        const wifiStatusEl = document.getElementById('wifi-status');
        if (wifiStatusEl) wifiStatusEl.textContent = state.network.wifi_connected ? '✓ Connected' : '✗ Disconnected';
    },

    updateHistoryData(state) {
        // Update history for chart
        this.history.cpu.push(state.system?.cpu_percent || 0);
        this.history.memory.push((state.system?.free_heap_bytes || 0) / 1024);
        this.history.spindle.push(state.vfd?.current_amps || 0);
        this.history.temperature.push(state.system?.temperature || 0);
        this.history.latency.push(SharedWebSocket.latency || 0);

        // Network signal
        this.history.wifi.push(state.network?.signal_percent || 0);

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
            this.history.wifi.shift();
            this.history.timestamps.shift();
        }
    },

    updateAxisCard(axis, metrics) {
        if (!metrics) return;

        const connected = AppState.data.motion?.dro_connected ?? false;
        const prefix = `axis-${axis}`;
        const axisTitle = axis.toUpperCase() + ' Axis Quality';
        this.updateHeaderNA(`header-axis-${axis}`, axisTitle, connected);

        const qualityEl = document.getElementById(`${prefix}-quality`);
        if (connected) this.setValue(qualityEl, metrics.quality || 0);
        else this.setNA(qualityEl);

        const barEl = document.getElementById(`${prefix}-bar`);
        if (barEl) barEl.style.width = connected ? ((metrics.quality || 0) + '%') : '0%';
        if (barEl) {
            if (!connected) barEl.classList.add('offline'); // Optional styling
            else barEl.classList.remove('offline');
        }

        const jitterEl = document.getElementById(`${prefix}-jitter`);
        if (connected) this.setValue(jitterEl, (metrics.jitter_mms || 0).toFixed(3) + ' mm/s');
        else this.setNA(jitterEl);

        const errorEl = document.getElementById(`${prefix}-error`);
        if (connected) this.setValue(errorEl, (metrics.vfd_error_percent || 0).toFixed(1) + '%');
        else this.setNA(errorEl);

        const stalledEl = document.getElementById(`${prefix}-stalled`);
        if (stalledEl) {
            if (!connected) {
                stalledEl.textContent = 'OFFLINE';
                stalledEl.style.color = 'var(--color-critical)'; // Red for Offline
            } else if (metrics.stalled) {
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
        // Check connection status from motion state
        // Default to false (Offline) to prevent "Live" flicker on boot before data arrives
        const connected = AppState.data.motion?.dro_connected ?? false;

        // Update Header (N/A)
        this.updateHeaderNA('header-dro', '🎯 Position (DRO)', connected);

        axes.forEach(axis => {
            const el = document.getElementById(`dro-${axis}`);
            if (el) {
                if (!connected) {
                    this.setNA(el);
                    el.classList.remove('negative');
                } else {
                    // Get position from axis data (position_mm or position field)
                    const pos = axisData[axis]?.position_mm ?? axisData[axis]?.position ?? 0;
                    el.textContent = pos.toFixed(3);
                    // Add negative class for styling
                    el.classList.toggle('negative', pos < 0);
                }
            }
        });

        // Update live status indicator
        const statusEl = document.getElementById('dro-status');
        if (statusEl) {
            if (!connected) {
                statusEl.textContent = 'Offline';
                statusEl.classList.add('offline');
            } else {
                statusEl.textContent = 'Live';
                statusEl.classList.remove('offline');
            }
        }
    },

    drawChart() {
        const canvas = document.getElementById('trendsChart');
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        const rect = canvas.getBoundingClientRect();
        canvas.width = rect.width;
        canvas.height = rect.height;

        // Margins for labels
        const margin = { top: 10, right: 10, bottom: 20, left: 35 };
        const width = canvas.width - margin.left - margin.right;
        const height = canvas.height - margin.top - margin.bottom;

        // Clear
        ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--bg-primary').trim() || '#fff';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Draw Grid & Y-Axis Labels
        ctx.strokeStyle = '#e5e7eb'; // Light gray for grid
        ctx.lineWidth = 1;
        ctx.fillStyle = '#6b7280'; // Gray for text
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';

        // Draw 5 horizontal grid lines
        for (let i = 0; i <= 4; i++) {
            const yRatio = i / 4;
            const y = margin.top + height - (yRatio * height);

            // Grid line
            ctx.beginPath();
            ctx.moveTo(margin.left, y);
            ctx.lineTo(canvas.width - margin.right, y);
            ctx.stroke();

            // Label (0-100%)
            const value = yRatio * 100;
            ctx.fillText(value.toFixed(0), margin.left - 5, y);
        }

        // X-Axis Labels (Time)
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';
        const timeLabel = this.currentTimeRange / 1000 >= 60 ? (this.currentTimeRange / 60000) + 'm' : (this.currentTimeRange / 1000) + 's';
        ctx.fillText('-' + timeLabel, margin.left, canvas.height - margin.bottom + 5);
        ctx.fillText('Now', canvas.width - margin.right, canvas.height - margin.bottom + 5);

        // Get visible data
        const sampleSize = Math.min(this.history.cpu.length, this.currentTimeRange * 60); // Approx sample rate
        const visibleCpu = this.history.cpu.slice(-sampleSize);
        const visibleMem = this.history.memory.slice(-sampleSize);
        const visibleSpindle = this.history.spindle.slice(-sampleSize);

        // Draw lines
        const drawLine = (data, color, scale, fallbackColor = '#888') => {
            if (!data || data.length < 2) return;

            ctx.strokeStyle = color || fallbackColor;
            ctx.lineWidth = 2;
            ctx.beginPath();

            for (let i = 0; i < data.length; i++) {
                const value = Math.max(0, Math.min(100, (data[i] / scale) * 100));
                const x = margin.left + (i / Math.max(1, data.length - 1)) * width;
                const y = margin.top + height - (value / 100) * height;

                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();
        };

        const cpuColor = getComputedStyle(document.documentElement).getPropertyValue('--chart-cpu').trim() || '#10b981';
        const memColor = getComputedStyle(document.documentElement).getPropertyValue('--chart-mem').trim() || '#3b82f6';
        const spindleColor = getComputedStyle(document.documentElement).getPropertyValue('--chart-spindle').trim() || '#f59e0b';

        // Visibility check and scaling:
        // CPU: 0-100%, skip division
        // Memory: data is in KB, scale by total (320KB typical free)
        // Spindle: data is in Amps, scale by 30A
        drawLine(visibleCpu, cpuColor, 100);
        drawLine(visibleMem, memColor, 320);
        drawLine(visibleSpindle, spindleColor, 30);

        // Draw Border axes
        ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--text-primary').trim();
        ctx.lineWidth = 1; // Thinner border
        ctx.beginPath();
        ctx.moveTo(margin.left, margin.top);
        ctx.lineTo(margin.left, canvas.height - margin.bottom);
        ctx.lineTo(canvas.width - margin.right, canvas.height - margin.bottom);
        ctx.stroke();
    },

    updateGraphs() {
        // Detect if we're using MiniChart (single-arg addDataPoint) or GraphVisualizer (two-arg)
        const isMiniChart = this.graphs.cpu && this.graphs.cpu.constructor.name === 'MiniChart';

        // Bulk-load historical data to charts if not already loaded
        if (!this.historicalDataLoaded && this.history.cpu.length > 1) {
            console.log(`[Dashboard] Bulk-loading ${this.history.cpu.length} points to charts`);

            for (let i = 0; i < this.history.cpu.length; i++) {
                if (this.history.cpu[i] !== undefined) {
                    if (isMiniChart) this.graphs.cpu?.addDataPoint(this.history.cpu[i]);
                    else this.graphs.cpu?.addDataPoint('CPU', this.history.cpu[i]);
                }
                if (this.history.memory[i] !== undefined) {
                    if (isMiniChart) this.graphs.memory?.addDataPoint(this.history.memory[i]);
                    else this.graphs.memory?.addDataPoint('Memory', this.history.memory[i]);
                }
                if (this.history.spindle[i] !== undefined) {
                    if (isMiniChart) this.graphs.spindle?.addDataPoint(this.history.spindle[i]);
                    else this.graphs.spindle?.addDataPoint('Spindle', this.history.spindle[i]);
                }
                if (this.history.temperature[i] !== undefined) {
                    if (isMiniChart) this.graphs.temperature?.addDataPoint(this.history.temperature[i]);
                    else this.graphs.temperature?.addDataPoint('Temperature', this.history.temperature[i]);
                }
            }

            this.historicalDataLoaded = true;
            console.log('[Dashboard] Bulk-load complete');
            return;
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

            // Update CPU stats - always calculate from history
            {
                let cpuStats = this.graphs.cpu?.getStats('CPU');

                // Fallback: calculate stats from history if getStats returns null
                if (!cpuStats && this.history.cpu.length > 0) {
                    const values = this.history.cpu;
                    cpuStats = {
                        avg: values.reduce((a, b) => a + b, 0) / values.length,
                        max: Math.max(...values),
                        current: cpu
                    };
                }

                if (cpuStats) {
                    const cpuValueEl = document.getElementById('cpu-value');
                    const cpuAvgEl = document.getElementById('cpu-avg');
                    const cpuMaxEl = document.getElementById('cpu-max');

                    if (cpuValueEl) cpuValueEl.textContent = cpu.toFixed(1) + '%';
                    if (cpuAvgEl) cpuAvgEl.textContent = cpuStats.avg.toFixed(1);
                    if (cpuMaxEl) cpuMaxEl.textContent = cpuStats.max.toFixed(1);

                    // Update Advanced section
                    const advCpuCurrent = document.getElementById('cpu-current');
                    const advCpuAvg = document.getElementById('cpu-stat-avg');
                    const advCpuMax = document.getElementById('cpu-stat-max');

                    if (advCpuCurrent) advCpuCurrent.textContent = cpu.toFixed(1) + '%';
                    if (advCpuAvg) advCpuAvg.textContent = cpuStats.avg.toFixed(1) + '%';
                    if (advCpuMax) advCpuMax.textContent = cpuStats.max.toFixed(1) + '%';
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
                    const memCurrentEl = document.getElementById('mem-value'); // Fixed ID
                    const memMinEl = document.getElementById('mem-min');

                    if (memCurrentEl) memCurrentEl.textContent = mem.toFixed(0) + ' KB';
                    if (memMinEl) memMinEl.textContent = memStats.min.toFixed(0);

                    // Update Advanced section
                    const advMemCurrent = document.getElementById('mem-current');
                    const advMemAvg = document.getElementById('mem-stat-avg');
                    const advMemMax = document.getElementById('mem-stat-max');

                    if (advMemCurrent) advMemCurrent.textContent = mem.toFixed(0) + ' KB';
                    if (advMemAvg) advMemAvg.textContent = memStats.avg.toFixed(0) + ' KB';
                    if (advMemMax) advMemMax.textContent = memStats.max.toFixed(0) + ' KB';
                }
            }
        }

        // Only update spindle graph and display if VFD is connected
        const vfdConnected = this.lastTelemetry?.vfd?.connected;

        if (this.history.spindle.length > 0 && vfdConnected) {
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

                    // Legacy/Advanced stat fields
                    const statCurrent = document.getElementById('spindle-stat-current');
                    const statAvg = document.getElementById('spindle-stat-avg');
                    const statMax = document.getElementById('spindle-stat-max');

                    if (statCurrent) statCurrent.textContent = spindle.toFixed(2) + ' A';
                    if (statAvg) statAvg.textContent = spindleStats.avg.toFixed(2) + ' A';
                    if (statMax) statMax.textContent = spindleStats.max.toFixed(2) + ' A';
                }
            }
        } else if (!vfdConnected) {
            // Handle N/A for Advanced section if disconnected
            this.setNA(document.getElementById('spindle-stat-current'), ' A');
            this.setNA(document.getElementById('spindle-stat-avg'), ' A');
            this.setNA(document.getElementById('spindle-stat-max'), ' A');
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
            const connected = AppState.data.motion?.dro_connected ?? false; // Check connection

            // Update Header (N/A)
            this.updateHeaderNA('header-motion-load', 'Motion System Load', connected);

            // Use 0 if offline, otherwise use value (default 0)
            const quality = connected ? (motion.quality || 0) : 0;
            const jitter = connected ? (motion.jitter || 0) : 0;

            this.graphs.motion?.addDataPoint('Quality', quality);
            this.graphs.motion?.addDataPoint('Jitter', jitter);

            const motionQualityEl = document.getElementById('motion-quality');
            const motionJitterEl = document.getElementById('motion-jitter');

            if (connected) {
                this.setValue(motionQualityEl, quality.toFixed(0) + '%');
                this.setValue(motionJitterEl, jitter.toFixed(2) + ' mm/s');
            } else {
                this.setNA(motionQualityEl);
                this.setNA(motionJitterEl);
            }
        }

        if (this.history.wifi.length > 0 && !isMiniChart) {
            const signal = this.history.wifi[this.history.wifi.length - 1];
            this.graphs.wifi?.addDataPoint('Signal', signal);

            const wifiStats = this.graphs.wifi?.getStats('Signal');
            if (wifiStats) {
                const wifiCurrentEl = document.getElementById('wifi-current');
                const wifiAvgEl = document.getElementById('wifi-stat-avg');
                const wifiMaxEl = document.getElementById('wifi-stat-max');

                if (wifiCurrentEl) wifiCurrentEl.textContent = signal.toFixed(0) + '%';
                if (wifiAvgEl) wifiAvgEl.textContent = wifiStats.avg.toFixed(0) + '%';
                if (wifiMaxEl) wifiMaxEl.textContent = wifiStats.max.toFixed(0) + '%';
            }
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
