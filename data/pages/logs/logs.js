/**
 * Logs Page Module
 * Tracks fault and operation history with filtering and export
 * Note: Use window.LogsModule to avoid "already declared" errors when navigating
 */
window.LogsModule = window.LogsModule || {
    // In-memory log buffer (max 10000 entries)
    logs: [],
    backendLogs: [],
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
        this.fetchBackendLogs();
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
        // Combine local and backend logs
        const allLogs = [...this.logs, ...this.backendLogs];

        // Sort by timestamp descending (newest first)
        allLogs.sort((a, b) => {
            if (a.timestamp < b.timestamp) return 1;
            if (a.timestamp > b.timestamp) return -1;
            return 0;
        });

        this.filteredLogs = allLogs.filter(log => {
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

        const levelFilter = document.getElementById('log-level-filter');
        const sourceFilter = document.getElementById('log-source-filter');
        const searchInput = document.getElementById('log-search');

        if (levelFilter) levelFilter.value = '';
        if (sourceFilter) sourceFilter.value = '';
        if (searchInput) searchInput.value = '';

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
        // use combined list if available (filteredLogs usually contains everything unless filtered)
        // Better: reconstruct full list for stats to show "Total" regardless of filter
        const allLogs = [...this.logs, ...this.backendLogs];

        const stats = {
            total: allLogs.length,
            critical: allLogs.filter(l => l.level === 'CRITICAL').length,
            error: allLogs.filter(l => l.level === 'ERROR').length,
            warn: allLogs.filter(l => l.level === 'WARN' || l.level === 'WARNING').length // Handle both conventions
        };

        const statTotalEl = document.getElementById('stat-total');
        const statCriticalEl = document.getElementById('stat-critical');
        const statErrorEl = document.getElementById('stat-error');
        const statWarningEl = document.getElementById('stat-warning');

        if (statTotalEl) statTotalEl.textContent = stats.total.toString();
        if (statCriticalEl) statCriticalEl.textContent = stats.critical.toString();
        if (statErrorEl) statErrorEl.textContent = stats.error.toString();
        if (statWarningEl) statWarningEl.textContent = stats.warn.toString();
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

    async clearLogs() {
        if (confirm('Clear all logs (Local and Device History)? This cannot be undone.')) {
            try {
                // Clear backend NVS logs - Use consistent /api/faults/clear POST endpoint
                const response = await fetch('/api/faults/clear', { method: 'POST' });
                if (!response.ok) throw new Error('Failed to clear device logs');

                // Clear local state
                this.logs = [];
                this.backendLogs = [];
                this.saveLogsToStorage();
                this.applyFilters();

                AlertManager.add('All logs cleared', 'info', 2000);
            } catch (e) {
                console.error('[Logs] Error clearing logs:', e);
                AlertManager.add('Failed to clear device logs', 'error', 3000);
            }
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

    async fetchBackendLogs() {
        try {
            // Get uptime to calculate absolute times
            const statusResp = await fetch('/api/network/status');
            const statusData = await statusResp.json();
            const uptime = statusData.uptime_ms || 0;
            const now = Date.now();

            // Get faults
            const faultResp = await fetch('/api/faults');
            const faultData = await faultResp.json();

            if (faultData.faults && Array.isArray(faultData.faults)) {
                this.backendLogs = faultData.faults.map(f => {
                    // Calculate approx absolute time: Now - (Uptime - FaultTime)
                    // If fault time > uptime (due to reboot?), clamp
                    const timeAgo = uptime - f.timestamp;
                    const logDate = new Date(now - timeAgo);
                    const timestamp = logDate.toISOString().replace('T', ' ').split('.')[0];

                    return {
                        timestamp: timestamp,
                        level: f.severity, // WARN, ERROR, CRITICAL
                        source: 'firmware',
                        message: `[${f.code}] ${f.description || f.message || ''}`
                    };
                });

                console.log(`[Logs] Fetched ${this.backendLogs.length} backend faults`);
                this.applyFilters();
            }
        } catch (e) {
            console.error('[Logs] Failed to fetch backend logs:', e);
        }
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
            this.addLog('INFO', 'system', 'Log Viewer Initialized');
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
