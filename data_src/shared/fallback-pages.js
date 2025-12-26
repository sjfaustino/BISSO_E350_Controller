/**
 * @file shared/fallback-pages.js
 * @brief Fallback HTML templates for file:// and mock mode
 * @description Extracted from router.js to reduce file size and improve maintainability
 */

const FallbackPages = {
    /**
     * Get fallback HTML for a page when running in file:// or mock mode
     * @param {string} page - Page name
     * @param {boolean} isFileProtocol - True if running from file://
     * @returns {string} HTML content
     */
    getPageHTML(page, isFileProtocol = false) {
        const mode = isFileProtocol ? 'File Mode' : 'Mock Mode';
        const tip = isFileProtocol ? '' : ' - press M to disable';

        switch (page) {
            case 'dashboard':
                return `
                    <div class="dashboard-page">
                        <div style="padding: 20px 0;">
                            <h1>📊 Dashboard (${mode})</h1>
                            <p style="color: var(--text-secondary); font-size: 14px;">Simulated data${tip}</p>
                        </div>
                        <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 20px 0;">
                            <div class="card">
                                <div class="card-header"><h3>Status</h3></div>
                                <div class="card-content"><div id="motion-status">--</div></div>
                            </div>
                            <div class="card">
                                <div class="card-header"><h3>CPU</h3></div>
                                <div class="card-content"><div id="cpu-value" style="font-size: 24px;">--</div></div>
                            </div>
                            <div class="card">
                                <div class="card-header"><h3>Memory</h3></div>
                                <div class="card-content"><div id="memory-value" style="font-size: 24px;">--</div></div>
                            </div>
                            <div class="card">
                                <div class="card-header"><h3>VFD</h3></div>
                                <div class="card-content"><div id="vfd-status">--</div></div>
                            </div>
                        </div>
                        <div id="charts-section" style="margin-top: 30px;"></div>
                    </div>
                `;

            case 'gcode':
                return `
                    <div class="gcode-page">
                        <div class="card">
                            <div class="card-header"><h2>G-code Command Input (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">G-code execution requires connection to the ESP32 device.</p>
                                <textarea id="gcode-input" placeholder="Enter G-code commands..." style="width: 100%; min-height: 150px; padding: 10px; font-family: monospace; background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 4px;" readonly></textarea>
                                <div style="margin-top: 10px;">
                                    <button class="btn btn-primary" disabled>▶️ Execute Command (Requires Device)</button>
                                </div>
                            </div>
                        </div>
                        <div class="card">
                            <div class="card-header"><h2>Parser State</h2></div>
                            <div class="card-content">
                                <div id="distance-mode">Distance Mode: G90 (Absolute)</div>
                                <div id="work-coordinate">Work Coordinate: G54</div>
                                <div id="feed-rate">Feed Rate: -- mm/min</div>
                                <div id="motion-state">Motion State: Idle</div>
                            </div>
                        </div>
                    </div>
                `;

            case 'motion':
                return `
                    <div class="motion-page">
                        <div class="card">
                            <div class="card-header"><h2>Motion Control (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary);">Motion control requires connection to the ESP32 device.</p>
                                <div style="margin-top: 20px;">
                                    <h3>Current Position</h3>
                                    <div style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin: 15px 0;">
                                        ${['X', 'Y', 'Z', 'A'].map(axis => `
                                            <div class="position-display">
                                                <div style="color: var(--text-secondary); font-size: 12px;">${axis} Axis</div>
                                                <div id="pos-${axis.toLowerCase()}" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                            </div>
                                        `).join('')}
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'diagnostics':
                return `
                    <div class="diagnostics-page">
                        <div class="card">
                            <div class="card-header"><h2>System Diagnostics (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">Live diagnostics require connection to the ESP32 device.</p>
                                <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                    <h3>Status</h3>
                                    <div id="system-status">System: ${mode}</div>
                                    <div id="motion-system">Motion: Not Connected</div>
                                    <div id="vfd-system">VFD: Not Connected</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'network':
                return `
                    <div class="network-page">
                        <div class="card">
                            <div class="card-header"><h2>Network Status (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">Network information requires connection to the ESP32 device.</p>
                                <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                    <div id="wifi-status">WiFi: Not Connected</div>
                                    <div id="wifi-ssid">SSID: --</div>
                                    <div id="signal-dbm">Signal: -- dBm</div>
                                    <div id="signal-quality">Quality: --</div>
                                    <div id="latency-ms" style="margin-top: 10px;">Latency: -- ms</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'system':
                return `
                    <div class="system-page">
                        <div class="card">
                            <div class="card-header"><h2>System Information (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">System information requires connection to the ESP32 device.</p>
                                <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                    <h3>Device Info</h3>
                                    <div>Firmware: Not Connected</div>
                                    <div>Hardware: ESP32-WROOM-32E</div>
                                    <div>Uptime: --</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'maintenance':
                return `
                    <div class="maintenance-page">
                        <div class="card">
                            <div class="card-header"><h2>Maintenance (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">Maintenance tracking requires connection to the ESP32 device.</p>
                                <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                    <h3>Component Status</h3>
                                    <div>Motor Hours: --</div>
                                    <div>VFD Runtime: --</div>
                                    <div>Last Service: --</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'logs':
                return `
                    <div class="logs-page">
                        <div class="card">
                            <div class="card-header"><h2>System Logs (${mode})</h2></div>
                            <div class="card-content">
                                <p style="color: var(--text-secondary); margin-bottom: 15px;">Log viewing requires connection to the ESP32 device.</p>
                                <div id="log-container" style="padding: 20px; background: var(--bg-secondary); border-radius: 4px; font-family: monospace; max-height: 400px; overflow-y: auto;">
                                    <div>No logs available in ${mode.toLowerCase()}</div>
                                </div>
                            </div>
                        </div>
                    </div>
                `;

            case 'settings':
                return `
                    <div class="settings-page">
                        <div class="card">
                            <div class="card-header"><h2>⚙️ Display & Theme Settings</h2></div>
                            <div class="card-content">
                                <div class="setting-group">
                                    <label>Theme:</label>
                                    <div class="theme-selector" style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin: 15px 0;">
                                        ${['light', 'dark', 'high-contrast', 'colorblind'].map(theme => `
                                            <button class="theme-option" data-theme="${theme}" title="${theme} Theme" style="display: flex; flex-direction: column; align-items: center; gap: 8px; padding: 12px; border: 2px solid var(--border-color); border-radius: 8px; background: var(--bg-primary); cursor: pointer;">
                                                <div class="theme-preview ${theme}" style="width: 60px; height: 60px; border-radius: 8px; background: ${this.getThemeGradient(theme)}; border: 1px solid var(--border-color);"></div>
                                                <span style="font-size: 12px;">${theme.charAt(0).toUpperCase() + theme.slice(1).replace('-', ' ')}</span>
                                            </button>
                                        `).join('')}
                                    </div>
                                </div>
                                <div class="setting-group" style="margin-top: 20px;">
                                    <label>Font Size: <span id="font-size-display">100</span>%</label>
                                    <div class="slider-container" style="margin: 10px 0;">
                                        <input type="range" id="font-size-slider" min="80" max="120" value="100" class="slider" style="width: 100%;">
                                        <div class="slider-labels" style="display: flex; justify-content: space-between; font-size: 12px; color: var(--text-secondary); margin-top: 5px;">
                                            <span>Smaller</span>
                                            <span>Normal</span>
                                            <span>Larger</span>
                                        </div>
                                    </div>
                                </div>
                                <p style="color: var(--text-secondary); margin-top: 20px; font-size: 14px;">
                                    💡 Tip: Press <strong>T</strong> key to quickly cycle through themes
                                </p>
                            </div>
                        </div>
                    </div>
                `;

            default:
                return `
                    <div style="padding: 40px 20px; text-align: center;">
                        <h2>📄 ${page.charAt(0).toUpperCase() + page.slice(1)} Page</h2>
                        <p style="color: var(--text-secondary); margin: 20px 0;">
                            ${mode} - This page requires connection to the ESP32 device.
                        </p>
                    </div>
                `;
        }
    },

    /**
     * Get theme gradient for preview
     */
    getThemeGradient(theme) {
        const gradients = {
            'light': 'linear-gradient(135deg, #ffffff 0%, #f8fafc 100%)',
            'dark': 'linear-gradient(135deg, #1e293b 0%, #0f172a 100%)',
            'high-contrast': 'linear-gradient(135deg, #000000 0%, #ffffff 100%)',
            'colorblind': 'linear-gradient(135deg, #0173b2 0%, #de8f05 100%)'
        };
        return gradients[theme] || gradients.light;
    },

    /**
     * Get offline mode HTML
     */
    getOfflineHTML() {
        return `
            <div style="padding: 40px 20px; text-align: center;">
                <h2>📡 Offline Mode</h2>
                <p style="color: var(--text-secondary); margin: 20px 0;">
                    Cannot load page content while offline.
                </p>
                <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                    <p style="margin: 10px 0; font-size: 14px;">
                        <strong>Mock Mode:</strong> ✗ Disabled
                    </p>
                    <p style="margin: 10px 0; font-size: 14px;">
                        <strong>Network:</strong> ✗ Offline
                    </p>
                </div>
                <p style="color: var(--text-secondary); margin-top: 20px; font-size: 14px;">
                    Enable mock mode with <strong>M</strong> key to preview dashboard with simulated data.
                </p>
            </div>
        `;
    },

    /**
     * Get error page HTML
     */
    getErrorHTML(message) {
        return `
            <div style="padding: 40px 20px; text-align: center; color: var(--color-critical);">
                <h2>❌ Error Loading Page</h2>
                <p>${message || 'An error occurred'}</p>
                <p style="font-size: 14px; margin-top: 20px;">
                    Make sure the device is online or enable <strong>Mock Mode</strong> (press <strong>M</strong>)
                </p>
            </div>
        `;
    }
};

// Expose globally
window.FallbackPages = FallbackPages;
