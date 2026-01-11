/**
 * Advanced Graph Visualizer
 * Real-time line charts with multiple data series, auto-scaling, and responsive design
 */

class GraphVisualizer {
    constructor(canvasId, options = {}) {
        this.canvas = document.getElementById(canvasId);

        // Initialize essential properties BEFORE canvas check to prevent broken objects
        this.series = new Map();
        this.seriesColors = ['#10b981', '#3b82f6', '#f59e0b', '#ef4444', '#8b5cf6', '#ec4899'];

        // Configuration - initialize early so methods don't crash
        this.config = {
            title: options.title || 'Graph',
            timeWindow: options.timeWindow || 300000, // 5 minutes in ms
            maxPoints: options.maxPoints || 300,
            updateInterval: options.updateInterval || 1000,
            yMin: options.yMin || 0,
            yMax: options.yMax || 100,
            autoScale: options.autoScale !== false,
            unit: options.unit || '%',
            showGrid: options.showGrid !== false,
            showLegend: options.showLegend !== false,
            ...options
        };

        if (!this.canvas) {
            console.error(`[Graph] Canvas element '${canvasId}' not found`);
            // Don't return - object is initialized enough that methods won't crash
            this.isDisabled = true;
            return;
        }

        this.ctx = this.canvas.getContext('2d');
        this.width = this.canvas.width;
        this.height = this.canvas.height;

        // Data storage - already initialized above
        // this.series = new Map(); // Moved to top
        this.colors = {
            bg: getComputedStyle(document.documentElement).getPropertyValue('--bg-primary') || '#ffffff',
            grid: getComputedStyle(document.documentElement).getPropertyValue('--border-color') || '#e5e7eb',
            text: getComputedStyle(document.documentElement).getPropertyValue('--text-primary') || '#000000',
            optimal: getComputedStyle(document.documentElement).getPropertyValue('--color-optimal') || '#10b981',
            normal: getComputedStyle(document.documentElement).getPropertyValue('--color-normal') || '#3b82f6',
            warning: getComputedStyle(document.documentElement).getPropertyValue('--color-warning') || '#f59e0b',
            critical: getComputedStyle(document.documentElement).getPropertyValue('--color-critical') || '#ef4444'
        };

        // Default series colors
        this.seriesColors = [
            this.colors.optimal,
            this.colors.normal,
            this.colors.warning,
            this.colors.critical,
            '#8b5cf6',
            '#ec4899'
        ];

        // Handle resize
        this.resizeObserver = new ResizeObserver(() => this.handleResize());
        this.resizeObserver.observe(this.canvas);

        // Animation
        this.animationFrame = null;
        this.lastDrawTime = 0;

        // Initial draw
        this.draw();
    }

    handleResize() {
        const rect = this.canvas.parentElement.getBoundingClientRect();
        this.canvas.width = rect.width;
        this.canvas.height = rect.height;
        this.width = this.canvas.width;
        this.height = this.canvas.height;
        this.draw();
    }

    addSeries(seriesName, color = null) {
        if (!this.series.has(seriesName)) {
            const colorIndex = this.series.size % this.seriesColors.length;
            this.series.set(seriesName, {
                data: [],
                color: color || this.seriesColors[colorIndex],
                visible: true
            });
        }
    }

    addDataPoint(seriesName, value, timestamp = null) {
        if (!this.series.has(seriesName)) {
            this.addSeries(seriesName);
        }

        const series = this.series.get(seriesName);
        const time = timestamp || Date.now();

        series.data.push({ time, value });

        // Keep only recent data points
        const cutoffTime = time - this.config.timeWindow;
        series.data = series.data.filter(p => p.time >= cutoffTime);

        // Limit total points
        if (series.data.length > this.config.maxPoints) {
            series.data = series.data.slice(-this.config.maxPoints);
        }

        // Trigger redraw
        this.draw();
    }

    toggleSeries(seriesName, visible = null) {
        if (this.series.has(seriesName)) {
            const series = this.series.get(seriesName);
            series.visible = visible !== null ? visible : !series.visible;
            this.draw();
        }
    }

    clear() {
        this.series.forEach(series => {
            series.data = [];
        });
        this.draw();
    }

    clearSeries(seriesName) {
        if (this.series.has(seriesName)) {
            this.series.get(seriesName).data = [];
            this.draw();
        }
    }

    getMinMax() {
        let min = Infinity;
        let max = -Infinity;

        this.series.forEach((series) => {
            if (!series.visible) return;
            series.data.forEach(p => {
                min = Math.min(min, p.value);
                max = Math.max(max, p.value);
            });
        });

        // Apply configured min/max
        min = Math.min(min, this.config.yMin);
        max = Math.max(max, this.config.yMax);

        if (min === Infinity) min = 0;
        if (max === -Infinity) max = 100;

        // Add 10% padding
        const range = max - min;
        const padding = range * 0.1;

        return {
            min: Math.max(0, min - padding),
            max: max + padding
        };
    }

