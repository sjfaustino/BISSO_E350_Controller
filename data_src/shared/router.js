/**
 * @file shared/router.js
 * @brief Client-side router for page navigation
 * @description Refactored to use FallbackPages for fallback HTML templates
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
        'hardware': { file: 'pages/hardware/hardware.html', js: 'pages/hardware/hardware.js' },
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

                // Use FallbackPages module for fallback HTML
                html = window.FallbackPages?.getPageHTML(page, isFileProtocol) ||
                    `<div style="padding: 40px 20px; text-align: center;"><h2>${page} (Fallback)</h2></div>`;
            } else {
                try {
                    const htmlResponse = await fetch(route.file);
                    if (!htmlResponse.ok) throw new Error(`HTTP ${htmlResponse.status}`);
                    html = await htmlResponse.text();
                } catch (fetchError) {
                    fetchFailed = true;
                    console.warn(`[ROUTER] Failed to fetch ${route.file}:`, fetchError.message);

                    // If mock mode is enabled after fetch failed, use fallback HTML
                    if (window.MockMode?.enabled) {
                        console.log('[ROUTER] Mock mode enabled after fetch failure, generating fallback structure for:', page);
                        html = window.FallbackPages?.getPageHTML(page, false) ||
                            `<div style="padding: 40px 20px; text-align: center;"><h2>${page} (Mock)</h2></div>`;
                    } else if (!navigator.onLine) {
                        // Offline but mock mode not enabled - show helpful message
                        html = window.FallbackPages?.getOfflineHTML() ||
                            `<div style="padding: 40px 20px; text-align: center;"><h2>Offline</h2></div>`;
                        container.innerHTML = html;
                        this.isLoading = false;
                        return;
                    } else {
                        throw fetchError;
                    }
                }
            }

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
                    container.innerHTML = window.FallbackPages?.getErrorHTML('Could not load page module') ||
                        '<div style="color: red;">Error loading page</div>';
                }
                this.isLoading = false;
            };
            document.body.appendChild(script);

        } catch (error) {
            console.error('[ROUTER] Navigation error:', error);
            const container = document.getElementById('page-container');
            container.innerHTML = window.FallbackPages?.getErrorHTML(error.message) ||
                `<div style="color: red;">Error: ${error.message}</div>`;
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
