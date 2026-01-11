/**
 * Mini Charts - Lightweight Real-time Charting for Mock Mode
 * Works with minimal DOM and provides real-time visualization
 */

class MiniChart {
    constructor(containerId, options = {}) {
        this.container = document.getElementById(containerId);
        if (!this.container) {
            console.warn(`[MiniChart] Container ${containerId} not found`);
            return;
        }

        this.options = {
            width: options.width || 300,
            height: options.height || 150,
            maxDataPoints: options.maxDataPoints || 60,
            lineColor: options.lineColor || '#3b82f6',
            fillColor: options.fillColor || 'rgba(59, 130, 246, 0.1)',
            gridColor: options.gridColor || 'rgba(128, 128, 128, 0.2)',
            textColor: options.textColor || '#666',
            min: options.min !== undefined ? options.min : 'auto',
            max: options.max !== undefined ? options.max : 'auto',
            unit: options.unit || '',
            showGrid: options.showGrid !== false,
            showValues: options.showValues !== false,
            smooth: options.smooth !== false
        };

        this.data = [];
        this.canvas = null;
        this.ctx = null;
        this.animationFrame = null;

        this.init();
    }

    init() {
        // Create canvas
        this.canvas = document.createElement('canvas');
        this.canvas.width = this.options.width;
        this.canvas.height = this.options.height;
        this.canvas.style.width = '100%';
        this.canvas.style.height = 'auto';
        this.canvas.style.display = 'block';

        this.ctx = this.canvas.getContext('2d');
        this.container.appendChild(this.canvas);

        // Create value display if enabled
        if (this.options.showValues) {
            this.valueDisplay = document.createElement('div');
            this.valueDisplay.style.cssText = `
                margin-top: 8px;
                display: flex;
                justify-content: space-between;
                font-size: 12px;
                color: var(--text-secondary);
            `;
            this.container.appendChild(this.valueDisplay);
        }

        this.render();
    }

    addDataPoint(value) {
        this.data.push(value);

        // Keep only recent data
        if (this.data.length > this.options.maxDataPoints) {
            this.data.shift();
        }

        this.render();
    }

    render() {
        if (!this.ctx) return;

        const { width, height } = this.canvas;
        const ctx = this.ctx;

        // Clear canvas
        ctx.clearRect(0, 0, width, height);

        if (this.data.length < 2) return;

        // Calculate bounds
        let min = this.options.min === 'auto' ? Math.min(...this.data) : this.options.min;
        let max = this.options.max === 'auto' ? Math.max(...this.data) : this.options.max;

        // Add padding to range
        const range = max - min;
        if (this.options.min === 'auto') min -= range * 0.1;
        if (this.options.max === 'auto') max += range * 0.1;

        // Prevent division by zero
        if (max === min) {
            max = min + 1;
        }

        // Draw grid
        if (this.options.showGrid) {
            this.drawGrid(ctx, width, height, min, max);
        }

        // Draw line
        this.drawLine(ctx, width, height, min, max);

        // Update value display
        if (this.options.showValues && this.valueDisplay) {
            const current = this.data[this.data.length - 1];
            const avg = this.data.reduce((a, b) => a + b, 0) / this.data.length;

            this.valueDisplay.innerHTML = `
                <span>Current: <strong>${current.toFixed(1)}${this.options.unit}</strong></span>
                <span>Avg: <strong>${avg.toFixed(1)}${this.options.unit}</strong></span>
                <span>Max: <strong>${Math.max(...this.data).toFixed(1)}${this.options.unit}</strong></span>
            `;
        }
    }

    drawGrid(ctx, width, height, min, max) {
        ctx.strokeStyle = this.options.gridColor;
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 2]);

        // Horizontal grid lines (4 lines)
        for (let i = 0; i <= 4; i++) {
            const y = (height / 4) * i;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }

        ctx.setLineDash([]);
    }

    drawLine(ctx, width, height, min, max) {
        const range = max - min;
        const stepX = width / (this.options.maxDataPoints - 1);

        // Build path
        ctx.beginPath();

        this.data.forEach((value, index) => {
            const x = index * stepX;
            const y = height - ((value - min) / range) * height;

            if (index === 0) {
                ctx.moveTo(x, y);
            } else {
                if (this.options.smooth) {
                    // Smooth curve using quadratic bezier
                    const prevX = (index - 1) * stepX;
                    const prevY = height - ((this.data[index - 1] - min) / range) * height;
                    const cpX = (prevX + x) / 2;
                    const cpY = (prevY + y) / 2;
                    ctx.quadraticCurveTo(prevX, prevY, cpX, cpY);
                } else {
                    ctx.lineTo(x, y);
                }
            }
        });

        // Draw filled area
        if (this.options.fillColor) {
            ctx.lineTo(width, height);
            ctx.lineTo(0, height);
            ctx.closePath();
            ctx.fillStyle = this.options.fillColor;
            ctx.fill();
        }

        // Draw line
        ctx.strokeStyle = this.options.lineColor;
        ctx.lineWidth = 2;
        ctx.stroke();
    }

    destroy() {
        if (this.animationFrame) {
            cancelAnimationFrame(this.animationFrame);
        }
        if (this.canvas && this.canvas.parentNode) {
            this.canvas.parentNode.removeChild(this.canvas);
        }
        if (this.valueDisplay && this.valueDisplay.parentNode) {
            this.valueDisplay.parentNode.removeChild(this.valueDisplay);
        }
    }
}