    valueToCanvasY(value, yMin, yMax) {
        const padding = 60; // pixels
        const graphHeight = this.height - padding - 60; // 60px for bottom area (labels + legend)
        const normalized = (value - yMin) / (yMax - yMin);
        return padding + graphHeight - (normalized * graphHeight);
    }

    timeToCanvasX(time, minTime, maxTime) {
        const leftPadding = 50;
        const rightPadding = 20;
        const graphWidth = this.width - leftPadding - rightPadding;
        const normalized = (time - minTime) / (maxTime - minTime);
        return leftPadding + (normalized * graphWidth);
    }

    draw() {
        // Skip if canvas not available
        if (this.isDisabled || !this.canvas || !this.ctx) return;

        // Clear canvas
        this.ctx.fillStyle = this.colors.bg;
        this.ctx.fillRect(0, 0, this.width, this.height);

        // Get time range from visible data
        let minTime = Date.now() - this.config.timeWindow;
        let maxTime = Date.now();

        // Draw title
        this.drawTitle();

        // Draw grid
        if (this.config.showGrid) {
            this.drawGrid(minTime, maxTime);
        }

        // Get min/max values
        const { min: yMin, max: yMax } = this.getMinMax();

        // Draw Y-axis labels
        this.drawYAxisLabels(yMin, yMax);

        // Draw X-axis labels (time)
        this.drawXAxisLabels(minTime, maxTime);

        // Draw axes
        this.drawAxes();

        // Draw data series
        this.drawDataSeries(minTime, maxTime, yMin, yMax);

        // Draw legend
        if (this.config.showLegend) {
            this.drawLegend(yMin, yMax);
        }
    }

    drawTitle() {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = 'bold 14px Arial';
        this.ctx.textAlign = 'left';
        this.ctx.fillText(this.config.title, 10, 20);
    }

    drawGrid(minTime, maxTime) {
        this.ctx.strokeStyle = this.colors.grid;
        this.ctx.lineWidth = 1;
        this.ctx.globalAlpha = 0.3;

        // Vertical grid lines (time) - FIXED RELATIVE GRID
        let timeStep = 60000; // Default 1 minute
        if (this.config.timeWindow > 300000) timeStep = 180000;  // > 5m -> 3m
        if (this.config.timeWindow > 900000) timeStep = 300000;  // > 15m -> 5m
        if (this.config.timeWindow > 1800000) timeStep = 600000; // > 30m -> 10m

        // Iterate from 0 (Now) back to timeWindow
        for (let offset = 0; offset < this.config.timeWindow; offset += timeStep) {
            const t = maxTime - offset;
            const x = this.timeToCanvasX(t, minTime, maxTime);
            this.ctx.beginPath();
            this.ctx.moveTo(x, 50);
            this.ctx.lineTo(x, this.height - 60);
            this.ctx.stroke();
        }

        // Horizontal grid lines (values)
        const { min: yMin, max: yMax } = this.getMinMax();
        const valueStep = this.getGridStep(yMin, yMax);

        for (let v = Math.ceil(yMin / valueStep) * valueStep; v <= yMax; v += valueStep) {
            const y = this.valueToCanvasY(v, yMin, yMax);
            this.ctx.beginPath();
            this.ctx.moveTo(50, y);
            this.ctx.lineTo(this.width - 20, y);
            this.ctx.stroke();
        }

        this.ctx.globalAlpha = 1;
    }

    getGridStep(min, max) {
        const range = max - min;
        if (range === 0) return 1;

        // Target roughly 5-8 grid lines
        const targetStep = range / 6;
        const magnitude = Math.pow(10, Math.floor(Math.log10(targetStep)));
        const normalized = targetStep / magnitude;

        let step;
        if (normalized < 1.5) step = 1 * magnitude;
        else if (normalized < 3.5) step = 2 * magnitude;
        else if (normalized < 7.5) step = 5 * magnitude;
        else step = 10 * magnitude;

        return step;
    }

    drawYAxisLabels(yMin, yMax) {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = '11px Arial';
        this.ctx.textAlign = 'right';
        this.ctx.globalAlpha = 0.7;

        const step = this.getGridStep(yMin, yMax);

        for (let v = Math.ceil(yMin / step) * step; v <= yMax; v += step) {
            const y = this.valueToCanvasY(v, yMin, yMax);
            this.ctx.fillText(v.toFixed(0) + this.config.unit, 45, y + 4);
        }

        this.ctx.globalAlpha = 1;
    }

