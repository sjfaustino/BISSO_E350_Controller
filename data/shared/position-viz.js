/**
 * Real-Time Position Visualization
 * Displays 2D/3D workspace view with current position indicator
 */

class PositionVisualizer {
    constructor(canvasId, options = {}) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) {
            console.error(`[PosViz] Canvas element '${canvasId}' not found`);
            return;
        }

        this.ctx = this.canvas.getContext('2d');
        this.width = this.canvas.width;
        this.height = this.canvas.height;

        // Configuration
        this.config = {
            x_min: options.x_min || -100,
            x_max: options.x_max || 500,
            y_min: options.y_min || -100,
            y_max: options.y_max || 500,
            z_min: options.z_min || 0,
            z_max: options.z_max || 100,
            showGrid: options.showGrid !== false,
            showLimits: options.showLimits !== false,
            showTrail: options.showTrail !== false,
            ...options
        };

        // Current position
        this.position = {
            x: this.config.x_min,
            y: this.config.y_min,
            z: this.config.z_min,
            a: 0
        };

        // Trail tracking (last 100 points)
        this.trail = [];
        this.maxTrailLength = 100;

        // Colors
        this.colors = {
            bg: getComputedStyle(document.documentElement).getPropertyValue('--bg-primary') || '#ffffff',
            grid: getComputedStyle(document.documentElement).getPropertyValue('--border-color') || '#e5e7eb',
            limit: getComputedStyle(document.documentElement).getPropertyValue('--color-warning') || '#f59e0b',
            position: getComputedStyle(document.documentElement).getPropertyValue('--color-optimal') || '#10b981',
            trail: getComputedStyle(document.documentElement).getPropertyValue('--color-normal') || '#3b82f6',
            text: getComputedStyle(document.documentElement).getPropertyValue('--text-primary') || '#000000'
        };

        this.padding = 60; // Increased padding for axis labels

        // Handle resize
        this.resizeObserver = new ResizeObserver(() => this.handleResize());
        this.resizeObserver.observe(this.canvas);

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

    updatePosition(x, y, z, a) {
        this.position.x = x;
        this.position.y = y;
        this.position.z = z;
        this.position.a = a || 0;

        // Add to trail
        if (this.config.showTrail) {
            this.trail.push({ x, y, z });
            if (this.trail.length > this.maxTrailLength) {
                this.trail.shift();
            }
        }

        this.draw();
    }

    // Convert workspace coordinates to canvas coordinates
    toCanvasX(x) {
        const range = this.config.x_max - this.config.x_min;
        const normalized = (x - this.config.x_min) / range;
        return this.padding + normalized * (this.width - 2 * this.padding);
    }

    toCanvasY(y) {
        const range = this.config.y_max - this.config.y_min;
        const normalized = (y - this.config.y_min) / range;
        // Invert Y axis (canvas Y increases downward)
        return this.height - this.padding - normalized * (this.height - 2 * this.padding);
    }

    draw() {
        // Clear canvas
        this.ctx.fillStyle = this.colors.bg;
        this.ctx.fillRect(0, 0, this.width, this.height);

        // Draw grid
        if (this.config.showGrid) {
            this.drawGrid();
        }

        // Draw soft limits
        if (this.config.showLimits) {
            this.drawLimits();
        }

        // Draw trail
        if (this.config.showTrail && this.trail.length > 1) {
            this.drawTrail();
        }

        // Draw axes labels
        this.drawLabels();

        // Draw current position
        this.drawPosition();

        // Draw info box
        this.drawInfoBox();
    }

    drawGrid() {
        this.ctx.strokeStyle = this.colors.grid;
        this.ctx.lineWidth = 1;
        this.ctx.globalAlpha = 0.3;

        const gridStep = 50; // Draw grid every 50mm
        const range_x = this.config.x_max - this.config.x_min;
        const range_y = this.config.y_max - this.config.y_min;

        // Vertical lines (X axis)
        let x = Math.ceil(this.config.x_min / gridStep) * gridStep;
        while (x <= this.config.x_max) {
            const canvasX = this.toCanvasX(x);
            this.ctx.beginPath();
            this.ctx.moveTo(canvasX, this.padding);
            this.ctx.lineTo(canvasX, this.height - this.padding);
            this.ctx.stroke();
            x += gridStep;
        }

        // Horizontal lines (Y axis)
        let y = Math.ceil(this.config.y_min / gridStep) * gridStep;
        while (y <= this.config.y_max) {
            const canvasY = this.toCanvasY(y);
            this.ctx.beginPath();
            this.ctx.moveTo(this.padding, canvasY);
            this.ctx.lineTo(this.width - this.padding, canvasY);
            this.ctx.stroke();
            y += gridStep;
        }

        this.ctx.globalAlpha = 1;
    }

    drawLimits() {
        // Draw soft limit boundaries as dashed rectangles
        this.ctx.strokeStyle = this.colors.limit;
        this.ctx.lineWidth = 2;
        this.ctx.setLineDash([5, 5]);
        this.ctx.globalAlpha = 0.5;

        const x1 = this.toCanvasX(this.config.x_min);
        const y1 = this.toCanvasY(this.config.y_max);
        const x2 = this.toCanvasX(this.config.x_max);
        const y2 = this.toCanvasY(this.config.y_min);

        this.ctx.strokeRect(x1, y1, x2 - x1, y2 - y1);

        this.ctx.setLineDash([]);
        this.ctx.globalAlpha = 1;
    }

    drawTrail() {
        this.ctx.strokeStyle = this.colors.trail;
        this.ctx.lineWidth = 1.5;
        this.ctx.globalAlpha = 0.6;

        this.ctx.beginPath();
        for (let i = 0; i < this.trail.length; i++) {
            const point = this.trail[i];
            const canvasX = this.toCanvasX(point.x);
            const canvasY = this.toCanvasY(point.y);

            if (i === 0) {
                this.ctx.moveTo(canvasX, canvasY);
            } else {
                this.ctx.lineTo(canvasX, canvasY);
            }
        }
        this.ctx.stroke();

        this.ctx.globalAlpha = 1;
    }

    drawLabels() {
        this.ctx.fillStyle = this.colors.text;
        this.ctx.font = '12px Arial';
        this.ctx.textAlign = 'center';
        this.ctx.globalAlpha = 0.5;

        // X axis labels
        const step_x = 100;
        let x = Math.ceil(this.config.x_min / step_x) * step_x;
        while (x <= this.config.x_max) {
            const canvasX = this.toCanvasX(x);
            this.ctx.fillText(x + 'mm', canvasX, this.height - 20);
            x += step_x;
        }

        // Y axis labels
        this.ctx.textAlign = 'right';
        const step_y = 100;
        let y = Math.ceil(this.config.y_min / step_y) * step_y;
        while (y <= this.config.y_max) {
            const canvasY = this.toCanvasY(y);
            this.ctx.fillText(y + 'mm', this.padding - 8, canvasY + 4);
            y += step_y;
        }

        this.ctx.globalAlpha = 1;
    }

    drawPosition() {
        const x = this.toCanvasX(this.position.x);
        const y = this.toCanvasY(this.position.y);

        // Draw position circle
        this.ctx.fillStyle = this.colors.position;
        this.ctx.beginPath();
        this.ctx.arc(x, y, 8, 0, 2 * Math.PI);
        this.ctx.fill();

        // Draw crosshair
        this.ctx.strokeStyle = this.colors.position;
        this.ctx.lineWidth = 2;
        this.ctx.globalAlpha = 0.7;

        this.ctx.beginPath();
        this.ctx.moveTo(x - 15, y);
        this.ctx.lineTo(x + 15, y);
        this.ctx.stroke();

        this.ctx.beginPath();
        this.ctx.moveTo(x, y - 15);
        this.ctx.lineTo(x, y + 15);
        this.ctx.stroke();

        this.ctx.globalAlpha = 1;
    }

    drawInfoBox() {
        const padding = 10;
        const lineHeight = 18;
        const box_width = 150;
        const box_height = 90;

        // Move to top-right corner to avoid Y-axis overlap
        const x_pos = this.width - box_width - padding;
        const y_pos_start = padding;

        // Semi-transparent background
        this.ctx.fillStyle = 'rgba(0, 0, 0, 0.5)'; // Detached from theme for better contrast
        this.ctx.fillRect(x_pos, y_pos_start, box_width, box_height);

        // Border
        this.ctx.strokeStyle = this.colors.text;
        this.ctx.lineWidth = 1;
        this.ctx.globalAlpha = 0.5;
        this.ctx.strokeRect(x_pos, y_pos_start, box_width, box_height);
        this.ctx.globalAlpha = 1;

        // Text
        this.ctx.fillStyle = this.colors.position;
        this.ctx.font = 'bold 12px Arial';
        this.ctx.textAlign = 'left';

        let y_pos = y_pos_start + 20;

        // Use white text for better contrast on dark overlay
        this.ctx.fillStyle = '#ffffff';

        this.ctx.fillText(`X: ${this.position.x.toFixed(1)} mm`, x_pos + 10, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`Y: ${this.position.y.toFixed(1)} mm`, x_pos + 10, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`Z: ${this.position.z.toFixed(1)} mm`, x_pos + 10, y_pos);
        y_pos += lineHeight;
        this.ctx.fillText(`A: ${this.position.a.toFixed(1)}°`, x_pos + 10, y_pos);
    }

    // Reset trail
    clearTrail() {
        this.trail = [];
        this.draw();
    }

    // Cleanup
    destroy() {
        if (this.resizeObserver) {
            this.resizeObserver.disconnect();
        }
    }
}
