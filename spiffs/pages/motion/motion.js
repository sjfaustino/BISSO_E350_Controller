/**
 * Motion Control Page Module
 */
const MotionModule = {
    stepSize: 10,
    isMoving: false,

    init() {
        console.log('[Motion] Initializing');
        this.setupEventListeners();
        window.addEventListener('state-changed', () => this.onStateChanged());
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
            document.getElementById('pos-x').textContent = (state.motion.position?.x || 0).toFixed(2) + ' mm';
            document.getElementById('pos-y').textContent = (state.motion.position?.y || 0).toFixed(2) + ' mm';
            document.getElementById('pos-z').textContent = (state.motion.position?.z || 0).toFixed(2) + ' mm';
            document.getElementById('pos-a').textContent = (state.motion.position?.a || 0).toFixed(2) + ' °';

            this.isMoving = state.motion.moving || false;
        }
    },

    cleanup() {
        console.log('[Motion] Cleaning up');
    }
};

window.currentPageModule = MotionModule;
