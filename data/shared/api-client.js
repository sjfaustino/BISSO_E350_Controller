/**
 * @file api-client.js
 * @brief Centralized API configuration and request handling
 */

(function (window) {
    class ApiClient {
        constructor() {
            this.baseUrl = '/api';
            this.defaultTimeout = 10000; // 10s
        }

        /**
         * Generic fetch wrapper with timeout, error handling, and optional UI spinner
         * @param {string} endpoint - API endpoint (relative to /api, or absolute if starts with /)
         * @param {object} options - Fetch options
         * @param {string|HTMLElement} [spinnerTarget] - ID or Element to show spinner on
         * @returns {Promise<any>} - JSON response or throws Error
         */
        async request(endpoint, options = {}, spinnerTarget = null) {
            const url = endpoint.startsWith('http') || endpoint.startsWith('/') ? endpoint : `${this.baseUrl}/${endpoint}`;

            // Setup AbortController for timeout
            const controller = new AbortController();
            const id = setTimeout(() => controller.abort(), this.defaultTimeout);
            options.signal = controller.signal;

            // Manage Spinner
            let stopSpinner = null;
            if (spinnerTarget && window.UI) {
                stopSpinner = window.UI.showSpinner(spinnerTarget, 'Loading...');
            }

            try {
                const response = await fetch(url, options);
                clearTimeout(id);

                // Handle HTTP Errors
                if (!response.ok) {
                    let errorMsg = `HTTP Error ${response.status}`;
                    try {
                        const errData = await response.json();
                        errorMsg = errData.error || errData.message || errorMsg;
                    } catch (e) { /* ignore JSON parse error on error response */ }
                    throw new Error(errorMsg);
                }

                // Handle JSON Response (Safe-Parsing if header is wrong)
                const contentType = response.headers.get("content-type");
                const text = await response.text();

                if (contentType && contentType.includes("application/json")) {
                    try {
                        return JSON.parse(text);
                    } catch (e) {
                        return text;
                    }
                }

                // If it looks like JSON, try to parse it anyway (robustness against bad headers)
                if (text.trim().startsWith('{') || text.trim().startsWith('[')) {
                    try {
                        return JSON.parse(text);
                    } catch (e) {
                        return text;
                    }
                }

                return text;

            } catch (error) {
                clearTimeout(id);
                let msg = error.name === 'AbortError' ? 'Request timed out' : error.message;

                // Show Alert
                if (window.AlertManager && !options.silent) {
                    window.AlertManager.add(msg, 'error');
                } else if (!window.AlertManager) {
                    console.error('[API]', msg);
                }
                throw error;

            } finally {
                if (stopSpinner) stopSpinner();
            }
        }

        /**
         * GET Request
         * @param {string} endpoint 
         * @param {string|HTMLElement} [spinnerTarget]
         */
        async get(endpoint, spinnerTarget = null) {
            return this.request(endpoint, { method: 'GET' }, spinnerTarget);
        }

        /**
         * POST Request (JSON)
         * @param {string} endpoint 
         * @param {object} data 
         * @param {string|HTMLElement} [spinnerTarget]
         */
        async post(endpoint, data, spinnerTarget = null) {
            return this.request(endpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(data)
            }, spinnerTarget);
        }

        /**
         * DELETE Request
         * @param {string} endpoint 
         * @param {string|HTMLElement} [spinnerTarget]
         */
        async delete(endpoint, spinnerTarget = null) {
            return this.request(endpoint, { method: 'DELETE' }, spinnerTarget);
        }
    }

    // Expose globally
    window.API = new ApiClient();
    console.log('[ApiClient] Initialized');

})(window);
