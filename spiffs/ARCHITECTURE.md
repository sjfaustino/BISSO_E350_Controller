# BISSO E350 Dashboard - Multi-File Architecture

## Overview

The web interface has been refactored from a monolithic 2100+ line `dashboard.html` into a modular, scalable multi-file architecture.

## Directory Structure

```
spiffs/
├── index.html                    # Main shell with navigation & alerts
├── shared/
│   ├── websocket.js              # Shared WebSocket connection (80 lines)
│   ├── state.js                  # Centralized app state management (150 lines)
│   ├── alerts.js                 # Alert manager with sound/notifications (100 lines)
│   ├── theme.js                  # Theme & accessibility settings (100 lines)
│   └── router.js                 # Client-side page router (150 lines)
├── css/
│   ├── variables.css             # CSS variables for theming (200 lines)
│   ├── layout.css                # Header, nav, main container (250 lines)
│   ├── cards.css                 # Card components & grids (250 lines)
│   ├── charts.css                # Chart styling & legends (200 lines)
│   └── responsive.css            # Mobile/tablet breakpoints (300 lines)
└── pages/
    ├── dashboard/
    │   ├── dashboard.html        # Dashboard metrics cards (200 lines)
    │   ├── dashboard.js          # Dashboard logic & chart rendering (180 lines)
    │   └── dashboard.css         # Dashboard-specific styles (optional)
    ├── motion/
    │   ├── motion.html           # Jog controls, quick presets
    │   ├── motion.js             # Motion control logic
    │   └── motion.css
    ├── diagnostics/
    │   ├── diagnostics.html      # Axis quality, encoder health
    │   ├── diagnostics.js
    │   └── diagnostics.css
    ├── maintenance/
    │   ├── maintenance.html      # Wear tracking, service history
    │   ├── maintenance.js
    │   └── maintenance.css
    ├── logs/
    │   ├── logs.html             # Fault logs, operation history
    │   ├── logs.js
    │   └── logs.css
    └── settings/
        ├── settings.html         # User preferences, thresholds
        ├── settings.js
        └── settings.css
```

## Key Features

### 1. **Shared Modules** (no duplication across pages)

#### `shared/websocket.js`
- Single WebSocket connection reused by all pages
- Auto-reconnect with exponential backoff
- Emits `telemetry` events to all listeners
- Connection status: `ws-connected`, `ws-disconnected`, `ws-error`

```javascript
// Usage in any page:
window.addEventListener('telemetry', (event) => {
    const data = event.detail;  // { system, motion, axis, vfd, ... }
    updateDisplay(data);
});
```

#### `shared/state.js`
- Centralized application state (immutable updates)
- Automatic history tracking (1440 samples = 24 hours @ 1/min)
- Reactive updates via `state-changed` events

```javascript
// Get state:
const cpuUsage = AppState.get('system.cpu_percent');

// Update state:
AppState.update({ system: { cpu_percent: 45 } });

// Subscribe to changes:
AppState.subscribe(() => console.log('State changed'));
```

#### `shared/alerts.js`
- Unified alert system for all pages
- Auto-dismiss alerts with configurable duration
- Sound alerts for critical events
- Message queue with max 100 alerts

```javascript
AlertManager.add('Motion stalled!', 'critical', null);  // No auto-dismiss
AlertManager.add('CPU warning', 'warning', 5000);       // Auto-dismiss after 5s
```

#### `shared/theme.js`
- Theme management (light, dark, high-contrast, colorblind)
- Font size control (80-120%)
- Settings persistence to localStorage
- Keyboard shortcut: `T` key toggles themes

#### `shared/router.js`
- Client-side page navigation with hash routing
- Lazy-loads page HTML & JS only when needed
- Auto-loads page-specific CSS
- Calls `init()` and `cleanup()` on page modules

### 2. **CSS Framework** (reusable components)

All colors defined as CSS variables for easy theming. Framework includes:

- **Card system** - Responsive grid with hover effects
- **Progress bars** - Colored states (optimal, warning, critical)
- **Charts** - Flexible canvas containers with legends
- **Buttons** - 5 variants (primary, success, warning, danger, ghost)
- **Forms** - Inputs, selects, toggles, sliders
- **Responsive** - Mobile-first breakpoints (480px, 768px, 992px, 1200px, 1920px)

### 3. **Page Modules** (isolated logic)

Each page is a standalone module with this pattern:

```javascript
const PageModule = {
    init() {
        // Initialize when page loads
        this.setupEventListeners();
    },

    onStateChanged() {
        // Handle state updates
        const state = AppState.data;
        // ... update UI ...
    },

    cleanup() {
        // Cleanup when leaving page
        // Remove listeners, cancel timers, etc.
    }
};

window.currentPageModule = PageModule;
```

## Performance Benefits

### Before (Monolithic)
- 2100+ lines loaded upfront (~150KB+)
- All CSS for all pages loaded
- Single large JavaScript file
- Hard to maintain & extend
- Slow initial load

### After (Modular)
```
index.html + shared/* loaded once:
  - index.html: 5KB
  - websocket.js: 3KB
  - state.js: 5KB
  - alerts.js: 3KB
  - theme.js: 4KB
  - router.js: 5KB
  - CSS framework: 25KB
  ────────────────────
  Total initial: 50KB

Per-page load (e.g., Dashboard):
  - dashboard.html: 8KB
  - dashboard.js: 7KB
  - (shared CSS already loaded)
  ────────────────────
  Additional: 15KB

Total for first page: 65KB (vs 150KB+)
Subsequent pages: only 15KB each (very fast)
```

