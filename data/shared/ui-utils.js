/**
 * ui-utils.js - Shared UI Utilities
 * Provides standard UI elements like loading spinners and confirmation dialogs.
 */

window.UI = {
    /**
     * Shows a loading spinner on a specific element (usually a button or container).
     * Disables the element if it's a button.
     * @param {string|HTMLElement} elementId - The target element ID or element
     * @param {string} [loadingText] - Optional text to show next to spinner
     * @returns {function} Function to restore the original state (hide spinner)
     */
    showSpinner(elementId, loadingText) {
        const el = typeof elementId === 'string' ? document.getElementById(elementId) : elementId;
        if (!el) return () => { };

        const originalHtml = el.innerHTML;
        const originalDisabled = el.disabled;
        const width = el.offsetWidth;

        // Prevent layout shift if possible
        if (width > 0) el.style.minWidth = `${width}px`;

        el.disabled = true;
        el.innerHTML = `<span class="spinner-border spinner-border-sm" role="status" aria-hidden="true"></span> ${loadingText || ''}`;

        // Return a cleanup function to restore state
        return () => {
            el.innerHTML = originalHtml;
            el.disabled = originalDisabled;
            el.style.minWidth = '';
        };
    },

    /**
     * Shows a confirmation dialog.
     * Uses native confirm() for now, but wrapped for future enhancement.
     * @param {string} message - The confirmation message
     * @returns {Promise<boolean>} Resolves to true if confirmed, false otherwise
     */
    async showConfirm(message) {
        // Wrap in timeout to allow UI updates to flush before blocking alert
        return new Promise(resolve => {
            setTimeout(() => {
                const result = confirm(message);
                resolve(result);
            }, 10);
        });
    },

    /**
     * Toggles a 'spinner' class on an element.
     * @param {string} id - Element ID
     * @param {boolean} show - Show or hide
     */
    toggleSpinner(id, show) {
        const el = document.getElementById(id);
        if (!el) return;
        if (show) el.classList.add('spinner');
        else el.classList.remove('spinner');
    }
};
