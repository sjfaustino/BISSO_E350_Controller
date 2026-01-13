# Internationalization (i18n) Strategy for Posipro Web UI

This document outlines the architectural plan to add multi-lingual support (English and Portuguese PT-PT) to the Posipro Web UI, as requested.

## Constraints & Requirements
- **Hardware**: ESP32 with limited RAM. Avoid large in-memory dictionaries on the server.
- **Technology**: Vanilla JS, LittleFS, embedded web server.
- **Target Languages**: English (Source), Portuguese (PT-PT).

## Recommended Approach: Client-Side Translation

Given the static nature of the HTML files served from LittleFS and the memory constraints of the ESP32, a **Client-Side Replacement Strategy** is the most efficient and scalable approach.

The server continues to serve identical static HTML files. The browser determines the user's language preference and replaces text content dynamically upon load.

### 1. File Structure
We will introduce a lightweight JSON-based structure:

```text
data/
├── i18n/
│   ├── en.json       # Source language definition (English)
│   └── pt.json       # Portuguese translations (PT-PT)
└── shared/
    └── i18n.js       # Lightweight translation manager script
```

### 2. JSON Schema (`en.json`)
We will use a nested JSON structure for namespace organization:

```json
{
  "nav": {
    "dashboard": "Dashboard",
    "motion": "Motion",
    "settings": "Settings"
  },
  "dashboard": {
    "dro_title": "Position (DRO)",
    "zero_all": "Zero All",
    "system_health": "System Health"
  },
  "settings": {
    "save": "Save",
    "reset": "Reset Defaults"
  }
}
```

### 3. HTML Integration & UI Changes
We will tag text elements with a `data-i18n` attribute corresponding to the JSON key.

**Global Navigation Bar (New):**
- Add two small flag icons (🇺🇸 / 🇵🇹) next to the "Posipro" title in the sidebar/header.
- **Behavior**: Clicking a flag sets the language cookie/localStorage and instantly updates the UI for that browser.

**Settings Page:**
- Retain the Language Selector dropdown in `settings.html`.
- This can be used to set the *default* system language (boot default) if desired, or simply mirror the browser setting.

**Example Markup (Inline SVG for zero latency):**
```html
<div class="logo">
    Posipro <span class="version-tag">v1.0.0</span>
    <div class="lang-flags">
        <!-- English (US/GB) -->
        <svg class="flag-icon" onclick="i18n.setLang('en')" viewBox="0 0 640 480" width="24" height="18">
            <path fill="#012169" d="M0 0h640v480H0z"/>
            <!-- ... simplified flag paths ... -->
        </svg>
        
        <!-- Portuguese (PT) -->
        <svg class="flag-icon" onclick="i18n.setLang('pt')" viewBox="0 0 640 480" width="24" height="18">
            <path fill="#d32e12" d="M240 0h400v480H240z"/>
            <path fill="#006600" d="M0 0h240v480H0z"/>
            <!-- ... simplified flag paths ... -->
        </svg>
    </div>
</div>
```

**Text Elements:**
```html
<h2 id="header-dro" data-i18n="dashboard.dro_title">🎯 Position (DRO)</h2>
<button class="btn" data-i18n="dashboard.zero_all">Zero All</button>
<input data-i18n-attr="placeholder:common.enter_value">
```

### 4. JavaScript Logic (`i18n.js`)
A small (<2KB) class that handles:
1.  **Language Detection**:
    - Checks `localStorage.getItem('language')`.
    - If empty, defaults to English (or a configured system default).
    - **Persistence**: Selections are saved to `localStorage` (per browser).
2.  **Loading**: Fetches the appropriate `data/i18n/{lang}.json` file.
3.  **Translation**:
    - `i18n.updatePage()`: Traverses the DOM for `[data-i18n]` and updates `.textContent`.
    - `i18n.t(key)`: Returns a translated string for dynamic use (e.g., in `AlertManager`).

### 5. Settings UI
- Add a **Language Selector** (Dropdown) to `settings.html`.
- On change, save to `localStorage` and reload the page to apply the new language resources.

## Implementation Plan

When ready to proceed, the implementation steps are:

1.  **Preparation**:
    - Create `data/shared/i18n.js`.
    - Create initial `data/i18n/en.json`.

2.  **Extraction (The Bulk of Work)**:
    - Go through `index.html` (sidebar), `dashboard.html`, `settings.html`, etc.
    - Extract text into `en.json`.
    - Add `data-i18n` attributes to HTML tags.

3.  **JavaScript Updates**:
    - Update `core.js` to initialize `I18nManager` before loading pages.
    - Scan JS files (`dashboard.js`, `settings.js`) for `AlertManager.add()` calls and string templates.
    - Replace strings with `i18n.t('key')`.

4.  **Translation**:
    - Duplicate `en.json` to `pt.json`.
    - Translate values to Portuguese (PT-PT).

## Performance Impact
- **Server**: Negligible. Static file serving matches existing pattern.
- **Client**: Minimal. One extra HTTP request (cached) for the JSON file. String replacement happens instantly on modern browsers.
