/**
 * @file shared/router.js
 * @brief Client-side router for page navigation
 */

console.log('[ROUTER] router.js loading...');

class Router {
    static routes = {
        'dashboard': { file: 'pages/dashboard/dashboard.html', js: 'pages/dashboard/dashboard.js' },
        'gcode': { file: 'pages/gcode/gcode.html', js: 'pages/gcode/gcode.js' },
        'motion': { file: 'pages/motion/motion.html', js: 'pages/motion/motion.js' },
        'diagnostics': { file: 'pages/diagnostics/diagnostics.html', js: 'pages/diagnostics/diagnostics.js' },
        'network': { file: 'pages/network/network.html', js: 'pages/network/network.js' },
        'system': { file: 'pages/system/system.html', js: 'pages/system/system.js' },
        'maintenance': { file: 'pages/maintenance/maintenance.html', js: 'pages/maintenance/maintenance.js' },
        'logs': { file: 'pages/logs/logs.html', js: 'pages/logs/logs.js' },
        'settings': { file: 'pages/settings/settings.html', js: 'pages/settings/settings.js' }
    };

    static currentPage = null;
    static currentModule = null;
    static isLoading = false;

    static init() {
        console.log('[ROUTER] Initializing router');
        window.addEventListener('hashchange', () => this.navigate());
        this.navigate();
    }

