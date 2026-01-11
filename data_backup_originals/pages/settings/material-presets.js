/**
 * material-presets.js - Predefined configurations for different materials
 */
window.MaterialPresets = {
    limestone: {
        name: 'Limestone',
        spindle: {
            toolbreak_threshold: 3.0,
            stall_threshold: 15,
            stall_timeout_ms: 1500
        },
        vfd: {
            min_speed_hz: 10,
            max_speed_hz: 80,
            acc_time_ms: 800,
            dec_time_ms: 600
        },
        motion: {
            // Placeholder for motion specific parameters if needed
        }
    },
    marble: {
        name: 'Marble',
        spindle: {
            toolbreak_threshold: 5.0,
            stall_threshold: 25,
            stall_timeout_ms: 2000
        },
        vfd: {
            min_speed_hz: 5,
            max_speed_hz: 100,
            acc_time_ms: 600,
            dec_time_ms: 400
        },
        motion: {}
    },
    soft_granite: {
        name: 'Soft Granite',
        spindle: {
            toolbreak_threshold: 7.0,
            stall_threshold: 35,
            stall_timeout_ms: 2500
        },
        vfd: {
            min_speed_hz: 5,
            max_speed_hz: 105,
            acc_time_ms: 1000,
            dec_time_ms: 800
        },
        motion: {}
    },
    hard_granite: {
        name: 'Hard Granite',
        spindle: {
            toolbreak_threshold: 10.0,
            stall_threshold: 45,
            stall_timeout_ms: 3000
        },
        vfd: {
            min_speed_hz: 5,
            max_speed_hz: 105,
            acc_time_ms: 1500,
            dec_time_ms: 1200
        },
        motion: {}
    }
};
