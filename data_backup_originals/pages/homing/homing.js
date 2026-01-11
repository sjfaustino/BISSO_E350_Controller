/**
 * @file homing.js
 * @brief Homing Wizard - Step-by-step guided homing procedure
 */

(function () {
    'use strict';

    let currentStep = 1;
    let homingInProgress = false;
    let positionInterval = null;

    // Initialize wizard
    function init() {
        console.log('[HOMING] Wizard initialized');
        bindEvents();
        updatePrecheckStatus();
        startPositionPolling();
    }

    function bindEvents() {
        // Pre-check checkboxes
        document.querySelectorAll('.precheck').forEach(cb => {
            cb.addEventListener('change', updatePrecheckStatus);
        });

        // Navigation buttons
        const btnStart = document.getElementById('btn-start-homing');
        if (btnStart) btnStart.addEventListener('click', () => goToStep(2));

        // Axis homing buttons
        const btnHomeZ = document.getElementById('btn-home-z');
        const btnHomeX = document.getElementById('btn-home-x');
        const btnHomeY = document.getElementById('btn-home-y');
        const btnSkipZ = document.getElementById('btn-skip-z');
        const btnSkipX = document.getElementById('btn-skip-x');
        const btnSkipY = document.getElementById('btn-skip-y');

        if (btnHomeZ) btnHomeZ.addEventListener('click', () => homeAxis('Z'));
        if (btnHomeX) btnHomeX.addEventListener('click', () => homeAxis('X'));
        if (btnHomeY) btnHomeY.addEventListener('click', () => homeAxis('Y'));
        if (btnSkipZ) btnSkipZ.addEventListener('click', () => goToStep(3));
        if (btnSkipX) btnSkipX.addEventListener('click', () => goToStep(4));
        if (btnSkipY) btnSkipY.addEventListener('click', () => goToStep(5));

        // Completion buttons
        const btnDashboard = document.getElementById('btn-go-dashboard');
        const btnRestart = document.getElementById('btn-restart-wizard');

        if (btnDashboard) btnDashboard.addEventListener('click', () => {
            window.location.hash = '#dashboard';
        });
        if (btnRestart) btnRestart.addEventListener('click', () => {
            currentStep = 1;
            document.querySelectorAll('.precheck').forEach(cb => cb.checked = false);
            goToStep(1);
            updatePrecheckStatus();
        });
    }

    function updatePrecheckStatus() {
        const checks = document.querySelectorAll('.precheck');
        const allChecked = Array.from(checks).every(cb => cb.checked);
        const btnStart = document.getElementById('btn-start-homing');
        const statusEl = document.getElementById('precheck-status');

        if (btnStart) {
            btnStart.disabled = !allChecked;
        }
        if (statusEl) {
            if (allChecked) {
                statusEl.textContent = '✅ All checks complete. Ready to begin homing.';
                statusEl.style.color = 'var(--color-success)';
            } else {
                const remaining = Array.from(checks).filter(cb => !cb.checked).length;
                statusEl.textContent = `${remaining} check(s) remaining`;
                statusEl.style.color = 'var(--text-secondary)';
            }
        }
    }

    function goToStep(step) {
        // Update step indicators
        document.querySelectorAll('.step-circle').forEach((circle, index) => {
            circle.classList.remove('active');
            if (index + 1 < step) {
                circle.classList.add('complete');
            } else if (index + 1 === step) {
                circle.classList.add('active');
            }
        });

        // Show/hide content
        document.querySelectorAll('.wizard-content').forEach(content => {
            content.style.display = 'none';
        });

        const stepContent = document.getElementById(`step-${step}`);
        if (stepContent) {
            stepContent.style.display = 'block';
        }

        currentStep = step;

        // Update final positions on completion step
        if (step === 5) {
            updateFinalPositions();
        }
    }

    async function homeAxis(axis) {
        if (homingInProgress) {
            console.log('[HOMING] Already in progress');
            return;
        }

        homingInProgress = true;
        const stateEl = document.getElementById(`${axis.toLowerCase()}-state`);
        const homeBtn = document.getElementById(`btn-home-${axis.toLowerCase()}`);
        const skipBtn = document.getElementById(`btn-skip-${axis.toLowerCase()}`);

        if (stateEl) {
            stateEl.textContent = 'Homing...';
            stateEl.className = 'badge homing';
        }
        if (homeBtn) homeBtn.disabled = true;
        if (skipBtn) skipBtn.disabled = true;

        try {
            // Send homing command to API
            const response = await Utils.fetchWithAuth('/api/jog', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    command: 'home',
                    axis: axis.toUpperCase()
                })
            });

            if (!response.ok) {
                throw new Error(`Homing failed: ${response.status}`);
            }

            // Simulate homing completion (in real use, poll for status)
            await new Promise(resolve => setTimeout(resolve, 2000));

            if (stateEl) {
                stateEl.textContent = 'Homed ✓';
                stateEl.className = 'badge complete';
            }

            // Move to next step after short delay
            setTimeout(() => {
                const nextStep = axis === 'Z' ? 3 : (axis === 'X' ? 4 : 5);
                goToStep(nextStep);
            }, 500);

        } catch (error) {
            console.error('[HOMING] Error:', error);
            if (stateEl) {
                stateEl.textContent = 'Error';
                stateEl.className = 'badge error';
            }
            if (typeof AlertManager !== 'undefined') {
                AlertManager.add(`Homing ${axis} failed: ${error.message}`, 'error');
            }
        } finally {
            homingInProgress = false;
            if (homeBtn) homeBtn.disabled = false;
            if (skipBtn) skipBtn.disabled = false;
        }
    }

    function startPositionPolling() {
        positionInterval = setInterval(async () => {
            try {
                const response = await Utils.fetchWithAuth('/api/status');
                if (response.ok) {
                    const data = await response.json();
                    updatePositionDisplay(data);
                }
            } catch (e) {
                // Silent fail - position display is non-critical
            }
        }, 500);
    }

    function updatePositionDisplay(data) {
        const xPos = document.getElementById('x-pos');
        const yPos = document.getElementById('y-pos');
        const zPos = document.getElementById('z-pos');

        if (data.positions) {
            if (xPos) xPos.textContent = (data.positions.x || 0).toFixed(2);
            if (yPos) yPos.textContent = (data.positions.y || 0).toFixed(2);
            if (zPos) zPos.textContent = (data.positions.z || 0).toFixed(2);
        }
    }

    function updateFinalPositions() {
        const xPos = document.getElementById('x-pos');
        const yPos = document.getElementById('y-pos');
        const zPos = document.getElementById('z-pos');
        const finalX = document.getElementById('final-x');
        const finalY = document.getElementById('final-y');
        const finalZ = document.getElementById('final-z');

        if (xPos && finalX) finalX.textContent = xPos.textContent;
        if (yPos && finalY) finalY.textContent = yPos.textContent;
        if (zPos && finalZ) finalZ.textContent = zPos.textContent;
    }

    // Cleanup on page unload
    function cleanup() {
        if (positionInterval) {
            clearInterval(positionInterval);
            positionInterval = null;
        }
    }

    // Export for page lifecycle
    window.HomingWizard = {
        init: init,
        cleanup: cleanup
    };

    // Auto-init when DOM ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
