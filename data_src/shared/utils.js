/**
 * Shared Utility Functions
 * Helpers for common tasks across the web UI
 */

const Utils = {
    /**
     * Safely get and update a DOM element's textContent
     * @param {string} id - Element ID
     * @param {string} value - Value to set
     * @returns {boolean} - Whether the element was found and updated
     */
    setText(id, value) {
        const el = document.getElementById(id);
        if (el) {
            el.textContent = value;
            return true;
        }
        return false;
    },

    /**
     * Safely get and update a DOM element's value
     * @param {string} id - Element ID
     * @param {string} value - Value to set
     * @returns {boolean} - Whether the element was found and updated
     */
    setValue(id, value) {
        const el = document.getElementById(id);
        if (el) {
            el.value = value;
            return true;
        }
        return false;
    },

    /**
     * Safely update a DOM element's style property
     * @param {string} id - Element ID
     * @param {string} property - CSS property name
     * @param {string} value - Value to set
     * @returns {boolean} - Whether the element was found and updated
     */
    setStyle(id, property, value) {
        const el = document.getElementById(id);
        if (el) {
            el.style[property] = value;
            return true;
        }
        return false;
    },

    /**
     * Safely get a DOM element
     * @param {string} id - Element ID
     * @returns {HTMLElement|null} - The element or null
     */
    getElement(id) {
        return document.getElementById(id);
    },

    /**
     * Update multiple elements with a mapping object
     * @param {Object} updates - Object with {elementId: value} pairs
     */
    updateElements(updates) {
        for (const [id, value] of Object.entries(updates)) {
            this.setText(id, value);
        }
    },

    /**
     * Debounce function calls
     * @param {Function} func - Function to debounce
     * @param {number} wait - Milliseconds to wait
     * @returns {Function} - Debounced function
     */
    debounce(func, wait) {
        let timeout;
        return function executedFunction(...args) {
            const later = () => {
                clearTimeout(timeout);
                func(...args);
            };
            clearTimeout(timeout);
            timeout = setTimeout(later, wait);
        };
    },

    /**
     * Throttle function calls
     * @param {func} func - Function to throttle
     * @param {number} limit - Milliseconds between calls
     * @returns {Function} - Throttled function
     */
    throttle(func, limit) {
        let inThrottle;
        return function(...args) {
            if (!inThrottle) {
                func.apply(this, args);
                inThrottle = true;
                setTimeout(() => inThrottle = false, limit);
            }
        };
    },

    /**
     * Format bytes to human readable string
     * @param {number} bytes - Number of bytes
     * @param {number} decimals - Decimal places
     * @returns {string} - Formatted string
     */
    formatBytes(bytes, decimals = 2) {
        if (bytes === 0) return '0 Bytes';
        const k = 1024;
        const dm = decimals < 0 ? 0 : decimals;
        const sizes = ['Bytes', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
    },

    /**
     * Format milliseconds to human readable duration
     * @param {number} ms - Milliseconds
     * @returns {string} - Formatted duration
     */
    formatDuration(ms) {
        const seconds = Math.floor(ms / 1000);
        const minutes = Math.floor(seconds / 60);
        const hours = Math.floor(minutes / 60);
        const days = Math.floor(hours / 24);

        if (days > 0) return `${days}d ${hours % 24}h`;
        if (hours > 0) return `${hours}h ${minutes % 60}m`;
        if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
        return `${seconds}s`;
    },

    /**
     * Check if running in file:// protocol or offline mode
     * @returns {boolean} - True if offline mode
     */
    isOfflineMode() {
        return window.location.protocol === 'file:' || false;
    },

    /**
     * Clamp a number between min and max
     * @param {number} value - Value to clamp
     * @param {number} min - Minimum value
     * @param {number} max - Maximum value
     * @returns {number} - Clamped value
     */
    clamp(value, min, max) {
        return Math.min(Math.max(value, min), max);
    },

    /**
     * Generate a random number between min and max
     * @param {number} min - Minimum value
     * @param {number} max - Maximum value
     * @returns {number} - Random number
     */
    random(min, max) {
        return Math.random() * (max - min) + min;
    },

    /**
     * Linear interpolation between two values
     * @param {number} start - Start value
     * @param {number} end - End value
     * @param {number} t - Interpolation factor (0-1)
     * @returns {number} - Interpolated value
     */
    lerp(start, end, t) {
        return start + (end - start) * t;
    },

    /**
     * Download a file with given content
     * @param {string} filename - Name for the downloaded file
     * @param {string} content - File content
     * @param {string} mimeType - MIME type (default: text/plain)
     */
    downloadFile(filename, content, mimeType = 'text/plain') {
        const blob = new Blob([content], { type: mimeType });
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        a.click();
        window.URL.revokeObjectURL(url);
    },

    /**
     * Copy text to clipboard
     * @param {string} text - Text to copy
     * @returns {Promise<boolean>} - Whether copy succeeded
     */
    async copyToClipboard(text) {
        try {
            await navigator.clipboard.writeText(text);
            return true;
        } catch (err) {
            console.error('Failed to copy to clipboard:', err);
            return false;
        }
    },

    /**
     * Escape HTML special characters
     * @param {string} text - Text to escape
     * @returns {string} - Escaped text
     */
    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    },

    /**
     * Deep clone an object
     * @param {Object} obj - Object to clone
     * @returns {Object} - Cloned object
     */
    deepClone(obj) {
        return JSON.parse(JSON.stringify(obj));
    }
};

// Expose globally
window.Utils = Utils;


