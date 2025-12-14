# Mock Data Mode - Offline Dashboard Preview

This guide explains how to use the Mock Data Mode to preview the dashboard offline with realistic simulated data.

## Quick Start

### Option 1: Press 'M' Key
Simply press the **M** key while on the dashboard to toggle mock mode on/off.
- No configuration needed
- Works instantly
- Status indicator shows "Mock Mode" in dashed border when active

### Option 2: URL Parameter
Add `?mock=true` to the dashboard URL:
```
http://localhost:8080/?mock=true
```
Mock mode will automatically start when the page loads.

### Option 3: Console Command
Open browser DevTools (F12) and run:
```javascript
MockMode.toggle()      // Toggle mock mode on/off
MockMode.enable()      // Enable mock mode
MockMode.disable()     // Disable mock mode
```

## What Gets Simulated

The mock data generator creates realistic, animated data for all dashboard components:

### System Metrics
- **CPU Usage**: Varies 5-95% with smooth oscillation and noise
- **Memory**: Fluctuates 100-230KB with realistic heap behavior
- **Temperature**: Increases under load, ranges 25-75°C
- **Uptime**: Real-time counter from session start

### Motion & Axis Control
- **X, Y, Z Position**: Smooth sinusoidal motion patterns
- **Rotation (A)**: Continuous rotation simulation
- **Quality Metrics**: 65-100% with realistic variance
- **Jitter**: ±0.5mm/s with random fluctuations
- **Status**: Alternates between "Moving" and "Stopped" every 15 seconds

### Spindle/VFD
- **Frequency**: Ramps up/down (0-18kHz) smoothly
- **Current**: 0-8A with load-based variation
- **Voltage**: 375-385V (simulating AC supply)
- **Temperature**: 35-65°C under load

### Network
- **WiFi Signal**: 75-95% signal strength
- **Signal DBM**: -50 to -40 dBm
- **Latency**: 15-35ms with jitter
- **Status**: Always connected (for preview)

### Graphs & Charts
All graphs populate with real mock data:
- CPU Usage graph (0-100%)
- Memory graph (100-230KB)
- Spindle Current graph (0-8A)
- Temperature graph (25-75°C)
- WebSocket Latency graph (15-35ms)
- Motion Load graph (dual series)

## Features

✅ **No Network Required** - Works completely offline
✅ **Realistic Data** - Simulates actual device behavior patterns
✅ **Smooth Animation** - 100ms update rate for fluid visuals
✅ **Theme Compatible** - Respects light/dark theme settings
✅ **All UI Elements** - Every dashboard section shows real data
✅ **Mobile Responsive** - Full mobile UI preview support
✅ **Easy Toggle** - Single key press to enable/disable

## Use Cases

1. **UI Mockup Review**: See the full dashboard layout with realistic content
2. **Responsive Testing**: Check mobile/tablet layouts with actual data
3. **Theme Testing**: Preview light/dark themes with populated content
4. **Feature Planning**: Evaluate UI changes without device connection
5. **Offline Development**: Work on the dashboard without hardware access
6. **Performance Testing**: Test graph rendering with continuous data streams

## Data Generation Details

The mock data generator produces realistic patterns:

- **CPU**: `base(30% + 15% sin wave) + noise(±2.5%)`
- **Memory**: `base(200KB + 30KB sin wave) + noise(±2.5KB)`
- **Temperature**: `base(35°C + 8°C sin wave) + noise(±1°C)`
- **Position**: `sin/cos waves with realistic velocities`
- **Quality**: `85% + noise(±10%)`
- **Jitter**: `±0.5mm/s with random fluctuations`
- **Latency**: `20ms base + noise(±7.5ms)`

All values are constrained to realistic hardware ranges.

## Visual Indicator

When mock mode is active:
- Status dot in header has **dashed border** instead of solid
- Status text shows **"Mock Mode"** instead of "Online"
- Alert notification confirms activation
- Press 'M' again to disable

## Examples

### Preview the Full Dashboard Offline
1. Open the dashboard URL
2. Press **M** to enable mock mode
3. Explore all pages (Dashboard, Motion, Network, System, etc.)
4. All graphs, charts, and metrics show simulated real data

### Test Mobile Responsiveness
1. Open DevTools (F12) and toggle Device Emulation
2. Enable mock mode (**M** key)
3. Resize viewport to test different breakpoints
4. All elements respond with real simulated data

### Evaluate UI Changes
1. Enable mock mode before design review
2. Show stakeholders the full dashboard with data
3. Make notes on any desired changes
4. Implement changes knowing exact data context

### Develop Without Hardware
1. Start dashboard with mock mode enabled
2. Work on UI improvements offline
3. See immediate feedback with animated data
4. Test before deploying to actual device

## Technical Details

- **Update Rate**: 100ms (10 updates/sec) - matches real device
- **Data Cycle**: Periodic patterns (30-60 second full cycles)
- **Memory**: Mock data is lightweight, minimal performance impact
- **Browser Compatibility**: Works in all modern browsers
- **No Storage**: Mock mode doesn't save any data

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| M | Toggle mock mode on/off |
| T | Toggle theme (dark/light) |
| Esc | Close modals/menus |

## Troubleshooting

**Q: Mock mode doesn't work**
- A: Make sure you're not typing in a text input (M key is disabled in inputs)
- Check browser console (F12) for any error messages
- Reload page and try again

**Q: Data looks frozen**
- A: Check if the page is visible (some browsers pause animations in inactive tabs)
- Try toggling mock mode off and on again
- Open DevTools console to verify updates: `MockMode.mockWs.updateRate`

**Q: Mock mode keeps disconnecting**
- A: Check browser console for WebSocket errors
- Try refreshing the page
- Ensure you have the latest code (git pull)

## Future Enhancements

Potential improvements to mock data:
- [ ] Recorded data replay from actual device sessions
- [ ] Configurable data patterns (stress test, idle, normal operation)
- [ ] Scenario playback (fault conditions, alarms)
- [ ] Data export for UI testing documentation
- [ ] Multi-device simulation (e.g., two machines)

## Files

- `spiffs/shared/mock-data.js` - Mock data generator and WebSocket replacement
- `spiffs/index.html` - Includes mock-data.js script
- This guide: `MOCK_MODE_GUIDE.md`
