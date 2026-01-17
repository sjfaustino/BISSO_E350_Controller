/**
 * validation.js - Shared Validation Utilities
 * Provides common validation functions for form inputs and configuration values.
 */

window.ValidationUtils = {
    /**
     * Validates if a value is not empty/null/undefined.
     * @param {any} value - The value to check
     * @returns {boolean} True if valid
     */
    required(value) {
        return value !== null && value !== undefined && String(value).trim() !== '';
    },

    /**
     * Validates if a value is a valid number within a range.
     * @param {number|string} value - The value to check
     * @param {number} [min] - Minimum value (inclusive)
     * @param {number} [max] - Maximum value (inclusive)
     * @returns {object} { valid: boolean, error: string|null }
     */
    number(value, min, max) {
        const num = parseFloat(value);
        if (isNaN(num)) return { valid: false, error: 'Must be a number' };
        if (min !== undefined && num < min) return { valid: false, error: `Must be >= ${min}` };
        if (max !== undefined && num > max) return { valid: false, error: `Must be <= ${max}` };
        return { valid: true, error: null };
    },

    /**
     * Validates an IP address string (IPv4).
     * @param {string} ip - The IP string
     * @returns {boolean} True if valid IPv4
     */
    ipAddress(ip) {
        if (!ip) return false;
        const parts = ip.split('.');
        if (parts.length !== 4) return false;
        return parts.every(part => {
            const num = parseInt(part, 10);
            return !isNaN(num) && num >= 0 && num <= 255;
        });
    },

    /**
     * Validates a hostname or IP address.
     * @param {string} host - The hostname
     * @returns {boolean} True if valid
     */
    hostname(host) {
        if (!host) return false;
        // Simple regex for hostname/IP - can be expanded
        const regex = /^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])$|^(\d{1,3}\.){3}\d{1,3}$/;
        return regex.test(host);
    },

    /**
     * Validates a baud rate against standard values.
     * @param {number|string} baud - The baud rate
     * @returns {boolean} True if valid
     */
    baudRate(baud) {
        const validRates = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600];
        return validRates.includes(parseInt(baud));
    }
};