## Navigation Flow

1. User clicks nav link: `#dashboard`
2. `Router.navigate('dashboard')` triggered
3. Fetch `pages/dashboard/dashboard.html`
4. Insert into `#page-container`
5. Load `pages/dashboard/dashboard.js`
6. Call `DashboardModule.init()`
7. Subscribe to state changes
8. Page renders and stays responsive

## Adding New Pages

To add a new feature page (e.g., `trends`):

1. Create directory: `spiffs/pages/trends/`
2. Create files:
   ```
   pages/trends/trends.html     # UI markup
   pages/trends/trends.js       # Page logic
   pages/trends/trends.css      # Optional: page-specific styles
   ```
3. Add to router in `shared/router.js`:
   ```javascript
   'trends': {
       file: 'pages/trends/trends.html',
       js: 'pages/trends/trends.js'
   }
   ```
4. Add nav link in `index.html`:
   ```html
   <li><a href="#trends" class="nav-item">📈 Trends</a></li>
   ```

## Shared Data Flow

```
┌─────────────────────────────────────┐
│        WebSocket (ESP32)            │
│  { system, motion, axis, vfd, ... } │
└──────────────┬──────────────────────┘
               │
               ▼
    ┌──────────────────────┐
    │ SharedWebSocket.js   │
    │ onmessage handler    │
    └──────────┬───────────┘
               │
               ▼
    ┌──────────────────────┐
    │  window.dispatchEvent │
    │  'telemetry' event    │
    └──────────┬───────────┘
               │
               ▼
    ┌──────────────────────┐
    │   AppState.update()  │
    │ Merge new data       │
    │ Record history       │
    └──────────┬───────────┘
               │
               ▼
    ┌──────────────────────┐
    │ window.dispatchEvent │
    │ 'state-changed'      │
    └──────────┬───────────┘
               │
               ▼
    ┌──────────────────────────────────┐
    │  Each active page's              │
    │  onStateChanged() handler        │
    │  Updates UI with new data        │
    └──────────────────────────────────┘
```

## Example: Dashboard Page

### dashboard.html
- System health cards (CPU, memory, health)
- Trends chart (CPU, memory, spindle current)
- Axis motion quality cards (X, Y, Z)
- Motion/safety/VFD status
- Network connection status

### dashboard.js
- Listens to `state-changed` events
- Updates metric displays
- Maintains 60-sample history buffer
- Renders trends chart with time-range selector (1m, 5m, 1h, 24h)
- Card collapse/expand toggles

### dashboard.css
- Optional page-specific styling
- Can override CSS variables for this page only

## Future Pages (Ready to Implement)

### Motion Control Page
- XYZ jog controls (1mm, 5mm, 10mm, 25mm steps)
- Home all axes button
- E-STOP with confirmation
- Quick position presets (home, park, corners)
- Real-time position display

### Diagnostics Page
- Per-axis detailed metrics
- Encoder health indicators
- VFD register inspector (Modbus reads)
- Signal quality dashboard
- Timing/latency analysis

### Maintenance Page
- Bearing wear predictor (jitter trends)
- Service history log (before/after quality scores)
- Component lifetime calculator
- Recommended maintenance calendar
- Parts inventory tracking

### Logs Page
- Fault log viewer with filtering
- Operation history (who moved what when)
- Export to CSV/JSON
- Real-time tail of new logs
- Search & highlight

### Settings Page
- Theme selector
- Alert threshold configuration
- Font size adjuster
- Auto-refresh interval
- Data retention policy
- User preferences

## CSS Variables Available

All defined in `css/variables.css` for easy theming:

### Colors
```css
--bg-primary, --bg-secondary, --bg-tertiary
--text-primary, --text-secondary, --text-muted
--border-color
--color-optimal (green), --color-normal (blue), --color-warning (amber), --color-critical (red)
--chart-cpu, --chart-mem, --chart-spindle
```

### Spacing
```css
--space-xs (4px), --space-sm (8px), --space-md (16px), --space-lg (20px), --space-xl (32px)
```

### Typography
```css
--font-family
--font-size-sm, --font-size-base, --font-size-lg, --font-size-xl, --font-size-2xl
```

### Transitions
```css
--transition-fast (0.15s), --transition-normal (0.3s), --transition-slow (0.5s)
```

## Browser Support

- Chrome 60+
- Firefox 55+
- Safari 12+
- Edge 79+
- Mobile browsers (iOS Safari 12+, Chrome Android)

Gracefully degrades in older browsers (no WebSocket, no CSS Grid).

## Development Tips

1. **Hot reload**: Edit HTML/JS/CSS and refresh browser
2. **DevTools**: Use Network tab to verify lazy-loading
3. **State inspection**: Open console and type `AppState.data` to inspect
4. **Alerts**: Use `AlertManager.add('test', 'info', 2000)` to test
5. **Theme toggle**: Press `T` key to cycle themes

## Performance Targets

- Initial load: < 200ms
- Per-page navigation: < 100ms
- State update → render: < 50ms
- WebSocket latency: 20-50ms (typical)
- Memory usage: < 5MB
- CPU usage: < 5% idle

## Future Optimizations

- [ ] Service Worker for offline mode
- [ ] IndexedDB for larger history buffers
- [ ] Code splitting for large pages
- [ ] Image optimization & lazy loading
- [ ] GZIP compression for all assets
- [ ] HTTP caching headers
- [ ] Minification & tree-shaking
- [ ] Web Workers for heavy computations
