/**
 * @file shared/theme.js
 * @brief Theme and accessibility management
 */

class ThemeManager {
    static themes = ['light', 'dark', 'high-contrast', 'colorblind'];
    static currentTheme = 'light';
    static settings = {
        theme: 'light',
        fontSize: 100,
        gridLines: false,
        dualAxis: true,
        soundAlerts: true,
        autoRefresh: true
    };

    static init() {
        this.loadSettings();
        this.applyTheme(this.currentTheme);
        this.setupListeners();
    }

    static loadSettings() {
        const saved = localStorage.getItem('app-settings');
        if (saved) {
            try {
                this.settings = { ...this.settings, ...JSON.parse(saved) };
                this.currentTheme = this.settings.theme;
            } catch (e) {
                console.error('[THEME] Failed to load settings:', e);
            }
        }
    }

    static saveSettings() {
        localStorage.setItem('app-settings', JSON.stringify(this.settings));
        window.dispatchEvent(new CustomEvent('settings-changed', { detail: this.settings }));
    }

    static applyTheme(themeName) {
        if (!this.themes.includes(themeName)) {
            console.warn(`[THEME] Unknown theme: ${themeName}`);
            return;
        }

        document.body.className = '';
        if (themeName === 'dark') {
            document.body.classList.add('dark-theme');
        } else if (themeName === 'high-contrast') {
            document.body.classList.add('high-contrast');
        } else if (themeName === 'colorblind') {
            document.body.classList.add('colorblind-mode');
        }

        this.currentTheme = themeName;
        this.settings.theme = themeName;
        this.saveSettings();
    }

    static setFontSize(percent) {
        this.settings.fontSize = Math.max(80, Math.min(120, percent));
        document.documentElement.style.fontSize = (this.settings.fontSize / 100) * 16 + 'px';
        this.saveSettings();
    }

    static toggleGridLines(enabled) {
        this.settings.gridLines = enabled;
        this.saveSettings();
    }

    static toggleDualAxis(enabled) {
        this.settings.dualAxis = enabled;
        this.saveSettings();
    }

    static toggleSoundAlerts(enabled) {
        this.settings.soundAlerts = enabled;
        this.saveSettings();
    }

    static getThemes() {
        return this.themes;
    }

    static getCurrentTheme() {
        return this.currentTheme;
    }

    static getSettings() {
        return { ...this.settings };
    }

    static setupListeners() {
        window.addEventListener('keydown', (e) => {
            // T = Toggle theme
            if (e.key.toLowerCase() === 't' && !e.ctrlKey && !e.metaKey) {
                const current = this.themes.indexOf(this.currentTheme);
                const next = this.themes[(current + 1) % this.themes.length];
                this.applyTheme(next);
            }
        });
    }
}

// Auto-init on load
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => ThemeManager.init());
} else {
    ThemeManager.init();
}
