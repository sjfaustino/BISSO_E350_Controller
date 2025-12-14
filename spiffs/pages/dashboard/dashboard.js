/**
 * Dashboard Page Module
 */
const DashboardModule = {
    history: { cpu: [], memory: [], spindle: [], timestamps: [] },
    maxHistory: 60,
    currentTimeRange: 1,
    chart: null,

    init() {
        console.log('[Dashboard] Initializing');
        this.setupEventListeners();
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
        this.history.timestamps.push(Date.now());

        if (this.history.cpu.length > this.maxHistory) {
            this.history.cpu.shift();
            this.history.memory.shift();
            this.history.spindle.shift();
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

    updateMetrics() {
        setInterval(() => {
            this.onStateChanged();
        }, 1000);
    },

    cleanup() {
        console.log('[Dashboard] Cleaning up');
        // Remove event listeners if needed
    }
};

// Expose module
window.currentPageModule = DashboardModule;
