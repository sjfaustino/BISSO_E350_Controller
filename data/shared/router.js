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

        // Check for firmware updates on load
        setTimeout(() => this.checkForUpdates(), 2000);
    }

    static async checkForUpdates() {
        const banner = document.getElementById('ota-update-banner');
        if (!banner) return;

        try {
            const response = await fetch('/api/ota/check');
            const data = await response.json();

            // If background check hasn't completed yet, retry in 3 seconds
            if (!data.check_complete) {
                console.log('[OTA] Background check not complete, retrying...');
                setTimeout(() => this.checkForUpdates(), 3000);
                return;
            }

            if (data.available) {
                this.showUpdateBanner(data);
            } else {
                console.log('[OTA] Firmware is up to date');
            }
        } catch (err) {
            console.warn('[OTA] Failed to check for updates:', err);
        }
    }

    static showUpdateBanner(data) {
        const banner = document.getElementById('ota-update-banner');
        if (!banner) return;

        banner.innerHTML = `
            <div class="ota-content">
                <span class="ota-icon">🚀</span>
                <div class="ota-text">
                    New version <strong>${data.latest_version}</strong> is available!
                </div>
            </div>
            <div class="ota-actions">
                <button class="ota-btn ota-btn-update" id="ota-install-btn">Install Now</button>
                <button class="ota-btn ota-btn-dismiss" id="ota-dismiss-btn">Later</button>
            </div>
        `;
        banner.classList.remove('hidden');

        document.getElementById('ota-dismiss-btn').onclick = () => {
            banner.classList.add('hidden');
        };

        document.getElementById('ota-install-btn').onclick = () => {
            this.startUpdate(data.url);
        };
    }

    static async startUpdate(url) {
        const banner = document.getElementById('ota-update-banner');
        if (!banner) return;

        if (!confirm('The device will reboot after updating. Proceed?')) return;

        try {
            const response = await fetch('/api/ota/update', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ url: url })
            });
            const data = await response.json();

            if (data.success) {
                banner.innerHTML = `
                    <div class="ota-content">
                        <span class="ota-icon">📥</span>
                        <div class="ota-text">Downloading update... do not power off</div>
                        <div class="ota-progress-container">
                            <div class="ota-progress-bar" id="ota-progress" style="width: 10%"></div>
                        </div>
                    </div>
                `;
                this.pollUpdateStatus();
            } else {
                window.Toast?.error(data.error || 'Update failed to start');
            }
        } catch (err) {
            window.Toast?.error('Communication error during update');
        }
    }

    static async pollUpdateStatus() {
        const progressBar = document.getElementById('ota-progress');
        const interval = setInterval(async () => {
            try {
                const response = await fetch('/api/ota/status');
                const data = await response.json();

                if (data.updating) {
                    if (progressBar) progressBar.style.width = `${Math.max(10, data.progress)}%`;
                } else {
                    clearInterval(interval);
                    // If no longer updating, either it failed or it's rebooting.
                    // The backend reboots 2s after success, so if we can still poll, it might have failed.
                }
            } catch (err) {
                // Connection lost usually means it's rebooting
                clearInterval(interval);
                document.getElementById('ota-update-banner').innerHTML = `
                    <div class="ota-content">
                        <span class="ota-icon">🔄</span>
                        <div class="ota-text">Update complete! Reconnecting...</div>
                    </div>
                `;
                setTimeout(() => window.location.reload(), 5000);
            }
        }, 1000);
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

            // Skip fetch if file:// protocol or offline mode is enabled
            if (isFileProtocol || false) {
                fetchFailed = true;
                console.log(`[ROUTER] ${isFileProtocol ? 'File protocol detected' : 'offline mode enabled'} - using fallback HTML for:`, page);

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

                    // If offline mode is enabled after fetch failed, use fallback HTML
                    if (false) {
                        console.log('[ROUTER] offline mode enabled after fetch failure, generating fallback structure for:', page);
                        html = window.FallbackPages?.getPageHTML(page, false) ||
                            `<div style="padding: 40px 20px; text-align: center;"><h2>${page} (Mock)</h2></div>`;
                    } else if (!navigator.onLine) {
                        // Offline but offline mode not enabled - show helpful message
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


