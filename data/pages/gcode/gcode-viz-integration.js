/**
 * G-Code Visualizer Integration
 * Connects the visualizer to the G-code editor and telemetry
 */
(function () {
    let initialized = false;
    let parseTimeout = null;

    /**
     * Initialize visualizer when page loads
     */
    function initVisualizer() {
        if (initialized) return;

        // Wait for canvas to be available
        const canvas = document.getElementById('gcode-preview');
        if (!canvas) {
            console.log('[VizIntegration] Canvas not found, retrying...');
            setTimeout(initVisualizer, 100);
            return;
        }

        // Initialize visualizer
        if (window.GCodeVisualizer && window.GCodeVisualizer.init()) {
            initialized = true;

            // Setup editor change handler
            setupEditorHandler();

            // Setup button handlers
            setupVisualizerButtons();

            // Setup telemetry handler for live position
            setupTelemetryHandler();

            // Parse initial content if any
            parseEditorContent();

            console.log('[VizIntegration] Initialized');
        }
    }

    /**
     * Setup control buttons for the visualizer
     */
    function setupVisualizerButtons() {
        if (!window.GCodeVisualizer) return;

        document.getElementById('viz-zoom-in')?.addEventListener('click', () => {
            window.GCodeVisualizer.zoom(1.2);
        });

        document.getElementById('viz-zoom-out')?.addEventListener('click', () => {
            window.GCodeVisualizer.zoom(0.8);
        });

        document.getElementById('viz-reset')?.addEventListener('click', () => {
            window.GCodeVisualizer.resetView();
            window.GCodeVisualizer.draw();
        });
    }

    /**
     * Setup handler for editor content changes
     */
    function setupEditorHandler() {
        const editor = document.getElementById('gcode-input');
        if (!editor) return;

        editor.addEventListener('input', debounce(parseEditorContent, 300));
    }

    /**
     * Parse current editor content
     */
    function parseEditorContent() {
        const editor = document.getElementById('gcode-input');
        if (!editor || !window.GCodeVisualizer) return;

        window.GCodeVisualizer.parse(editor.value);
    }

    /**
     * Setup telemetry handler for live position updates
     */
    function setupTelemetryHandler() {
        window.addEventListener('telemetry', function (e) {
            if (!window.GCodeVisualizer || !e.detail) return;

            const data = e.detail;

            // Update position from DRO data
            if (data.dro) {
                window.GCodeVisualizer.updatePosition(
                    data.dro.x || 0,
                    data.dro.y || 0,
                    data.dro.z || 0
                );
            }

            // Update work limits from motion config if available
            if (data.limits) {
                window.GCodeVisualizer.setWorkLimits({
                    xMin: data.limits.x_min || 0,
                    xMax: data.limits.x_max || 500,
                    yMin: data.limits.y_min || 0,
                    yMax: data.limits.y_max || 500,
                    zMin: data.limits.z_min || 0,
                    zMax: data.limits.z_max || 100
                });
            }
        });
    }

    /**
     * Debounce utility
     */
    function debounce(fn, ms) {
        let timer;
        return function (...args) {
            clearTimeout(timer);
            timer = setTimeout(() => fn.apply(this, args), ms);
        };
    }

    // Start initialization when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initVisualizer);
    } else {
        // Small delay to ensure other scripts have loaded
        setTimeout(initVisualizer, 50);
    }

    // Cleanup on page unload
    window.addEventListener('beforeunload', function () {
        if (window.GCodeVisualizer) {
            window.GCodeVisualizer.cleanup();
        }
    });
})();
