/**
 * Motion Control Page Module
 * Note: Use window.MotionModule to avoid "already declared" errors when navigating
 */
window.MotionModule = window.MotionModule || {
    stepSize: 10,
    isMoving: false,
    positionViz: null,

    init() {
        console.log('[Motion] Initializing');
        this.setupPositionVisualization();
        this.setupEventListeners();
        window.addEventListener('state-changed', () => this.onStateChanged());
    },

    setupPositionVisualization() {
        // Initialize position visualization canvas
        const canvas = document.getElementById('position-canvas');
        if (canvas) {
            // Set canvas size
            const rect = canvas.parentElement.getBoundingClientRect();
            canvas.width = rect.width || 600;
            canvas.height = rect.height || 350;

            // Create visualizer with configuration
            this.positionViz = new PositionVisualizer('position-canvas', {
                x_min: -100,
                x_max: 500,
                y_min: -100,
                y_max: 500,
                z_min: 0,
                z_max: 100,
                showGrid: true,
                showLimits: true,
                showTrail: true
            });

            // Clear trail button
            const clearBtn = document.getElementById('clear-trail');
            if (clearBtn) {
                clearBtn.addEventListener('click', () => {
                    this.positionViz.clearTrail();
                    AlertManager.add('Position trail cleared', 'info', 1500);
                });
            }
        }
    },

    setupEventListeners() {
        // Jog buttons
        document.querySelectorAll('[data-jog]').forEach(btn => {
            btn.addEventListener('click', () => {
                const direction = btn.dataset.jog;
                this.sendJog(direction);
            });
        });

        // Preset buttons
        document.querySelectorAll('[data-preset]').forEach(btn => {
            btn.addEventListener('click', () => {
                const preset = btn.dataset.preset;
                this.moveToPreset(preset);
            });
        });

        // Step size selector
        const stepSelect = document.getElementById('step-size');
        if (stepSelect) {
            stepSelect.addEventListener('change', (e) => {
                this.stepSize = parseInt(e.target.value);
            });
        }

        // Keyboard controls
        window.addEventListener('keydown', (e) => {
            if (e.ctrlKey || e.metaKey) return;

            const directions = {
                'ArrowUp': 'Y+',
                'ArrowDown': 'Y-',
                'ArrowLeft': 'X-',
                'ArrowRight': 'X+',
                'w': 'Z+',
                's': 'Z-',
                ' ': 'STOP'
            };

            if (directions[e.key]) {
                e.preventDefault();
                this.sendJog(directions[e.key]);
            }
        });
    },

    sendJog(direction) {
        if (direction === 'STOP') {
            SharedWebSocket.send({ cmd: 'stop' });
            AlertManager.add('Motion stopped', 'info', 2000);
        } else {
            const distance = this.stepSize;
            SharedWebSocket.send({
                cmd: 'jog',
                direction,
                distance,
                speed: 100
            });
            AlertManager.add(`Jog: ${direction} ${distance}mm`, 'info', 1000);
        }
    },

    moveToPreset(preset) {
        const presets = {
            'home': { x: 0, y: 0, z: 0, a: 0 },
            'park': { x: 0, y: 0, z: 50, a: 0 },
            'corner-tl': { x: -100, y: 100, z: 0, a: 0 },
            'corner-tr': { x: 100, y: 100, z: 0, a: 0 },
            'corner-bl': { x: -100, y: -100, z: 0, a: 0 },
            'corner-br': { x: 100, y: -100, z: 0, a: 0 }
        };

        if (presets[preset]) {
            SharedWebSocket.send({
                cmd: 'move_absolute',
                position: presets[preset],
                speed: 100
            });
            AlertManager.add(`Moving to ${preset}...`, 'info');
        }
    },

    onStateChanged() {
        const state = AppState.data;

        if (state.motion) {
            const x = state.motion.position?.x || 0;
            const y = state.motion.position?.y || 0;
            const z = state.motion.position?.z || 0;
            const a = state.motion.position?.a || 0;

            document.getElementById('pos-x').textContent = x.toFixed(2) + ' mm';
            document.getElementById('pos-y').textContent = y.toFixed(2) + ' mm';
            document.getElementById('pos-z').textContent = z.toFixed(2) + ' mm';
            document.getElementById('pos-a').textContent = a.toFixed(2) + ' °';

            // Update position visualization
            if (this.positionViz) {
                this.positionViz.updatePosition(x, y, z, a);
            }

            this.isMoving = state.motion.moving || false;
        }
    },

    cleanup() {
        console.log('[Motion] Cleaning up');
        if (this.positionViz) {
            this.positionViz.destroy();
            this.positionViz = null;
        }
    }
};

window.currentPageModule = MotionModule;