    static async navigate(page = null) {
        console.log('[ROUTER] Navigate called, page:', page, 'isLoading:', this.isLoading);
        page = page || window.location.hash.slice(1) || 'dashboard';

        if (!this.routes[page]) {
            console.warn(`[ROUTER] Unknown page: ${page}`);
            window.location.hash = '#dashboard';
            return;
        }

        if (this.isLoading) {
            console.log('[ROUTER] Already loading, skipping duplicate navigate request');
            return;
        }
        this.isLoading = true;

        try {
            // Cleanup previous page
            if (this.currentModule && this.currentModule.cleanup) {
                this.currentModule.cleanup();
            }

            const route = this.routes[page];
            const container = document.getElementById('page-container');

            // Detect file:// protocol - fetch will fail, so skip it and use direct script loading
            const isFileProtocol = window.location.protocol === 'file:';

            // Load HTML
            let html;
            let fetchFailed = false;

            // Skip fetch if file:// protocol or mock mode is enabled
            if (isFileProtocol || window.MockMode?.enabled) {
                fetchFailed = true;
                console.log(`[ROUTER] ${isFileProtocol ? 'File protocol detected' : 'Mock mode enabled'} - using fallback HTML for:`, page);

                // Generate fallback HTML structure for file:// or mock mode
                if (page === 'dashboard') {
                    html = `
                        <div class="dashboard-page">
                            <div style="padding: 20px 0;">
                                <h1>📊 Dashboard ${isFileProtocol ? '(File Mode)' : '(Mock Mode)'}</h1>
                                <p style="color: var(--text-secondary); font-size: 14px;">Simulated data${isFileProtocol ? '' : ' - press M to disable'}</p>
                            </div>

                            <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 20px 0;">
                                <div class="card">
                                    <div class="card-header"><h3>Status</h3></div>
                                    <div class="card-content">
                                        <div id="motion-status">--</div>
                                    </div>
                                </div>
                                <div class="card">
                                    <div class="card-header"><h3>CPU</h3></div>
                                    <div class="card-content">
                                        <div id="cpu-value" style="font-size: 24px;">--</div>
                                    </div>
                                </div>
                                <div class="card">
                                    <div class="card-header"><h3>Memory</h3></div>
                                    <div class="card-content">
                                        <div id="memory-value" style="font-size: 24px;">--</div>
                                    </div>
                                </div>
                                <div class="card">
                                    <div class="card-header"><h3>VFD</h3></div>
                                    <div class="card-content">
                                        <div id="vfd-status">--</div>
                                    </div>
                                </div>
                            </div>

                            <div id="charts-section" style="margin-top: 30px;"></div>
                        </div>
                    `;
                } else if (page === 'gcode') {
                    html = `
                        <div class="gcode-page">
                            <div class="card">
                                <div class="card-header"><h2>G-code Command Input (File Mode)</h2></div>
                                <div class="card-content">
                                    <p style="color: var(--text-secondary); margin-bottom: 15px;">G-code execution requires connection to the ESP32 device. This interface is view-only in file mode.</p>
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
                } else if (page === 'motion') {
                    html = `
                        <div class="motion-page">
                            <div class="card">
                                <div class="card-header"><h2>Motion Control (File Mode)</h2></div>
                                <div class="card-content">
                                    <p style="color: var(--text-secondary);">Motion control requires connection to the ESP32 device. View-only in file mode.</p>
                                    <div style="margin-top: 20px;">
                                        <h3>Current Position</h3>
                                        <div style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin: 15px 0;">
                                            <div class="position-display">
                                                <div style="color: var(--text-secondary); font-size: 12px;">X Axis</div>
                                                <div id="pos-x" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                            </div>
                                            <div class="position-display">
                                                <div style="color: var(--text-secondary); font-size: 12px;">Y Axis</div>
                                                <div id="pos-y" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                            </div>
                                            <div class="position-display">
                                                <div style="color: var(--text-secondary); font-size: 12px;">Z Axis</div>
                                                <div id="pos-z" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                            </div>
                                            <div class="position-display">
                                                <div style="color: var(--text-secondary); font-size: 12px;">A Axis</div>
                                                <div id="pos-a" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    `;
                } else if (page === 'diagnostics') {
                    html = `
                        <div class="diagnostics-page">
                            <div class="card">
                                <div class="card-header"><h2>System Diagnostics (File Mode)</h2></div>
                                <div class="card-content">
                                    <p style="color: var(--text-secondary); margin-bottom: 15px;">Live diagnostics require connection to the ESP32 device.</p>
                                    <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                        <h3>Status</h3>
                                        <div id="system-status">System: Offline (File Mode)</div>
                                        <div id="motion-system">Motion: Not Connected</div>
                                        <div id="vfd-system">VFD: Not Connected</div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    `;
                } else if (page === 'network') {
                    html = `
                        <div class="network-page">
                            <div class="card">
                                <div class="card-header"><h2>Network Status (File Mode)</h2></div>
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
                } else if (page === 'system') {
                    html = `
                        <div class="system-page">
                            <div class="card">
                                <div class="card-header"><h2>System Information (File Mode)</h2></div>
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
                } else if (page === 'maintenance') {
                    html = `
                        <div class="maintenance-page">
                            <div class="card">
                                <div class="card-header"><h2>Maintenance (File Mode)</h2></div>
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
                } else if (page === 'logs') {
                    html = `
                        <div class="logs-page">
                            <div class="card">
                                <div class="card-header"><h2>System Logs (File Mode)</h2></div>
                                <div class="card-content">
                                    <p style="color: var(--text-secondary); margin-bottom: 15px;">Log viewing requires connection to the ESP32 device.</p>
                                    <div id="log-container" style="padding: 20px; background: var(--bg-secondary); border-radius: 4px; font-family: monospace; max-height: 400px; overflow-y: auto;">
                                        <div>No logs available in file mode</div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    `;
                } else if (page === 'settings') {
                    html = `
                        <div class="settings-page">
                            <div class="card">
                                <div class="card-header"><h2>Settings (File Mode)</h2></div>
                                <div class="card-content">
                                    <p style="color: var(--text-secondary); margin-bottom: 15px;">Settings configuration requires connection to the ESP32 device.</p>
                                    <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                        <h3>Display Settings</h3>
                                        <div style="margin: 10px 0;">
                                            <label>Theme: <select disabled><option>Light</option></select></label>
                                        </div>
                                        <div style="margin: 10px 0;">
                                            <label>Font Size: <input type="range" disabled /></label>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        </div>
                    `;
                } else {
                    // Generic fallback for any other pages
                    html = `
                        <div style="padding: 40px 20px; text-align: center;">
                            <h2>📄 ${page.charAt(0).toUpperCase() + page.slice(1)} Page</h2>
                            <p style="color: var(--text-secondary); margin: 20px 0;">
                                ${isFileProtocol ? 'File mode' : 'Mock mode'} - This page requires connection to the ESP32 device.
                            </p>
                        </div>
                    `;
                }
            } else {
                try {
                    const htmlResponse = await fetch(route.file);
                    if (!htmlResponse.ok) throw new Error(`HTTP ${htmlResponse.status}`);
                    html = await htmlResponse.text();
                } catch (fetchError) {
                    fetchFailed = true;
                    console.warn(`[ROUTER] Failed to fetch ${route.file}:`, fetchError.message);

                // If mock mode is enabled after fetch failed, create minimal HTML with basic structure
                // The JS module will populate it with mock data
                if (window.MockMode?.enabled) {
                    console.log('[ROUTER] Mock mode enabled after fetch failure, generating fallback structure for:', page);

                    // Generate page-specific fallback structures
                    if (page === 'dashboard') {
                        html = `
                            <div class="dashboard-page">
                                <div style="padding: 20px 0;">
                                    <h1>📊 Dashboard (Mock Mode)</h1>
                                    <p style="color: var(--text-secondary); font-size: 14px;">Simulated data - press M to disable</p>
                                </div>

                                <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 20px 0;">
                                    <div class="card">
                                        <div class="card-header"><h3>Status</h3></div>
                                        <div class="card-content">
                                            <div id="motion-status">--</div>
                                        </div>
                                    </div>
                                    <div class="card">
                                        <div class="card-header"><h3>CPU</h3></div>
                                        <div class="card-content">
                                            <div id="cpu-value" style="font-size: 24px;">--</div>
                                        </div>
                                    </div>
                                    <div class="card">
                                        <div class="card-header"><h3>Memory</h3></div>
                                        <div class="card-content">
                                            <div id="memory-value" style="font-size: 24px;">--</div>
                                        </div>
                                    </div>
                                    <div class="card">
                                        <div class="card-header"><h3>VFD</h3></div>
                                        <div class="card-content">
                                            <div id="vfd-status">--</div>
                                        </div>
                                    </div>
                                </div>

                                <div id="charts-section" style="margin-top: 30px;"></div>
                            </div>
                        `;
                    } else if (page === 'gcode') {
                        html = `
                            <div class="gcode-page">
                                <div class="card">
                                    <div class="card-header"><h2>G-code Command Input (Mock Mode)</h2></div>
                                    <div class="card-content">
                                        <p style="color: var(--text-secondary); margin-bottom: 15px;">Mock mode - interface view-only.</p>
                                        <textarea id="gcode-input" placeholder="Enter G-code commands..." style="width: 100%; min-height: 150px; padding: 10px; font-family: monospace; background: var(--bg-secondary); border: 1px solid var(--border-color); border-radius: 4px;" readonly></textarea>
                                        <div style="margin-top: 10px;">
                                            <button class="btn btn-primary" disabled>▶️ Execute Command (Mock Mode)</button>
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
                    } else if (page === 'motion') {
                        html = `
                            <div class="motion-page">
                                <div class="card">
                                    <div class="card-header"><h2>Motion Control (Mock Mode)</h2></div>
                                    <div class="card-content">
                                        <p style="color: var(--text-secondary);">Mock mode - view-only.</p>
                                        <div style="margin-top: 20px;">
                                            <h3>Current Position</h3>
                                            <div style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin: 15px 0;">
                                                <div class="position-display">
                                                    <div style="color: var(--text-secondary); font-size: 12px;">X Axis</div>
                                                    <div id="pos-x" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                                </div>
                                                <div class="position-display">
                                                    <div style="color: var(--text-secondary); font-size: 12px;">Y Axis</div>
                                                    <div id="pos-y" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                                </div>
                                                <div class="position-display">
                                                    <div style="color: var(--text-secondary); font-size: 12px;">Z Axis</div>
                                                    <div id="pos-z" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                                </div>
                                                <div class="position-display">
                                                    <div style="color: var(--text-secondary); font-size: 12px;">A Axis</div>
                                                    <div id="pos-a" style="font-size: 20px; font-weight: bold;">0.00 mm</div>
                                                </div>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        `;
                    } else if (page === 'diagnostics') {
                        html = `
                            <div class="diagnostics-page">
                                <div class="card">
                                    <div class="card-header"><h2>System Diagnostics (Mock Mode)</h2></div>
                                    <div class="card-content">
                                        <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                            <h3>Status</h3>
                                            <div id="system-status">System: Mock Mode</div>
                                            <div id="motion-system">Motion: Simulated</div>
                                            <div id="vfd-system">VFD: Simulated</div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        `;
                    } else if (page === 'network') {
                        html = `
                            <div class="network-page">
                                <div class="card">
                                    <div class="card-header"><h2>Network Status (Mock Mode)</h2></div>
                                    <div class="card-content">
                                        <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                            <div id="wifi-status">WiFi: Simulated</div>
                                            <div id="wifi-ssid">SSID: --</div>
                                            <div id="signal-dbm">Signal: -- dBm</div>
                                            <div id="signal-quality">Quality: --</div>
                                            <div id="latency-ms" style="margin-top: 10px;">Latency: -- ms</div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        `;
                    } else if (page === 'system') {
                        html = `
                            <div class="system-page">
                                <div class="card">
                                    <div class="card-header"><h2>System Information (Mock Mode)</h2></div>
                                    <div class="card-content">
                                        <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                            <h3>Device Info</h3>
                                            <div>Firmware: Simulated</div>
                                            <div>Hardware: ESP32-WROOM-32E</div>
                                            <div>Uptime: --</div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        `;
                    } else if (page === 'maintenance') {
                        html = `
                            <div class="maintenance-page">
                                <div class="card">
                                    <div class="card-header"><h2>Maintenance (Mock Mode)</h2></div>
                                    <div class="card-content">
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
                    } else if (page === 'logs') {
                        html = `
                            <div class="logs-page">
                                <div class="card">
                                    <div class="card-header"><h2>System Logs (Mock Mode)</h2></div>
                                    <div class="card-content">
                                        <div id="log-container" style="padding: 20px; background: var(--bg-secondary); border-radius: 4px; font-family: monospace; max-height: 400px; overflow-y: auto;">
                                            <div>Mock mode - no logs available</div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        `;
                    } else if (page === 'settings') {
                        html = `
                            <div class="settings-page">
                                <div class="card">
                                    <div class="card-header"><h2>Settings (Mock Mode)</h2></div>
                                    <div class="card-content">
                                        <div style="padding: 20px; background: var(--bg-secondary); border-radius: 4px;">
                                            <h3>Display Settings</h3>
                                            <div style="margin: 10px 0;">
                                                <label>Theme: <select disabled><option>Light</option></select></label>
                                            </div>
                                            <div style="margin: 10px 0;">
                                                <label>Font Size: <input type="range" disabled /></label>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                            </div>
                        `;
                    } else {
                        // Generic fallback for any other pages
                        html = `
                            <div style="padding: 40px 20px; text-align: center;">
                                <h2>📄 ${page.charAt(0).toUpperCase() + page.slice(1)} Page</h2>
                                <p style="color: var(--text-secondary); margin: 20px 0;">
                                    Mock mode - This page requires connection to the ESP32 device.
                                </p>
                            </div>
                        `;
                    }
                    // Don't return - continue to load the JS module below
                } else if (!navigator.onLine) {
                    // Offline but mock mode not enabled - show helpful message
                    html = `
                        <div style="padding: 40px 20px; text-align: center;">
                            <h2>📡 Offline Mode</h2>
                            <p style="color: var(--text-secondary); margin: 20px 0;">
                                Cannot load page content while offline.
                            </p>
                            <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                                <p style="margin: 10px 0; font-size: 14px;">
                                    <strong>Mock Mode:</strong>
                                    ✗ Disabled
                                </p>
                                <p style="margin: 10px 0; font-size: 14px;">
                                    <strong>Network:</strong>
                                    ✗ Offline
                                </p>
                            </div>
                            <p style="color: var(--text-secondary); margin-top: 20px; font-size: 14px;">
                                Enable mock mode with <strong>M</strong> key to preview dashboard with simulated data.
                            </p>
                        </div>
                    `;
                    container.innerHTML = html;
                    this.isLoading = false;
                    return;
                } else {
                    throw fetchError;
                }
                }  // Close catch block
            }  // Close else block

            console.log('[ROUTER] Setting page content, HTML length:', html?.length || 0);
            container.innerHTML = html;
            console.log('[ROUTER] Page content set successfully');

            // Load CSS if exists (but don't fail if it doesn't)
            if (!fetchFailed) {
                const cssFile = route.file.replace('.html', '.css');
                this.loadCSS(cssFile).catch(() => {
                    // CSS not found, that's okay
                });
            }

            // Load JS module (this is what populates the page content)
            const script = document.createElement('script');
            script.src = route.js;
            script.onload = () => {
                this.currentPage = page;
                this.currentModule = window.currentPageModule || {};

                // Call init if exists
                if (this.currentModule.init) {
                    this.currentModule.init();
                }

                // Update nav
                this.updateNav(page);

                this.isLoading = false;
            };
            script.onerror = () => {
                console.error(`[ROUTER] Failed to load ${route.js}`);

                // If JS also fails and we're offline, show error
                if (fetchFailed && !navigator.onLine) {
                    container.innerHTML = `
                        <div style="padding: 40px 20px; text-align: center; color: var(--color-critical);">
                            <h2>❌ Error Loading Page</h2>
                            <p>Could not load page module and content is not available offline.</p>
                            <p style="font-size: 14px; margin-top: 20px;">
                                Enable <strong>Mock Mode</strong> (press <strong>M</strong>) to use simulated data.
                            </p>
                        </div>
                    `;
                }
                this.isLoading = false;
            };
            document.body.appendChild(script);

        } catch (error) {
            console.error('[ROUTER] Navigation error:', error);
            const container = document.getElementById('page-container');
            container.innerHTML = `
                <div style="padding: 40px 20px; text-align: center; color: var(--color-critical);">
                    <h2>❌ Error Loading Page</h2>
                    <p>${error.message}</p>
                    <p style="font-size: 14px; margin-top: 20px;">
                        Make sure the device is online or enable <strong>Mock Mode</strong> (press <strong>M</strong>)
                    </p>
                </div>
            `;
            this.isLoading = false;
        }
    }

    static loadCSS(href) {
        return new Promise((resolve, reject) => {
            // Check if already loaded
            if (document.querySelector(`link[href="${href}"]`)) {
                resolve();
                return;
            }

            const link = document.createElement('link');
            link.rel = 'stylesheet';
            link.href = href;
            link.onload = resolve;
            link.onerror = reject;
            document.head.appendChild(link);
        });
    }

    static updateNav(page) {
        document.querySelectorAll('.nav-item').forEach(item => {
            const href = item.getAttribute('href').slice(1);
            item.classList.toggle('active', href === page);
        });
    }

    static go(page) {
        window.location.hash = '#' + page;
    }
}

// Expose Router globally
window.Router = Router;

console.log('[ROUTER] Router class defined, typeof Router:', typeof Router);
console.log('[ROUTER] Router attached to window, typeof window.Router:', typeof window.Router);
