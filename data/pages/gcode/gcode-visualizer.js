/**
 * gcode-visualizer.js - G-Code Path Preview Class
 * Renders a 3D isometric preview of the toolpath derived from G-code
 */

window.GCodeVisualizer = (function () {
    let canvas, ctx;
    let paths = []; // Array of path segments {type: 'rapid/feed', points: [{x,y,z}]}
    let currentPos = { x: 0, y: 0, z: 0 };
    let bounds = { minPX: 0, maxPX: 100, minPY: 0, maxPY: 100 };
    let scale = 1.0;
    let offset = { x: 0, y: 0 };
    let machineLimits = { xMin: 0, xMax: 500, yMin: 0, yMax: 500, zMin: 0, zMax: 100 };

    // Isometric projection constants
    const cos30 = 0.866;
    const sin30 = 0.5;

    /**
     * Project 3D coordinate to 2D screen coordinate (unscaled)
     * Positive PY is UP in isometric space
     */
    function project(x, y, z) {
        return {
            px: (x - y) * cos30,
            py: z - (x + y) * sin30
        };
    }

    return {
        init() {
            canvas = document.getElementById('gcode-preview');
            if (!canvas) return false;

            ctx = canvas.getContext('2d');
            this.handleResize();
            window.addEventListener('resize', () => this.handleResize());

            // Add basic interaction: mouse wheel zoom
            canvas.addEventListener('wheel', (e) => {
                e.preventDefault();
                const zoom = e.deltaY > 0 ? 0.9 : 1.1;
                this.zoom(zoom);
            });

            // Initial clear
            this.clear();
            return true;
        },

        handleResize() {
            if (!canvas) return;
            const rect = canvas.parentElement.getBoundingClientRect();
            canvas.width = rect.width || 400;
            canvas.height = 300; // Fixed height from CSS
            this.draw();
        },

        parse(gcodeText) {
            console.log('[Visualizer] Parsing G-code text (3D Isometric FIXED)...');
            const lines = gcodeText.split('\n');
            paths = [];
            let lastPos = { x: 0, y: 0, z: 0 };
            let currentPath = null;
            let absoluteMode = true;

            for (let line of lines) {
                line = line.split(';')[0].trim(); // Remove comments
                if (!line) continue;

                if (line.includes('G90')) absoluteMode = true;
                if (line.includes('G91')) absoluteMode = false;

                const isG0 = line.startsWith('G0') || line.startsWith('G00');
                const isG1 = line.startsWith('G1') || line.startsWith('G01');
                const isG28 = line.startsWith('G28');

                if (isG0 || isG1 || isG28) {
                    const xMatch = line.match(/X(-?\d+\.?\d*)/i);
                    const yMatch = line.match(/Y(-?\d+\.?\d*)/i);
                    const zMatch = line.match(/Z(-?\d+\.?\d*)/i);

                    let nextX = lastPos.x;
                    let nextY = lastPos.y;
                    let nextZ = lastPos.z;

                    if (isG28) {
                        nextX = 0; nextY = 0; nextZ = 0;
                    } else {
                        if (xMatch) nextX = absoluteMode ? parseFloat(xMatch[1]) : lastPos.x + parseFloat(xMatch[1]);
                        if (yMatch) nextY = absoluteMode ? parseFloat(yMatch[1]) : lastPos.y + parseFloat(yMatch[1]);
                        if (zMatch) nextZ = absoluteMode ? parseFloat(zMatch[1]) : lastPos.z + parseFloat(zMatch[1]);
                    }

                    const type = isG0 ? 'rapid' : 'feed';

                    if (!currentPath || currentPath.type !== type) {
                        currentPath = { type, points: [{ x: lastPos.x, y: lastPos.y, z: lastPos.z }] };
                        paths.push(currentPath);
                    }

                    currentPath.points.push({ x: nextX, y: nextY, z: nextZ });
                    lastPos = { x: nextX, y: nextY, z: nextZ };
                }
            }

            this.updateBounds();
            this.resetView();
            this.draw();
        },

        updateBounds() {
            if (paths.length === 0) {
                bounds = { minPX: -100, maxPX: 100, minPY: -50, maxPY: 150 };
                return;
            }

            let minPX = Infinity, maxPX = -Infinity, minPY = Infinity, maxPY = -Infinity;

            // Boundary points for machine limits too
            const limitPoints = [
                { x: machineLimits.xMin, y: machineLimits.yMin, z: machineLimits.zMin },
                { x: machineLimits.xMax, y: machineLimits.yMin, z: machineLimits.zMin },
                { x: machineLimits.xMin, y: machineLimits.yMax, z: machineLimits.zMin },
                { x: machineLimits.xMax, y: machineLimits.yMax, z: machineLimits.zMin },
                { x: machineLimits.xMin, y: machineLimits.yMin, z: machineLimits.zMax },
                { x: machineLimits.xMax, y: machineLimits.yMin, z: machineLimits.zMax },
                { x: machineLimits.xMin, y: machineLimits.yMax, z: machineLimits.zMax },
                { x: machineLimits.xMax, y: machineLimits.yMax, z: machineLimits.zMax }
            ];

            limitPoints.forEach(pt => {
                const proj = project(pt.x, pt.y, pt.z);
                minPX = Math.min(minPX, proj.px);
                maxPX = Math.max(maxPX, proj.px);
                minPY = Math.min(minPY, proj.py);
                maxPY = Math.max(maxPY, proj.py);
            });

            paths.forEach(p => {
                p.points.forEach(pt => {
                    const proj = project(pt.x, pt.y, pt.z);
                    minPX = Math.min(minPX, proj.px);
                    maxPX = Math.max(maxPX, proj.px);
                    minPY = Math.min(minPY, proj.py);
                    maxPY = Math.max(maxPY, proj.py);
                });
            });

            // Add margin
            const margin = 20;
            bounds = {
                minPX: minPX - margin,
                maxPX: maxPX + margin,
                minPY: minPY - margin,
                maxPY: maxPY + margin
            };
        },

        resetView() {
            if (!canvas) return;
            const w = canvas.width, h = canvas.height;
            const bW = bounds.maxPX - bounds.minPX;
            const bH = bounds.maxPY - bounds.minPY;

            const scaleX = (w - 40) / bW;
            const scaleY = (h - 40) / bH;
            scale = Math.min(scaleX, scaleY);
            if (!isFinite(scale) || scale === 0) scale = 1.0;

            offset.x = (w / 2) - ((bounds.minPX + bounds.maxPX) / 2 * scale);
            offset.y = (h / 2) + ((bounds.minPY + bounds.maxPY) / 2 * scale);
        },

        zoom(factor) {
            scale *= factor;
            this.draw();
        },

        updatePosition(x, y, z) {
            currentPos = { x, y, z };
            this.draw();
        },

        setWorkLimits(limits) {
            machineLimits = { ...machineLimits, ...limits };
            this.draw();
        },

        clear() {
            if (!ctx) return;
            ctx.fillStyle = '#1a1a2e';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            this.drawGrid();
        },

        drawGrid() {
            ctx.strokeStyle = '#2d2d44';
            ctx.lineWidth = 1;

            const step = 50;
            const z = machineLimits.zMin;

            // Draw X lines
            for (let y = machineLimits.yMin; y <= machineLimits.yMax; y += step) {
                const p1 = project(machineLimits.xMin, y, z);
                const p2 = project(machineLimits.xMax, y, z);
                ctx.beginPath();
                ctx.moveTo(p1.px * scale + offset.x, offset.y - p1.py * scale);
                ctx.lineTo(p2.px * scale + offset.x, offset.y - p2.py * scale);
                ctx.stroke();
            }

            // Draw Y lines
            for (let x = machineLimits.xMin; x <= machineLimits.xMax; x += step) {
                const p1 = project(x, machineLimits.yMin, z);
                const p2 = project(x, machineLimits.yMax, z);
                ctx.beginPath();
                ctx.moveTo(p1.px * scale + offset.x, offset.y - p1.py * scale);
                ctx.lineTo(p2.px * scale + offset.x, offset.y - p2.py * scale);
                ctx.stroke();
            }

            // Draw Axis Origin
            ctx.strokeStyle = '#4d4d66';
            ctx.lineWidth = 2;
            const o = project(0, 0, 0);
            const ox = o.px * scale + offset.x;
            const oy = offset.y - o.py * scale;

            // Z Axis
            ctx.beginPath();
            ctx.moveTo(ox, oy);
            const oZ = project(0, 0, 50);
            ctx.lineTo(oZ.px * scale + offset.x, offset.y - oZ.py * scale);
            ctx.stroke();
        },

        draw() {
            if (!ctx || !canvas) return;
            this.clear();

            // Draw Machine Limits (Box)
            ctx.strokeStyle = '#ef4444';
            ctx.lineWidth = 1;
            ctx.setLineDash([5, 5]);

            const corners = [
                project(machineLimits.xMin, machineLimits.yMin, machineLimits.zMin),
                project(machineLimits.xMax, machineLimits.yMin, machineLimits.zMin),
                project(machineLimits.xMax, machineLimits.yMax, machineLimits.zMin),
                project(machineLimits.xMin, machineLimits.yMax, machineLimits.zMin),
                project(machineLimits.xMin, machineLimits.yMin, machineLimits.zMax),
                project(machineLimits.xMax, machineLimits.yMin, machineLimits.zMax),
                project(machineLimits.xMax, machineLimits.yMax, machineLimits.zMax),
                project(machineLimits.xMin, machineLimits.yMax, machineLimits.zMax)
            ];

            const drawEdge = (i, j) => {
                ctx.beginPath();
                ctx.moveTo(corners[i].px * scale + offset.x, offset.y - corners[i].py * scale);
                ctx.lineTo(corners[j].px * scale + offset.x, offset.y - corners[j].py * scale);
                ctx.stroke();
            };

            // Bottom
            drawEdge(0, 1); drawEdge(1, 2); drawEdge(2, 3); drawEdge(3, 0);
            // Top
            drawEdge(4, 5); drawEdge(5, 6); drawEdge(6, 7); drawEdge(7, 4);
            // Verticals
            drawEdge(0, 4); drawEdge(1, 5); drawEdge(2, 6); drawEdge(3, 7);

            ctx.setLineDash([]);

            // Draw Paths
            paths.forEach(segment => {
                ctx.beginPath();
                ctx.lineWidth = segment.type === 'rapid' ? 1 : 2;
                ctx.strokeStyle = segment.type === 'rapid' ? '#ef4444' : '#3b82f6';
                if (segment.type === 'rapid') ctx.setLineDash([2, 4]);

                segment.points.forEach((pt, i) => {
                    const proj = project(pt.x, pt.y, pt.z);
                    const x = proj.px * scale + offset.x;
                    const y = offset.y - proj.py * scale;
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                });
                ctx.stroke();
                ctx.setLineDash([]);
            });

            // Draw Cursor (Current position)
            const curProj = project(currentPos.x, currentPos.y, currentPos.z);
            const curX = curProj.px * scale + offset.x;
            const curY = offset.y - curProj.py * scale;

            ctx.fillStyle = '#10b981';
            ctx.beginPath();
            ctx.arc(curX, curY, 5, 0, Math.PI * 2);
            ctx.fill();

            // Draw Label
            ctx.fillStyle = '#ffffff';
            ctx.font = '10px monospace';
            ctx.fillText(`X:${currentPos.x.toFixed(1)} Y:${currentPos.y.toFixed(1)} Z:${currentPos.z.toFixed(1)}`, curX + 8, curY - 8);
        },

        cleanup() {
        }
    };
})();