/**
 * Multi-Series Chart for comparing multiple metrics
 */
class MiniMultiChart {
    constructor(containerId, options = {}) {
        this.container = document.getElementById(containerId);
        if (!this.container) {
            console.warn(`[MiniMultiChart] Container ${containerId} not found`);
            return;
        }

        this.options = {
            width: options.width || 400,
            height: options.height || 200,
            maxDataPoints: options.maxDataPoints || 60,
            series: options.series || [], // Array of {name, color, data}
            gridColor: options.gridColor || 'rgba(128, 128, 128, 0.2)',
            textColor: options.textColor || '#666',
            min: options.min !== undefined ? options.min : 'auto',
            max: options.max !== undefined ? options.max : 'auto',
            showGrid: options.showGrid !== false,
            showLegend: options.showLegend !== false
        };

        this.seriesData = new Map(); // Map of series name to data array
        this.canvas = null;
        this.ctx = null;

        this.init();
    }

    init() {
        // Create canvas
        this.canvas = document.createElement('canvas');
        this.canvas.width = this.options.width;
        this.canvas.height = this.options.height;
        this.canvas.style.width = '100%';
        this.canvas.style.height = 'auto';
        this.canvas.style.display = 'block';

        this.ctx = this.canvas.getContext('2d');
        this.container.appendChild(this.canvas);

        // Create legend if enabled
        if (this.options.showLegend) {
            this.legend = document.createElement('div');
            this.legend.style.cssText = `
                margin-top: 8px;
                display: flex;
                gap: 16px;
                flex-wrap: wrap;
                font-size: 12px;
            `;
            this.container.appendChild(this.legend);
        }

        // Initialize series
        this.options.series.forEach(s => {
            this.seriesData.set(s.name, []);
        });

        this.render();
    }

    addDataPoint(seriesName, value) {
        if (!this.seriesData.has(seriesName)) {
            console.warn(`[MiniMultiChart] Series ${seriesName} not found`);
            return;
        }

        const data = this.seriesData.get(seriesName);
        data.push(value);

        // Keep only recent data
        if (data.length > this.options.maxDataPoints) {
            data.shift();
        }

        this.render();
    }

    render() {
        if (!this.ctx) return;

        const { width, height } = this.canvas;
        const ctx = this.ctx;

        // Clear canvas
        ctx.clearRect(0, 0, width, height);

        // Calculate global bounds across all series
        let allValues = [];
        this.seriesData.forEach(data => allValues.push(...data));

        if (allValues.length < 2) return;

        let min = this.options.min === 'auto' ? Math.min(...allValues) : this.options.min;
        let max = this.options.max === 'auto' ? Math.max(...allValues) : this.options.max;

        const range = max - min;
        if (this.options.min === 'auto') min -= range * 0.1;
        if (this.options.max === 'auto') max += range * 0.1;

        if (max === min) max = min + 1;

        // Draw grid
        if (this.options.showGrid) {
            this.drawGrid(ctx, width, height);
        }

        // Draw each series
        this.options.series.forEach(series => {
            const data = this.seriesData.get(series.name);
            if (data && data.length > 0) {
                this.drawSeries(ctx, width, height, min, max, data, series.color);
            }
        });

        // Update legend
        if (this.options.showLegend && this.legend) {
            this.updateLegend();
        }
    }

    drawGrid(ctx, width, height) {
        ctx.strokeStyle = this.options.gridColor;
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 2]);

        for (let i = 0; i <= 4; i++) {
            const y = (height / 4) * i;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }

        ctx.setLineDash([]);
    }

    drawSeries(ctx, width, height, min, max, data, color) {
        const range = max - min;
        const stepX = width / (this.options.maxDataPoints - 1);

        ctx.beginPath();

        data.forEach((value, index) => {
            const x = index * stepX;
            const y = height - ((value - min) / range) * height;

            if (index === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        });

        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.stroke();
    }

    updateLegend() {
        this.legend.innerHTML = this.options.series.map(series => {
            const data = this.seriesData.get(series.name);
            const current = data.length > 0 ? data[data.length - 1] : 0;

            return `
                <div style="display: flex; align-items: center; gap: 6px;">
                    <div style="width: 12px; height: 12px; background: ${series.color}; border-radius: 2px;"></div>
                    <span style="color: var(--text-secondary);">${series.name}: <strong>${current.toFixed(1)}</strong></span>
                </div>
            `;
        }).join('');
    }

    destroy() {
        if (this.canvas && this.canvas.parentNode) {
            this.canvas.parentNode.removeChild(this.canvas);
        }
        if (this.legend && this.legend.parentNode) {
            this.legend.parentNode.removeChild(this.legend);
        }
    }
}

// Expose globally
window.MiniChart = MiniChart;
window.MiniMultiChart = MiniMultiChart;
