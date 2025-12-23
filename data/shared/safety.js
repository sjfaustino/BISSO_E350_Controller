/**
 * @file shared/safety.js
 * @brief Global Safety Components - Alarm Banner and E-Stop Button
 */

class SafetyManager {
    static alarmBanner = null;
    static estopButton = null;
    static alarmCheckInterval = null;
    static currentAlarms = [];
    static estopActive = false;

    static init() {
        this.createAlarmBanner();
        this.createEstopButton();
        this.startAlarmMonitoring();
        console.log('[SAFETY] Manager initialized');
    }

    static createAlarmBanner() {
        // Create alarm banner element (hidden by default)
        const banner = document.createElement('div');
        banner.id = 'alarm-banner';
        banner.className = 'alarm-banner';
        banner.style.display = 'none';
        banner.innerHTML = `
            <div class="alarm-content">
                <div class="alarm-icon">⚠️</div>
                <div class="alarm-details">
                    <div class="alarm-title">SYSTEM ALARM</div>
                    <div class="alarm-message" id="alarm-message"></div>
                </div>
                <button class="alarm-dismiss" id="alarm-dismiss" title="Dismiss">✕</button>
            </div>
        `;

        document.body.appendChild(banner);
        this.alarmBanner = banner;

        // Dismiss button
        document.getElementById('alarm-dismiss')?.addEventListener('click', () => {
            this.dismissAlarm();
        });
    }

    static createEstopButton() {
        // Create floating E-stop button
        const button = document.createElement('div');
        button.id = 'estop-button';
        button.className = 'estop-button';
        button.innerHTML = `
            <button class="estop-btn" id="estop-trigger" title="Emergency Stop">
                <div class="estop-icon">⏹</div>
                <div class="estop-label">E-STOP</div>
            </button>
        `;

        document.body.appendChild(button);
        this.estopButton = button;

        // E-stop trigger
        document.getElementById('estop-trigger')?.addEventListener('click', () => {
            this.confirmEstop();
        });
    }

    static startAlarmMonitoring() {
        // Check for alarms every 2 seconds
        this.alarmCheckInterval = setInterval(() => {
            this.checkAlarms();
        }, 2000);

        // Also check immediately
        this.checkAlarms();
    }

    static async checkAlarms() {
        try {
            // Skip API calls in file:// mode or mock mode - no real device to check
            if (window.location.protocol === 'file:' || window.MockMode?.enabled) {
                // In mock mode, assume no alarms for simplicity
                this.estopActive = false;
                this.updateEstopButton();
                this.hideAlarm();
                return;
            }

            // PHASE 5.10: Removed hardcoded credentials - browser handles auth via 401 prompt
            const response = await fetch('/api/alarms', {
                credentials: 'same-origin' // Include auth credentials from browser
            });

            if (!response.ok) return;

            const data = await response.json();

            // Update E-stop state
            this.estopActive = data.estop_active;
            this.updateEstopButton();

            // Update alarm banner
            if (data.estop_active) {
                this.showAlarm('EMERGENCY STOP ACTIVE', 'critical');
            } else if (data.faults && data.faults.length > 0) {
                // Show most recent critical/error fault
                const criticalFault = data.faults.find(f => f.severity === 'CRITICAL' || f.severity === 'ERROR');
                if (criticalFault) {
                    this.showAlarm(
                        `${criticalFault.code}: ${criticalFault.message}`,
                        criticalFault.severity === 'CRITICAL' ? 'critical' : 'warning'
                    );
                }
            } else {
                this.hideAlarm();
            }

            this.currentAlarms = data.faults || [];

        } catch (error) {
            console.error('[SAFETY] Alarm check error:', error);
        }
    }

    static showAlarm(message, severity = 'warning') {
        if (!this.alarmBanner) return;

        const messageEl = document.getElementById('alarm-message');
        if (messageEl) messageEl.textContent = message;

        this.alarmBanner.className = `alarm-banner alarm-${severity}`;
        this.alarmBanner.style.display = 'block';

        // Flash effect for critical alarms
        if (severity === 'critical') {
            this.alarmBanner.classList.add('alarm-flash');
        } else {
            this.alarmBanner.classList.remove('alarm-flash');
        }
    }

    static hideAlarm() {
        if (this.alarmBanner) {
            this.alarmBanner.style.display = 'none';
        }
    }

    static dismissAlarm() {
        // Only allow dismissing non-E-stop alarms
        if (!this.estopActive) {
            this.hideAlarm();
        } else {
            AlertManager.add('Cannot dismiss - E-stop active! Reset E-stop first.', 'critical', 3000);
        }
    }

    static updateEstopButton() {
        const btn = document.getElementById('estop-trigger');
        if (!btn) return;

        if (this.estopActive) {
            btn.classList.add('estop-active');
            btn.querySelector('.estop-label').textContent = 'RESET';
            btn.title = 'Reset Emergency Stop';
        } else {
            btn.classList.remove('estop-active');
            btn.querySelector('.estop-label').textContent = 'E-STOP';
            btn.title = 'Emergency Stop';
        }
    }

    static confirmEstop() {
        if (this.estopActive) {
            // Reset E-stop
            if (confirm('Reset Emergency Stop?\n\nEnsure it is safe to resume operation.')) {
                this.resetEstop();
            }
        } else {
            // Trigger E-stop
            if (confirm('Trigger Emergency Stop?\n\nThis will halt all motion immediately.')) {
                this.triggerEstop();
            }
        }
    }

