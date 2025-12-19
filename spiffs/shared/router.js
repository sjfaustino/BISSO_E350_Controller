/**
 * @file shared/router.js
 * @brief Client-side router for page navigation
 */

class Router {
    static routes = {
        'dashboard': { file: 'pages/dashboard/dashboard.html', js: 'pages/dashboard/dashboard.js' },
        'gcode': { file: 'pages/gcode/gcode.html', js: 'pages/gcode/gcode.js' },
        'motion': { file: 'pages/motion/motion.html', js: 'pages/motion/motion.js' },
        'diagnostics': { file: 'pages/diagnostics/diagnostics.html', js: 'pages/diagnostics/diagnostics.js' },
        'maintenance': { file: 'pages/maintenance/maintenance.html', js: 'pages/maintenance/maintenance.js' },
        'logs': { file: 'pages/logs/logs.html', js: 'pages/logs/logs.js' },
        'settings': { file: 'pages/settings/settings.html', js: 'pages/settings/settings.js' }
    };

    static currentPage = null;
    static currentModule = null;
    static isLoading = false;

    static init() {
        window.addEventListener('hashchange', () => this.navigate());
        this.navigate();
    }

    static async navigate(page = null) {
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
                } else {
                    // Generic fallback for other pages
                    html = `
                        <div style="padding: 40px 20px; text-align: center;">
                            <h2>📄 ${page.charAt(0).toUpperCase() + page.slice(1)} Page</h2>
                            <p style="color: var(--text-secondary); margin: 20px 0;">
                                ${isFileProtocol ? 'File mode' : 'Mock mode'} - loading with simulated data...
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
                    } else {
                        // Generic fallback for other pages
                        html = `
                            <div style="padding: 40px 20px; text-align: center;">
                                <h2>📄 ${page.charAt(0).toUpperCase() + page.slice(1)} Page</h2>
                                <p style="color: var(--text-secondary); margin: 20px 0;">
                                    Mock mode - loading with simulated data...
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
