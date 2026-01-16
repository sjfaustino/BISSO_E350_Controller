/**
 * i18n.js - Minimal Internationalization Manager for ESP32 Web UI
 * Handles loading of JSON language files and replacing text content.
 */

window.I18nManager = window.I18nManager || {
    currentLang: 'en',
    translations: {},
    availableLangs: ['en', 'pt'],
    ready: null,

    async init() {
        // Create a promise that other modules can await
        this.ready = (async () => {
            // 1. Get preference from storage or fallback to 'en'
            const stored = localStorage.getItem('language');
            this.currentLang = (stored && this.availableLangs.includes(stored)) ? stored : 'en';
            console.log('[I18n] Initializing language:', this.currentLang);

            // 2. Load the translation file
            await this.loadTranslations(this.currentLang);

            // 3. Apply to page
            this.updatePage();

            // 4. Update HTML lang attribute
            document.documentElement.lang = this.currentLang;

            return true;
        })();

        return this.ready;
    },

    async loadTranslations(lang) {
        try {
            const response = await fetch(`/i18n/${lang}.json`);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            this.translations = await response.json();
            console.log('[I18n] Loaded strings for:', lang);
        } catch (e) {
            console.error('[I18n] Failed to load translations:', e);
            this.translations = {};
        }
    },

    setLang(lang) {
        if (!this.availableLangs.includes(lang)) return;

        localStorage.setItem('language', lang);
        this.currentLang = lang;

        // Reload to apply clean state
        location.reload();
    },

    // Translate a key
    t(key) {
        if (!key) return '';

        const keys = key.split('.');
        let val = this.translations;

        for (const k of keys) {
            if (val && val[k] !== undefined) {
                val = val[k];
            } else {
                return key;
            }
        }

        return val || key;
    },

    // Update all static HTML elements with [data-i18n] attributes
    updatePage() {
        // SAFETY: If translations aren't loaded yet, don't touch the DOM
        // to avoid replacing fallbacks with raw keys.
        if (!this.translations || Object.keys(this.translations).length === 0) return;

        // 1. Text Content
        document.querySelectorAll('[data-i18n]').forEach(el => {
            const key = el.getAttribute('data-i18n');
            const text = this.t(key);
            if (text !== key) el.textContent = text;
        });

        // 2. Attributes
        document.querySelectorAll('[data-i18n-attr]').forEach(el => {
            const raw = el.getAttribute('data-i18n-attr');
            const pairs = raw.split(';');
            pairs.forEach(pair => {
                const [attr, key] = pair.split(':');
                if (attr && key) {
                    const text = this.t(key);
                    if (text !== key) el.setAttribute(attr.trim(), text);
                }
            });
        });
    }
};

window.i18n = window.I18nManager;