    static async triggerEstop() {
        // In file:// or mock mode, these are non-functional
        if (window.location.protocol === 'file:' || window.MockMode?.enabled) {
            AlertManager.add('E-stop not available in offline mode', 'warning', 3000);
            return;
        }

        try {
            // PHASE 5.10: Removed hardcoded credentials - browser handles auth via 401 prompt
            const response = await fetch('/api/estop/trigger', {
                method: 'POST',
                credentials: 'same-origin' // Include auth credentials from browser
            });

            if (response.ok) {
                AlertManager.add('Emergency Stop Activated', 'critical', 5000);
                this.checkAlarms(); // Immediate update
            } else {
                AlertManager.add('Failed to trigger E-stop', 'critical', 3000);
            }
        } catch (error) {
            console.error('[SAFETY] E-stop trigger error:', error);
            AlertManager.add('Communication error', 'critical', 3000);
        }
    }

    static async resetEstop() {
        // In file:// or mock mode, these are non-functional
        if (window.location.protocol === 'file:' || window.MockMode?.enabled) {
            AlertManager.add('E-stop reset not available in offline mode', 'warning', 3000);
            return;
        }

        try {
            // PHASE 5.10: Removed hardcoded credentials - browser handles auth via 401 prompt
            const response = await fetch('/api/estop/reset', {
                method: 'POST',
                credentials: 'same-origin' // Include auth credentials from browser
            });

            const data = await response.json();

            if (data.success) {
                AlertManager.add('Emergency Stop Reset - Operation can resume', 'success', 3000);
                this.checkAlarms(); // Immediate update
            } else {
                AlertManager.add('E-stop reset failed - Check system state', 'critical', 3000);
            }
        } catch (error) {
            console.error('[SAFETY] E-stop reset error:', error);
            AlertManager.add('Communication error', 'critical', 3000);
        }
    }

    static cleanup() {
        if (this.alarmCheckInterval) {
            clearInterval(this.alarmCheckInterval);
        }
    }
}

// CSS styles for safety components (inject into page)
const safetyStyles = `
<style>
.alarm-banner {
    position: fixed;
    top: 60px;
    left: 0;
    right: 0;
    z-index: 9999;
    padding: 15px 20px;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    animation: slideDown 0.3s ease;
}

.alarm-banner.alarm-critical {
    background: var(--color-critical);
    color: white;
}

.alarm-banner.alarm-warning {
    background: var(--color-warning);
    color: white;
}

.alarm-content {
    display: flex;
    align-items: center;
    gap: 15px;
    max-width: 1200px;
    margin: 0 auto;
}

.alarm-icon {
    font-size: 32px;
    flex-shrink: 0;
}

.alarm-details {
    flex: 1;
}

.alarm-title {
    font-weight: bold;
    font-size: 16px;
    margin-bottom: 4px;
    letter-spacing: 1px;
}

.alarm-message {
    font-size: 14px;
    opacity: 0.95;
}

.alarm-dismiss {
    background: rgba(255, 255, 255, 0.2);
    border: none;
    color: white;
    width: 30px;
    height: 30px;
    border-radius: 50%;
    cursor: pointer;
    font-size: 20px;
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
}

.alarm-dismiss:hover {
    background: rgba(255, 255, 255, 0.3);
}

.alarm-flash {
    animation: flash 1s infinite;
}

@keyframes flash {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.7; }
}

@keyframes slideDown {
    from {
        transform: translateY(-100%);
        opacity: 0;
    }
    to {
        transform: translateY(0);
        opacity: 1;
    }
}

.estop-button {
    position: fixed;
    bottom: 30px;
    right: 30px;
    z-index: 9998;
}

.estop-btn {
    background: var(--color-critical);
    color: white;
    border: 3px solid #fff;
    border-radius: 50%;
    width: 90px;
    height: 90px;
    cursor: pointer;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    box-shadow: 0 4px 16px rgba(220, 38, 38, 0.5);
    transition: all 0.2s ease;
}

.estop-btn:hover {
    transform: scale(1.05);
    box-shadow: 0 6px 20px rgba(220, 38, 38, 0.7);
}

.estop-btn:active {
    transform: scale(0.95);
}

.estop-btn.estop-active {
    background: var(--color-warning);
    animation: pulse 1.5s infinite;
}

.estop-icon {
    font-size: 32px;
    line-height: 1;
}

.estop-label {
    font-size: 11px;
    font-weight: bold;
    margin-top: 4px;
    letter-spacing: 0.5px;
}

@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.6; }
}

@media (max-width: 768px) {
    .alarm-banner {
        top: 50px;
        padding: 12px 15px;
    }

    .alarm-icon {
        font-size: 24px;
    }

    .alarm-title {
        font-size: 14px;
    }

    .alarm-message {
        font-size: 12px;
    }

    .estop-button {
        bottom: 20px;
        right: 20px;
    }

    .estop-btn {
        width: 70px;
        height: 70px;
    }

    .estop-icon {
        font-size: 24px;
    }

    .estop-label {
        font-size: 9px;
    }
}
</style>
`;

// Inject styles
document.head.insertAdjacentHTML('beforeend', safetyStyles);

// Auto-initialize
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => SafetyManager.init());
} else {
    SafetyManager.init();
}

window.SafetyManager = SafetyManager;