    drawXAxisLabels(minTime, maxTime) {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = '11px Arial';
        this.ctx.textAlign = 'center';
        this.ctx.globalAlpha = 0.7;

        let timeStep = 60000; // Default 1 minute
        if (this.config.timeWindow > 300000) timeStep = 180000;  // > 5m -> 3m
        if (this.config.timeWindow > 900000) timeStep = 300000;  // > 15m -> 5m
        if (this.config.timeWindow > 1800000) timeStep = 600000; // > 30m -> 10m

        // Iterate from 0 (Now) back to timeWindow - STATIC LABELS
        for (let offset = 0; offset <= this.config.timeWindow; offset += timeStep) {
            const t = maxTime - offset;
            const x = this.timeToCanvasX(t, minTime, maxTime);

            // Convert offset to label
            const secondsAgo = Math.round(offset / 1000);
            let label = '';

            if (secondsAgo === 0) {
                label = 'Now';
            } else if (secondsAgo < 60) {
                label = secondsAgo + 's ago';
            } else if (secondsAgo < 3600) {
                label = Math.round(secondsAgo / 60) + 'm ago';
            } else {
                label = Math.round(secondsAgo / 3600) + 'h ago';
            }

            // Draw label above legend
            this.ctx.fillText(label, x, this.height - 42);
        }

        this.ctx.globalAlpha = 1;
    }

    drawAxes() {
        this.ctx.strokeStyle = this.colors.text;
        this.ctx.lineWidth = 2;

        // Y-axis
        this.ctx.beginPath();
        this.ctx.moveTo(50, 50);
        this.ctx.lineTo(50, this.height - 60);
        this.ctx.stroke();

        // X-axis
        this.ctx.beginPath();
        this.ctx.moveTo(50, this.height - 60);
        this.ctx.lineTo(this.width - 20, this.height - 60);
        this.ctx.stroke();
    }

    drawDataSeries(minTime, maxTime, yMin, yMax) {
        const { min: actualMin, max: actualMax } = this.getMinMax();

        this.series.forEach((series, seriesName) => {
            if (!series.visible || series.data.length === 0) return;

            this.ctx.strokeStyle = series.color;
            this.ctx.lineWidth = 2;
            this.ctx.globalAlpha = 0.8;

            this.ctx.beginPath();

            for (let i = 0; i < series.data.length; i++) {
                const point = series.data[i];
                const x = this.timeToCanvasX(point.time, minTime, maxTime);
                const y = this.valueToCanvasY(point.value, actualMin, actualMax);

                if (i === 0) {
                    this.ctx.moveTo(x, y);
                } else {
                    this.ctx.lineTo(x, y);
                }
            }

            this.ctx.stroke();

            // Draw data points (dots)
            this.ctx.fillStyle = series.color;
            series.data.forEach(point => {
                const x = this.timeToCanvasX(point.time, minTime, maxTime);
                const y = this.valueToCanvasY(point.value, actualMin, actualMax);
                this.ctx.fillRect(x - 2, y - 2, 4, 4);
            });

            this.ctx.globalAlpha = 1;
        });
    }

    drawLegend(yMin, yMax) {
        const legendX = 60;
        const legendY = this.height - 30; // Moved up to tighten spacing (was -20)
        const spacing = 120;

        let index = 0;
        this.series.forEach((series, seriesName) => {
            // Only show visible series in legend
            if (!series.visible) return;

            const x = legendX + index * spacing;
            const y = legendY;

            // Draw color box
            this.ctx.fillStyle = series.color;
            this.ctx.globalAlpha = 0.8;
            this.ctx.fillRect(x, y, 10, 10);

            // Draw label
            this.ctx.fillStyle = this.colors.text;
            this.ctx.globalAlpha = 1;
            this.ctx.font = '11px Arial';
            this.ctx.textAlign = 'left';
            this.ctx.fillText(seriesName, x + 15, y + 9);

            index++;
        });
    }

    // Cleanup
    destroy() {
        if (this.resizeObserver) {
            this.resizeObserver.disconnect();
        }
        if (this.animationFrame) {
            cancelAnimationFrame(this.animationFrame);
        }
    }

    // Get statistics for current data
    getStats(seriesName) {
        if (!this.series.has(seriesName)) return null;

        const data = this.series.get(seriesName).data;
        if (data.length === 0) return null;

        const values = data.map(p => p.value);
        const avg = values.reduce((a, b) => a + b, 0) / values.length;
        const min = Math.min(...values);
        const max = Math.max(...values);
        const current = data[data.length - 1].value;

        return { avg, min, max, current, count: values.length };
    }

    // Export data as CSV
    exportData() {
        let csv = 'Time,' + Array.from(this.series.keys()).join(',') + '\n';

        if (this.series.size === 0) return csv;

        const allPoints = new Map();
        this.series.forEach((series, seriesName) => {
            series.data.forEach(p => {
                if (!allPoints.has(p.time)) {
                    allPoints.set(p.time, {});
                }
                allPoints.get(p.time)[seriesName] = p.value;
            });
        });

        const sortedTimes = Array.from(allPoints.keys()).sort((a, b) => a - b);
        const seriesNames = Array.from(this.series.keys());

        sortedTimes.forEach(time => {
            const row = [new Date(time).toISOString()];
            const values = allPoints.get(time);
            seriesNames.forEach(name => {
                row.push(values[name] || '');
            });
            csv += row.join(',') + '\n';
        });

        return csv;
    }
}
