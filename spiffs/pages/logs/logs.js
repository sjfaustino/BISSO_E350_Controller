/**
 * Logs Page Module
 * Tracks fault and operation history with filtering and export
 * Note: Use window.LogsModule to avoid "already declared" errors when navigating
 */
window.LogsModule = window.LogsModule || {
    // In-memory log buffer (max 10000 entries)
    logs: [],
    maxLogs: 10000,
    filteredLogs: [],

    // Current filter state
    filters: {
        level: '',
        source: '',
        search: ''
    },

    init() {
        console.log('[Logs] Initializing');
        this.loadLogsFromStorage();
        this.setupEventListeners();
        this.updateDisplay();
        window.addEventListener('state-changed', () => this.onStateChanged());
    },

    setupEventListeners() {
        // Filter buttons
        document.getElementById('filter-apply-btn')?.addEventListener('click',
            () => this.applyFilters());
        document.getElementById('filter-clear-btn')?.addEventListener('click',
            () => this.clearFilters());

        // Export buttons
        document.getElementById('export-csv-btn')?.addEventListener('click',
            () => this.exportAsCSV());
        document.getElementById('export-json-btn')?.addEventListener('click',
            () => this.exportAsJSON());
        document.getElementById('clear-logs-btn')?.addEventListener('click',
            () => this.clearLogs());

        // Filter inputs
        document.getElementById('log-level-filter')?.addEventListener('change', (e) => {
            this.filters.level = e.target.value;
        });
        document.getElementById('log-source-filter')?.addEventListener('change', (e) => {
            this.filters.source = e.target.value;
        });
        document.getElementById('log-search')?.addEventListener('input', (e) => {
            this.filters.search = e.target.value.toLowerCase();
        });
    },

    onStateChanged() {
        // Add relevant state changes to log
        const state = AppState.data;

        // Monitor for quality degradation
        ['x', 'y', 'z'].forEach(axis => {
            const metrics = state.axis?.[axis];
            if (metrics) {
                const lastAxis = this.lastAxisState?.[axis];
                if (lastAxis && lastAxis.quality > 50 && metrics.quality_score <= 50) {
                    this.addLog('ERROR', 'motion',
                        `${axis.toUpperCase()} axis quality degraded to ${metrics.quality_score}%`);
                }
                if (lastAxis && !lastAxis.stalled && metrics.stalled) {
                    this.addLog('CRITICAL', 'motion',
                        `${axis.toUpperCase()} axis STALLED`);
                }
            }
        });

        // Track axis state for change detection
        if (!this.lastAxisState) {
            this.lastAxisState = {};
        }
        ['x', 'y', 'z'].forEach(axis => {
            const metrics = state.axis?.[axis];
            if (metrics) {
                this.lastAxisState[axis] = {
                    quality: metrics.quality_score,
                    stalled: metrics.stalled
                };
            }
        });

        // Monitor VFD errors
        if (state.vfd?.fault_code && state.vfd.fault_code !== 0) {
            const faultCode = '0x' + state.vfd.fault_code.toString(16).padStart(4, '0').toUpperCase();
            if (this.lastFaultCode !== faultCode) {
                this.addLog('ERROR', 'vfd', `VFD fault detected: ${faultCode}`);
                this.lastFaultCode = faultCode;
            }
        }
    },

    addLog(level, source, message) {
        const now = new Date();
        const timestamp = now.toISOString().replace('T', ' ').split('.')[0];

        const entry = {
            timestamp,
            level,
            source,
            message
        };

        this.logs.unshift(entry);

        // Maintain max log size
        if (this.logs.length > this.maxLogs) {
            this.logs.pop();
        }

        this.saveLogsToStorage();
        this.applyFilters();
    },

    applyFilters() {
        this.filteredLogs = this.logs.filter(log => {
            if (this.filters.level && log.level !== this.filters.level) return false;
            if (this.filters.source && log.source !== this.filters.source) return false;
            if (this.filters.search &&
                !(log.message.toLowerCase().includes(this.filters.search) ||
                  log.source.toLowerCase().includes(this.filters.search))) {
                return false;
            }
            return true;
        });

        this.updateDisplay();
    },

    clearFilters() {
        this.filters = { level: '', source: '', search: '' };
        document.getElementById('log-level-filter').value = '';
        document.getElementById('log-source-filter').value = '';
        document.getElementById('log-search').value = '';
        this.applyFilters();
    },

    updateDisplay() {
        const container = document.getElementById('log-container');
        if (!container) return;

        if (this.filteredLogs.length === 0) {
            container.innerHTML = '<div class="log-entry log-info"><span>No logs to display</span></div>';
        } else {
            container.innerHTML = this.filteredLogs.map(log => `
                <div class="log-entry log-${log.level.toLowerCase()}">
                    <span class="log-time">${log.timestamp}</span>
                    <span class="log-level">${log.level}</span>
                    <span class="log-source">${log.source}</span>
                    <span class="log-message">${this.escapeHtml(log.message)}</span>
                </div>
            `).join('');
        }

        // Update statistics
        this.updateStatistics();
    },

    updateStatistics() {
        const stats = {
            total: this.logs.length,
            critical: this.logs.filter(l => l.level === 'CRITICAL').length,
            error: this.logs.filter(l => l.level === 'ERROR').length,
            warn: this.logs.filter(l => l.level === 'WARN').length
        };

        document.getElementById('stat-total').textContent = stats.total.toString();
        document.getElementById('stat-critical').textContent = stats.critical.toString();
        document.getElementById('stat-error').textContent = stats.error.toString();
        document.getElementById('stat-warning').textContent = stats.warn.toString();
    },

    exportAsCSV() {
        const headers = ['Timestamp', 'Level', 'Source', 'Message'];
        const rows = this.filteredLogs.map(log =>
            [log.timestamp, log.level, log.source, `"${log.message}"`]
        );

        const csv = [headers, ...rows]
            .map(row => row.join(','))
            .join('\n');

        this.downloadFile(csv, 'logs.csv', 'text/csv');
        AlertManager.add('Logs exported as CSV', 'success', 2000);
    },

    exportAsJSON() {
        const json = JSON.stringify(this.filteredLogs, null, 2);
        this.downloadFile(json, 'logs.json', 'application/json');
        AlertManager.add('Logs exported as JSON', 'success', 2000);
    },

    downloadFile(content, filename, mimeType) {
        const blob = new Blob([content], { type: mimeType });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = filename;
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
        URL.revokeObjectURL(url);
    },

    clearLogs() {
        if (confirm('Clear all logs? This cannot be undone.')) {
            this.logs = [];
            this.saveLogsToStorage();
            this.applyFilters();
            AlertManager.add('All logs cleared', 'info', 2000);
        }
    },

    saveLogsToStorage() {
        // Only save if logs changed recently (debounce)
        if (this.saveTimeout) {
            clearTimeout(this.saveTimeout);
        }

        this.saveTimeout = setTimeout(() => {
            try {
                localStorage.setItem('systemLogs', JSON.stringify(this.logs));
            } catch (e) {
                console.warn('[Logs] Failed to save to storage (quota exceeded):', e);
                // Remove oldest half of logs if storage is full
                this.logs = this.logs.slice(0, Math.floor(this.logs.length / 2));
                try {
                    localStorage.setItem('systemLogs', JSON.stringify(this.logs));
                } catch (e2) {
                    console.error('[Logs] Still cannot save logs:', e2);
                }
            }
        }, 1000);
    },

    loadLogsFromStorage() {
        try {
            const stored = localStorage.getItem('systemLogs');
            if (stored) {
                this.logs = JSON.parse(stored);
            }
        } catch (e) {
            console.warn('[Logs] Failed to load logs from storage:', e);
        }

        // Always add system init log
        if (this.logs.length === 0) {
            this.addLog('INFO', 'system', 'System initialized');
        }
    },

    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    },

    cleanup() {
        console.log('[Logs] Cleaning up');
        if (this.saveTimeout) {
            clearTimeout(this.saveTimeout);
        }
    }
};

window.currentPageModule = LogsModule;
