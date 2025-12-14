/**
 * Maintenance Page Module
 * Tracks wear, service history, and component lifetime
 */
const MaintenanceModule = {
    // Component lifetimes in hours (typical industrial ratings)
    componentLifetimes: {
        motors: 50000,      // Stepper motors
        vfd: 100000,        // VFD capacitors
        encoders: 30000     // Encoder bearings
    },

    // Wear thresholds (jitter amplitude in mm/s indicates wear)
    wearThresholds: {
        healthy: 0.5,       // < 0.5 mm/s jitter = 0% wear
        warning: 1.0,       // > 1.0 mm/s jitter = 50% wear
        critical: 2.0       // > 2.0 mm/s jitter = 100% wear
    },

    init() {
        console.log('[Maintenance] Initializing');
        this.loadServiceHistory();
        this.updateMetrics();
        window.addEventListener('state-changed', () => this.onStateChanged());

        // Log service event button
        const logBtn = document.getElementById('log-service-btn');
        if (logBtn) {
            logBtn.addEventListener('click', () => this.logServiceEvent());
        }
    },

    onStateChanged() {
        this.updateMetrics();
    },

    updateMetrics() {
        const state = AppState.data;
        const now = new Date();

        // Get uptime from system
        const uptimeMs = state.system?.uptime_ms || 0;
        const totalHours = Math.floor(uptimeMs / (1000 * 60 * 60));
        const operatingDays = Math.floor(uptimeMs / (1000 * 60 * 60 * 24));

        document.getElementById('total-hours').textContent = totalHours.toString();
        document.getElementById('operating-days').textContent = operatingDays.toString();

        // Update wear predictions for each axis
        ['x', 'y', 'z'].forEach((axis, idx) => {
            const metrics = state.axis?.[axis];
            if (metrics) {
                const wearPercent = this.calculateWearPercent(metrics.jitter_mms || 0);
                const daysRemaining = this.estimateDaysRemaining(wearPercent);

                document.getElementById(`wear-${axis}-percent`).textContent =
                    wearPercent.toFixed(1) + '%';
                document.getElementById(`wear-${axis}-bar`).style.width =
                    wearPercent + '%';
                document.getElementById(`wear-${axis}-bar`).style.background =
                    this.getWearColor(wearPercent);
                document.getElementById(`wear-${axis}-days`).textContent =
                    `Est. ${Math.max(0, Math.floor(daysRemaining))} days`;
            }
        });

        // Update component lifetime based on operating hours
        const motorPercent = (totalHours / this.componentLifetimes.motors) * 100;
        const vfdPercent = (totalHours / this.componentLifetimes.vfd) * 100;
        const encoderPercent = (totalHours / this.componentLifetimes.encoders) * 100;

        document.getElementById('motor-hours').textContent = totalHours.toString();
        document.getElementById('motor-bar').style.width = Math.min(100, motorPercent) + '%';
        document.getElementById('motor-bar').style.background =
            this.getWearColor(Math.min(100, motorPercent));

        document.getElementById('vfd-hours').textContent = totalHours.toString();
        document.getElementById('vfd-bar').style.width = Math.min(100, vfdPercent) + '%';
        document.getElementById('vfd-bar').style.background =
            this.getWearColor(Math.min(100, vfdPercent));

        document.getElementById('encoder-hours').textContent = totalHours.toString();
        document.getElementById('encoder-bar').style.width = Math.min(100, encoderPercent) + '%';
        document.getElementById('encoder-bar').style.background =
            this.getWearColor(Math.min(100, encoderPercent));

        // Update maintenance calendar
        const motorServiceDays = Math.max(0,
            this.componentLifetimes.motors - totalHours) / 24;
        const vfdServiceDays = Math.max(0,
            this.componentLifetimes.vfd - totalHours) / 24;
        const encoderServiceDays = Math.max(0,
            this.componentLifetimes.encoders - totalHours) / 24;

        const motorDate = new Date(now.getTime() + motorServiceDays * 24 * 60 * 60 * 1000);
        const vfdDate = new Date(now.getTime() + vfdServiceDays * 24 * 60 * 60 * 1000);
        const encoderDate = new Date(now.getTime() + encoderServiceDays * 24 * 60 * 60 * 1000);

        document.getElementById('next-motor-service').textContent =
            `Est. ${motorDate.toISOString().split('T')[0]}`;
        document.getElementById('next-vfd-service').textContent =
            `Est. ${vfdDate.toISOString().split('T')[0]}`;
        document.getElementById('next-encoder-service').textContent =
            `Est. ${encoderDate.toISOString().split('T')[0]}`;

        // Update last service date
        const lastService = localStorage.getItem('lastServiceDate') || 'Today';
        document.getElementById('last-service').textContent = lastService;
    },

    calculateWearPercent(jitterMms) {
        if (jitterMms <= this.wearThresholds.healthy) {
            return 0;
        } else if (jitterMms >= this.wearThresholds.critical) {
            return 100;
        } else {
            // Linear interpolation between healthy and critical
            const range = this.wearThresholds.critical - this.wearThresholds.healthy;
            const excess = jitterMms - this.wearThresholds.healthy;
            return (excess / range) * 100;
        }
    },

    estimateDaysRemaining(wearPercent) {
        // Assume linear wear progression
        // At 100% wear, component fails (10 years nominal life)
        const nominalDays = 365 * 10;
        return (100 - wearPercent) / 100 * nominalDays;
    },

    getWearColor(percent) {
        if (percent < 33) return 'var(--color-optimal)';
        if (percent < 67) return 'var(--color-warning)';
        return 'var(--color-critical)';
    },

    loadServiceHistory() {
        const history = JSON.parse(localStorage.getItem('serviceHistory') || '[]');
        const container = document.getElementById('service-history');

        if (history.length === 0) {
            // Initialize with system startup
            history.push({
                date: new Date().toISOString().split('T')[0],
                type: 'System initialized',
                qualityScore: 100
            });
            localStorage.setItem('serviceHistory', JSON.stringify(history));
        }

        container.innerHTML = history.map(entry => `
            <div class="service-entry">
                <div class="service-date">${entry.date}</div>
                <div class="service-text">${entry.type}</div>
                <div class="service-quality">Quality: ${entry.qualityScore}%</div>
            </div>
        `).join('');
    },

    logServiceEvent() {
        const state = AppState.data;
        const now = new Date();
        const date = now.toISOString().split('T')[0];
        const avgQuality = this.calculateAverageQuality();

        const event = {
            date: date,
            type: 'Service performed',
            qualityScore: avgQuality
        };

        const history = JSON.parse(localStorage.getItem('serviceHistory') || '[]');
        history.push(event);
        localStorage.setItem('serviceHistory', JSON.stringify(history));
        localStorage.setItem('lastServiceDate', date);

        this.loadServiceHistory();
        AlertManager.add('Service event logged successfully', 'success', 2000);
    },

    calculateAverageQuality() {
        const state = AppState.data;
        const qualities = [];

        ['x', 'y', 'z'].forEach(axis => {
            const metrics = state.axis?.[axis];
            if (metrics && metrics.quality_score !== undefined) {
                qualities.push(metrics.quality_score);
            }
        });

        if (qualities.length === 0) return 100;
        return Math.round(qualities.reduce((a, b) => a + b) / qualities.length);
    },

    cleanup() {
        console.log('[Maintenance] Cleaning up');
    }
};

window.currentPageModule = MaintenanceModule;
