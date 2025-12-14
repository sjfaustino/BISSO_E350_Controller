/**
 * @file shared/router.js
 * @brief Client-side router for page navigation
 */

class Router {
    static routes = {
        'dashboard': { file: 'pages/dashboard/dashboard.html', js: 'pages/dashboard/dashboard.js' },
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

        if (this.isLoading) return;
        this.isLoading = true;

        try {
            // Cleanup previous page
            if (this.currentModule && this.currentModule.cleanup) {
                this.currentModule.cleanup();
            }

            const route = this.routes[page];
            const container = document.getElementById('page-container');

            // Load HTML
            let html;
            try {
                const htmlResponse = await fetch(route.file);
                if (!htmlResponse.ok) throw new Error(`HTTP ${htmlResponse.status}`);
                html = await htmlResponse.text();
            } catch (fetchError) {
                console.warn(`[ROUTER] Failed to fetch ${route.file}:`, fetchError.message);

                // If fetch fails, check if we're in mock mode or offline
                if (window.MockMode?.enabled || !navigator.onLine) {
                    // Provide fallback content for offline mode
                    html = `
                        <div style="padding: 40px 20px; text-align: center;">
                            <h2>📡 Offline Mode</h2>
                            <p style="color: var(--text-secondary); margin: 20px 0;">
                                Cannot load page content while offline.
                            </p>
                            <div style="background: var(--bg-secondary); padding: 20px; border-radius: 8px; margin: 20px 0;">
                                <p style="margin: 10px 0; font-size: 14px;">
                                    <strong>Mock Mode:</strong>
                                    ${window.MockMode?.enabled ? '✓ Active' : '✗ Disabled'}
                                </p>
                                <p style="margin: 10px 0; font-size: 14px;">
                                    <strong>Network:</strong>
                                    ${navigator.onLine ? '✓ Online' : '✗ Offline'}
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
                }

                throw fetchError;
            }

            container.innerHTML = html;

            // Load CSS if exists
            const cssFile = route.file.replace('.html', '.css');
            this.loadCSS(cssFile).catch(() => {
                // CSS not found, that's okay
            });

            // Load JS
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

// Auto-init on load
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => Router.init());
} else {
    Router.init();
}
