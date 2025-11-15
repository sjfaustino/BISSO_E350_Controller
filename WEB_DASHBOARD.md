# BISSO v4.2 Web Dashboard

## Overview

The BISSO v4.2 firmware includes a comprehensive web-based control dashboard accessible via HTTP on the ESP32.

## Features

### Dashboard Tab
- **System Status Monitor**: Real-time display of all axis positions (X, Y, Z, A)
- **System Mode**: Current operational status (READY, IDLE, RUNNING, ERROR, etc.)
- **Uptime Counter**: System uptime tracking
- **Status Cards**: Quick view of all critical parameters

### Jog Control Tab
- **Interactive Jog Pad**: 5-button cross interface for XY motion (⬆⬅🔘➡⬇)
- **Pause/Resume**: Center button for execution control (amber when paused, red when idle)
- **Z Controls**: Up/Down buttons for Z-axis (blue)
- **A Controls**: Rotation buttons for A-axis (amber/yellow)
- **Speed Selection**: SLOW/MEDIUM/FAST presets
- **LCD Display**: 4-line status display mirroring physical LCD
- **Theme Toggle**: Light (industrial white) / Dark (low-light) modes

### Settings Tab
- Configuration management (coming soon)
- Parameter adjustment
- Calibration controls

### Diagnostics Tab
- System diagnostics and logging (coming soon)
- Error history
- Performance metrics

## Accessing the Dashboard

### Local Network
1. Determine the ESP32's IP address from serial console or network scanner
2. Open browser and navigate to: `http://<ESP32_IP>/`

### Default Settings
- **Port**: 80 (HTTP)
- **Storage**: SPIFFS (internal flash)
- **Theme**: Light mode (industrial white background)

## Theme Configuration

### Via URL Parameter
Add theme selection to URL:
- Light mode: `http://<ESP32_IP>/?theme=light`
- Dark mode: `http://<ESP32_IP>/?theme=dark`

### Via Toggle Button
Click the ☀️/🌙 button in top-right to switch themes

### Persistent Storage
Theme preference is saved in browser localStorage

## Web Files

All web files are stored in `/data/` directory:

```
data/
├── index.html          # Main dashboard with tabs
├── jog.html            # Jog control interface
└── (additional assets as needed)
```

## API Endpoints

### GET /api/status
Returns current system status in JSON format:
```json
{
  "status": "READY",
  "x_pos": 0.000,
  "y_pos": 0.000,
  "z_pos": 25.000,
  "a_pos": 0.000,
  "uptime": 3600
}
```

### POST /api/jog
Execute jog command with JSON body:
```json
{
  "direction": "X+",
  "distance": 1.0,
  "speed": "MEDIUM"
}
```

Valid directions: `X+`, `X-`, `Y+`, `Y-`, `Z+`, `Z-`, `A+`, `A-`

### POST /api/settings
Update system settings (coming soon)

### GET /api/diagnostics
Retrieve system diagnostics (coming soon)

## Responsive Design

The dashboard automatically adapts to different screen sizes:
- **Desktop** (769px+): Full layout with multi-column grids
- **Tablet** (481-768px): Optimized grid layout
- **Mobile** (≤480px): Single-column, touch-optimized controls

## Color Scheme (Light Mode)

- **Background**: Clean white (#f5f5f5 to #ffffff)
- **Blue (#2196f3)**: Motion controls (X/Y/Z motion)
- **Yellow (#ffc107)**: Pause/Resume and A-axis
- **Red (#e53935)**: Emergency Stop
- **Text**: Dark gray for readability (#1a1a1a)

## Color Scheme (Dark Mode)

- **Background**: Dark charcoal (#1a1a1a to #2d2d2d)
- **Blue (#2563eb)**: Motion controls
- **Yellow (#eab308)**: Pause/Resume and A-axis
- **Red (#dc2626)**: Emergency Stop
- **LCD Green**: Traditional LCD display (#1aff1a)
- **Text**: Light gray for visibility (#e8e8e8)

## Development Notes

### Adding New Tabs
1. Add tab button in `index.html`:
   ```html
   <button class="tab-button" data-tab="new-tab">Label</button>
   ```

2. Add content div:
   ```html
   <div id="new-tab" class="tab-content">
       <!-- Content here -->
   </div>
   ```

3. Tab switching is automatic via JavaScript event listeners

### Styling
- Tailored CSS for industrial control panel appearance
- Beveled/embossed button effects for tactile feedback
- Mobile-first responsive design using CSS Grid

### Browser Compatibility
- Chrome/Edge: Full support
- Firefox: Full support
- Safari: Full support
- Mobile browsers: Full support with touch optimization

## Troubleshooting

### Dashboard Not Loading
1. Check ESP32 is powered and connected to network
2. Verify SPIFFS is properly mounted
3. Check serial console for boot messages
4. Verify web files are in `/data/` directory

### Jog Commands Not Working
1. Ensure motion system is initialized
2. Check safety interlocks are satisfied
3. Verify axis is enabled via CLI configuration
4. Monitor serial console for error messages

### Unresponsive Buttons
1. Check browser console for JavaScript errors
2. Verify web server is running (`[WEB] Web server started` in console)
3. Try refreshing page (Ctrl+F5)
4. Clear browser cache

## Future Enhancements

- Real-time graph plotting for axis positions
- Motion program editor and playback
- Remote file upload/download
- System firmware updates via web UI
- Advanced diagnostics and fault analysis
- Multi-user authentication
- MQTT integration for remote monitoring

---

**Version**: 4.2.0
**Last Updated**: November 15, 2025
