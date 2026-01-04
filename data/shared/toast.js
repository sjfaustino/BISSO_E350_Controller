/**
 * Toast Notification System
 * Displays temporary notifications to the user
 */

const Toast = {
    container: null,
    queue: [],
    maxToasts: 5,

    /**
     * Initialize the toast system
     */
    init() {
        if (this.container) return; // Already initialized

        // Create toast container
        this.container = document.createElement('div');
        this.container.id = 'toast-container';
        this.container.className = 'toast-container';
        document.body.appendChild(this.container);

        console.log('[Toast] Notification system initialized');
    },

    /**
     * Show a toast notification
     * @param {string} message - Message to display
     * @param {string} type - Type: 'success', 'error', 'warning', 'info'
     * @param {number} duration - Duration in ms (0 = persistent)
     * @returns {HTMLElement} - The toast element
     */
    show(message, type = 'info', duration = 3000) {
        if (!this.container) this.init();

        // Clear existing toasts if it's a transient message (not error)
        // or clear if we already have too many to prevent blocking the screen
        if (type !== 'error') {
            const existing = this.container.querySelectorAll('.toast');
            existing.forEach(t => {
                if (!t.classList.contains('toast-error')) this.remove(t);
            });
        }

        // Create toast element
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;

        // Icon based on type
        const icons = {
            success: '✓',
            error: '✕',
            warning: '⚠',
            info: 'ℹ'
        };
        const icon = icons[type] || icons.info;

        toast.innerHTML = `
            <div class="toast-icon">${icon}</div>
            <div class="toast-message">${Utils.escapeHtml(message)}</div>
            <button class="toast-close" aria-label="Close">OK</button>
        `;

        // Close button
        const closeBtn = toast.querySelector('.toast-close');
        closeBtn.addEventListener('click', () => this.remove(toast));

        // Add to container
        this.container.appendChild(toast);

        // Trigger animation
        requestAnimationFrame(() => {
            toast.classList.add('toast-show');
        });

        // Auto-remove after duration
        if (duration > 0) {
            setTimeout(() => this.remove(toast), duration);
        }

        // Limit number of toasts
        this.limitToasts();

        return toast;
    },

    /**
     * Show success toast
     * @param {string} message - Message to display
     * @param {number} duration - Duration in ms
     */
    success(message, duration = 3000) {
        return this.show(message, 'success', duration);
    },

    /**
     * Show error toast
     * @param {string} message - Message to display
     * @param {number} duration - Duration in ms (0 = persistent)
     */
    error(message, duration = 5000) {
        return this.show(message, 'error', duration);
    },

    /**
     * Show warning toast
     * @param {string} message - Message to display
     * @param {number} duration - Duration in ms
     */
    warning(message, duration = 4000) {
        return this.show(message, 'warning', duration);
    },

    /**
     * Show info toast
     * @param {string} message - Message to display
     * @param {number} duration - Duration in ms
     */
    info(message, duration = 3000) {
        return this.show(message, 'info', duration);
    },

    /**
     * Remove a toast
     * @param {HTMLElement} toast - Toast element to remove
     */
    remove(toast) {
        if (!toast || !toast.parentNode) return;

        toast.classList.remove('toast-show');
        toast.classList.add('toast-hide');

        setTimeout(() => {
            if (toast.parentNode) {
                toast.parentNode.removeChild(toast);
            }
        }, 300); // Match CSS transition duration
    },

    /**
     * Remove all toasts
     */
    clear() {
        if (!this.container) return;

        const toasts = this.container.querySelectorAll('.toast');
        toasts.forEach(toast => this.remove(toast));
    },

    /**
     * Limit number of visible toasts
     */
    limitToasts() {
        if (!this.container) return;

        const toasts = this.container.querySelectorAll('.toast');
        if (toasts.length > this.maxToasts) {
            // Remove oldest toasts
            const toRemove = Array.from(toasts).slice(0, toasts.length - this.maxToasts);
            toRemove.forEach(toast => this.remove(toast));
        }
    }
};

// Auto-initialize when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => Toast.init());
} else {
    Toast.init();
}

// Expose globally
window.Toast = Toast;
