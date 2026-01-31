/**
 * Cut Planner - G-code generation for operator-friendly cutting jobs
 */

// Guard against re-declaration when Router reloads the script
if (typeof window.CutPlanner === 'undefined') {
    window.CutPlanner = {
        safeZ: 0,           // Captured at page load from current Z position
        currentJobType: null,
        generatedGcode: '',

        init() {
            console.log('[CutPlanner] Initialization V1.2 (Blade Thickness Fix)');
            this.captureSafeZ();
        },

        /**
         * Capture safe Z from current telemetry position
         */
        captureSafeZ() {
            if (typeof AppState !== 'undefined' && AppState.data && AppState.data.motion) {
                this.safeZ = AppState.data.motion.position?.z || 0;
            } else {
                fetch('/api/status')
                    .then(r => r.json())
                    .then(data => {
                        this.safeZ = (data.motion?.position?.z !== undefined) ?
                            data.motion.position.z : (data.z_pos || 0);
                    })
                    .catch(() => {
                        this.safeZ = 0;
                    });
            }
        },

        /**
         * Get form values for a job type
         */
        getParams(jobType) {
            switch (jobType) {
                case 'single':
                    return {
                        startY: parseFloat(document.getElementById('single-start-y').value) || 0,
                        endY: parseFloat(document.getElementById('single-end-y').value) || 0,
                        depth: parseFloat(document.getElementById('single-depth').value) || 0
                    };
                case 'linear-passes':
                    return {
                        startY: parseFloat(document.getElementById('passes-start-y').value) || 0,
                        endY: parseFloat(document.getElementById('passes-end-y').value) || 0,
                        totalDepth: parseFloat(document.getElementById('passes-total-depth').value) || 0,
                        forwardPass: parseFloat(document.getElementById('passes-forward').value) || 0,
                        backwardPass: parseFloat(document.getElementById('passes-backward').value) || 0
                    };
                case 'parallel':
                    const numPiecesP = parseInt(document.getElementById('parallel-num-pieces').value) || 1;
                    return {
                        startY: parseFloat(document.getElementById('parallel-start-y').value) || 0,
                        endY: parseFloat(document.getElementById('parallel-end-y').value) || 0,
                        depth: parseFloat(document.getElementById('parallel-depth').value) || 0,
                        numCuts: numPiecesP + 1,
                        numPieces: numPiecesP,
                        xStep: parseFloat(document.getElementById('parallel-x-step').value) || 0,
                        bladeThickness: parseFloat(document.getElementById('parallel-blade-thickness').value) || 0
                    };
                case 'parallel-passes':
                    const numPiecesPP = parseInt(document.getElementById('pp-num-pieces').value) || 1;
                    return {
                        startY: parseFloat(document.getElementById('pp-start-y').value) || 0,
                        endY: parseFloat(document.getElementById('pp-end-y').value) || 0,
                        totalDepth: parseFloat(document.getElementById('pp-total-depth').value) || 0,
                        forwardPass: parseFloat(document.getElementById('pp-forward').value) || 0,
                        backwardPass: parseFloat(document.getElementById('pp-backward').value) || 0,
                        numCuts: numPiecesPP + 1,
                        numPieces: numPiecesPP,
                        xStep: parseFloat(document.getElementById('pp-x-step').value) || 0,
                        bladeThickness: parseFloat(document.getElementById('pp-blade-thickness').value) || 0
                    };
                case 'corner-drilling':
                    return {
                        holeDiameter: parseFloat(document.getElementById('drill-hole-diameter').value) || 30,
                        cutoutX: parseFloat(document.getElementById('drill-cutout-x').value) || 450,
                        cutoutY: parseFloat(document.getElementById('drill-cutout-y').value) || 400,
                        depth: parseFloat(document.getElementById('drill-depth').value) || 30,
                        peckDepth: parseFloat(document.getElementById('drill-peck-depth').value) || 5,
                        dwellTime: parseFloat(document.getElementById('drill-dwell').value) || 0.5,
                        startupDelay: parseFloat(document.getElementById('drill-startup-delay').value) || 2.0,
                        speed: document.getElementById('drill-speed').value || 'medium',
                        startCorner: document.getElementById('drill-start-corner').value || 'top-left'
                    };
                default:
                    return {};
            }
        },

        /**
         * Generate G-code for Single Linear Cut
         */
        generateSingle(p) {
            const cutDistance = p.endY - p.startY;
            const lines = [
                '; Single Linear Cut',
                '; Generated by Cut Planner',
                '; Job Origin (0) is starting saw position',
                '',
                'G91              ; Relative positioning',
                `G0 Y${p.startY.toFixed(1)} F3       ; Move to Start Position`,
                `G1 Z${(-p.depth).toFixed(1)} F2   ; Plunge to target depth`,
                `G1 Y${cutDistance.toFixed(1)} F2    ; Perform the cut`,
                `G0 Z${p.depth.toFixed(1)} F3    ; Retract`,
                `G0 Y${(-p.endY).toFixed(1)} F3    ; Return to Job Origin`,
                '',
                '; Job complete'
            ];
            return lines.join('\n');
        },

        /**
         * Generate G-code for Linear Cut with Passes
         */
        generateLinearPasses(p) {
            const cutDistance = p.endY - p.startY;
            const lines = [
                '; Linear Cut with Passes',
                '; Generated by Cut Planner',
                '; Target Pieces: 1 (Linear Pass)',
                '',
                'G91              ; Relative positioning',
                `G0 Y${p.startY.toFixed(1)} F3       ; Move to Start Position`
            ];

            let totalZ = 0;
            let atStart = true; // true = at startY, false = at endY

            while (totalZ < p.totalDepth) {
                // Forward pass step
                let forwardStep = Math.min(p.forwardPass, p.totalDepth - totalZ);
                totalZ += forwardStep;

                lines.push('');
                lines.push(`; Pass at cumulative depth -${totalZ.toFixed(1)}`);
                lines.push(`G1 Z${(-forwardStep).toFixed(1)} F2   ; Plunge`);

                if (atStart) {
                    lines.push(`G1 Y${cutDistance.toFixed(1)} F2        ; Cut forward to End Y`);
                    atStart = false;
                } else {
                    lines.push(`G1 Y${(-cutDistance).toFixed(1)} F2       ; Cut backward to Start Y`);
                    atStart = true;
                }

                if (totalZ >= p.totalDepth) break;

                // Backward pass step (if enabled)
                if (p.backwardPass > 0) {
                    let backwardStep = Math.min(p.backwardPass, p.totalDepth - totalZ);
                    totalZ += backwardStep;

                    lines.push(`G1 Z${(-backwardStep).toFixed(1)} F2  ; Deepen`);
                    if (atStart) {
                        lines.push(`G1 Y${cutDistance.toFixed(1)} F2        ; Cut forward to End Y`);
                        atStart = false;
                    } else {
                        lines.push(`G1 Y${(-cutDistance).toFixed(1)} F2       ; Cut backward to Start Y`);
                        atStart = true;
                    }
                } else {
                    // One-way cutting: retract and return to startY
                    lines.push(`G0 Z${totalZ.toFixed(1)} F3         ; Retract to surface`);
                    if (!atStart) {
                        lines.push(`G0 Y${(-cutDistance).toFixed(1)} F3       ; Return to Start Y`);
                        atStart = true;
                    }
                    lines.push(`G0 Z${(-totalZ).toFixed(1)} F3        ; Return to depth`);
                }
            }

            // Final retract and return to Job Origin (0,0)
            lines.push('');
            lines.push(`G0 Z${totalZ.toFixed(1)} F3             ; Retract to surface`);
            if (!atStart) {
                // Currently at endY
                lines.push(`G0 Y${(-p.endY).toFixed(1)} F3           ; Return to Job Origin (0)`);
            } else {
                // Currently at startY
                lines.push(`G0 Y${(-p.startY).toFixed(1)} F3         ; Return to Job Origin (0)`);
            }
            lines.push('');
            lines.push('; Job complete');

            return lines.join('\n');
        },

        /**
         * Generate G-code for Parallel Cuts
         */
        generateParallel(p) {
            const cutDistance = p.endY - p.startY;
            const totalXMove = p.xStep + p.bladeThickness;
            const lines = [
                '; Parallel Cuts',
                '; Generated by Cut Planner',
                `; Target Pieces: ${p.numPieces}`,
                '',
                'G91              ; Relative positioning'
            ];

            for (let i = 0; i < p.numCuts; i++) {
                lines.push('');
                lines.push(`; --- Cut ${i + 1} of ${p.numCuts} ---`);
                lines.push(`G0 Y${p.startY.toFixed(1)} F3       ; Move to Start Y`);
                lines.push(`G1 Z${(-p.depth).toFixed(1)} F2   ; Plunge`);
                lines.push(`G1 Y${cutDistance.toFixed(1)} F2    ; Cut to End Y`);
                lines.push(`G0 Z${p.depth.toFixed(1)} F3    ; Retract`);
                lines.push(`G0 Y${(-p.endY).toFixed(1)} F3    ; Return to Origin Y`);

                if (i < p.numCuts - 1) {
                    lines.push(`G0 X${totalXMove.toFixed(1)} F3       ; Move to next piece (+ blade thickness)`);
                }
            }

            lines.push('');
            lines.push('; Job complete');
            return lines.join('\n');
        },

        /**
         * Generate G-code for Parallel Cuts with Passes
         */
        generateParallelPasses(p) {
            const cutDistance = p.endY - p.startY;
            const totalXMove = p.xStep + p.bladeThickness;
            const lines = [
                '; Parallel Cuts with Passes',
                '; Generated by Cut Planner',
                `; Target Pieces: ${p.numPieces}`,
                '',
                'G91              ; Relative positioning'
            ];

            for (let cut = 0; cut < p.numCuts; cut++) {
                lines.push('');
                lines.push(`; === Parallel Cut ${cut + 1} of ${p.numCuts} ===`);
                lines.push(`G0 Y${p.startY.toFixed(1)} F3       ; Move to Start Y`);

                let totalZ = 0;
                let atStart = true;

                while (totalZ < p.totalDepth) {
                    let forwardStep = Math.min(p.forwardPass, p.totalDepth - totalZ);
                    totalZ += forwardStep;
                    lines.push(`G1 Z${(-forwardStep).toFixed(1)} F2   ; Plunge`);

                    if (atStart) {
                        lines.push(`G1 Y${cutDistance.toFixed(1)} F2        ; Cut forward`);
                        atStart = false;
                    } else {
                        lines.push(`G1 Y${(-cutDistance).toFixed(1)} F2       ; Cut backward`);
                        atStart = true;
                    }

                    if (totalZ >= p.totalDepth) break;

                    if (p.backwardPass > 0) {
                        let backwardStep = Math.min(p.backwardPass, p.totalDepth - totalZ);
                        totalZ += backwardStep;
                        lines.push(`G1 Z${(-backwardStep).toFixed(1)} F2  ; Deepen`);
                        if (atStart) {
                            lines.push(`G1 Y${cutDistance.toFixed(1)} F2        ; Cut forward`);
                            atStart = false;
                        } else {
                            lines.push(`G1 Y${(-cutDistance).toFixed(1)} F2       ; Cut backward`);
                            atStart = true;
                        }
                    } else {
                        lines.push(`G0 Z${totalZ.toFixed(1)} F3         ; Retract`);
                        if (!atStart) {
                            lines.push(`G0 Y${(-cutDistance).toFixed(1)} F3       ; Return to Start Y`);
                            atStart = true;
                        }
                        lines.push(`G0 Z${(-totalZ).toFixed(1)} F3        ; Depth`);
                    }
                }

                lines.push(`G0 Z${totalZ.toFixed(1)} F3             ; Retract`);
                if (!atStart) {
                    lines.push(`G0 Y${(-p.endY).toFixed(1)} F3           ; Return to Origin Y`);
                } else {
                    lines.push(`G0 Y${(-p.startY).toFixed(1)} F3         ; Return to Origin Y`);
                }

                if (cut < p.numCuts - 1) {
                    lines.push(`G0 X${totalXMove.toFixed(1)} F3               ; Next piece (+ blade thickness)`);
                }
            }

            lines.push('');
            lines.push('; Job complete');
            return lines.join('\n');
        },

        /**
         * Generate G-code for Corner Drilling (Sink Cutout)
         */
        generateCornerDrilling(p) {
            const safeZ = 20; // Retract height above surface

            // Map speed to feedrate
            const speedMap = { slow: 1, medium: 2, fast: 3 };
            const feedRate = speedMap[p.speed] || 2;

            // Dimensions between hole centers
            const dimX = p.cutoutX - p.holeDiameter;
            const dimY = p.cutoutY - p.holeDiameter;

            // Define absolute positions of the 4 holes in a local coordinate system
            // where the Starting Corner is always (0,0).
            // Sequence is Clockwise: TL -> TR -> BR -> BL
            // But we just need the relative path based on where we start.

            /*
               Grid Reference (relative to bottom-left 0,0 of Cutout):
               TL: (0, dimY)   TR: (dimX, dimY)
               BL: (0, 0)      BR: (dimX, 0)
            */

            let holes = [];

            // Define the 4 holes relative to the connection sequence
            // For each start corner, we define the 4 coordinates relative to IT being (0,0)
            switch (p.startCorner) {
                case 'top-left': // Moves: Right -> Down -> Left
                    holes = [
                        { x: 0, y: 0, name: 'TOP LEFT (Start)' },
                        { x: dimX, y: 0, name: 'TOP RIGHT' },
                        { x: dimX, y: -dimY, name: 'BOTTOM RIGHT' },
                        { x: 0, y: -dimY, name: 'BOTTOM LEFT' }
                    ];
                    break;
                case 'top-right': // Moves: Down -> Left -> Up
                    holes = [
                        { x: 0, y: 0, name: 'TOP RIGHT (Start)' },
                        { x: 0, y: -dimY, name: 'BOTTOM RIGHT' },
                        { x: -dimX, y: -dimY, name: 'BOTTOM LEFT' },
                        { x: -dimX, y: 0, name: 'TOP LEFT' }
                    ];
                    break;
                case 'bottom-right': // Moves: Left -> Up -> Right
                    holes = [
                        { x: 0, y: 0, name: 'BOTTOM RIGHT (Start)' },
                        { x: -dimX, y: 0, name: 'BOTTOM LEFT' },
                        { x: -dimX, y: dimY, name: 'TOP LEFT' },
                        { x: 0, y: dimY, name: 'TOP RIGHT' }
                    ];
                    break;
                case 'bottom-left': // Moves: Up -> Right -> Down
                    holes = [
                        { x: 0, y: 0, name: 'BOTTOM LEFT (Start)' },
                        { x: 0, y: dimY, name: 'TOP LEFT' },
                        { x: dimX, y: dimY, name: 'TOP RIGHT' },
                        { x: dimX, y: 0, name: 'BOTTOM RIGHT' }
                    ];
                    break;
            }

            const lines = [
                '; Corner Drilling (Sink Cutout)',
                '; Generated by Cut Planner',
                `; Cutout Size: ${p.cutoutX} x ${p.cutoutY} mm`,
                `; Hole Diameter: ${p.holeDiameter} mm`,
                `; Travel Distance X: ${dimX.toFixed(1)} mm`,
                `; Travel Distance Y: ${dimY.toFixed(1)} mm`,
                `; Total Depth: ${p.depth} mm (Peck: ${p.peckDepth} mm)`,
                '',
                'G91              ; Relative positioning',
                'M8               ; Coolant ON',
                'M3               ; Drill ON',
                `G4 P${p.startupDelay.toFixed(1)}          ; Wait for VFD ramp-up`,
                ''
            ];

            let currentX = 0;
            let currentY = 0;

            for (let i = 0; i < holes.length; i++) {
                const h = holes[i];
                lines.push(`; --- Hole ${i + 1}: ${h.name} ---`);

                // Calculate move from current position
                const dx = h.x - currentX;
                const dy = h.y - currentY;

                if (i === 0) {
                    // Already at start position (0,0) by definition
                    // Just ensure we are in G91
                } else {
                    lines.push(`G0 X${dx.toFixed(1)} Y${dy.toFixed(1)} F3  ; Move to next hole`);
                }

                // Update current position tracker
                currentX = h.x;
                currentY = h.y;

                // Peck drilling cycle
                let drilledDepth = 0;
                while (drilledDepth < p.depth) {
                    const peck = Math.min(p.peckDepth, p.depth - drilledDepth);
                    drilledDepth += peck;

                    lines.push(`G1 Z${(-peck).toFixed(1)} F${feedRate}     ; Peck drill`);

                    if (p.dwellTime > 0 && drilledDepth < p.depth) {
                        lines.push(`G4 P${p.dwellTime.toFixed(1)}          ; Dwell`);
                    }
                }

                // Retract
                lines.push(`G0 Z${(p.depth + safeZ).toFixed(1)} F3    ; Retract`);
                lines.push('');
            }

            // Return to origin (Optional, but good practice to allow repeat)
            lines.push('; --- Return to start ---');
            lines.push(`G0 X${(-currentX).toFixed(1)} Y${(-currentY).toFixed(1)} F3  ; Return to ${p.startCorner}`);
            lines.push(`G0 Z${(-safeZ).toFixed(1)} F3             ; Lower to starting Z`);
            lines.push('');
            lines.push('M5               ; Drill OFF');
            lines.push('M9               ; Coolant OFF');
            lines.push('');
            lines.push('; Job complete');

            return lines.join('\n');
        },

        /**
         * Generate G-code for the specified job type
         */
        generate(jobType) {
            const params = this.getParams(jobType);
            switch (jobType) {
                case 'single': return this.generateSingle(params);
                case 'linear-passes': return this.generateLinearPasses(params);
                case 'parallel': return this.generateParallel(params);
                case 'parallel-passes': return this.generateParallelPasses(params);
                case 'corner-drilling': return this.generateCornerDrilling(params);
                default: return '; Unknown job type';
            }
        },

        /**
         * Show preview modal
         */
        preview(jobType) {
            this.currentJobType = jobType;
            this.generatedGcode = this.generate(jobType);
            document.getElementById('gcode-preview-content').textContent = this.generatedGcode;
            document.getElementById('gcode-preview-modal').classList.remove('hidden');
            document.getElementById('gcode-preview-run').onclick = () => {
                this.closePreview();
                this.executeGcode(this.generatedGcode);
            };
        },

        closePreview() {
            document.getElementById('gcode-preview-modal').classList.add('hidden');
        },

        run(jobType) {
            const gcode = this.generate(jobType);
            this.executeGcode(gcode);
        },

        async waitForBufferSpace() {
            return new Promise((resolve) => {
                const startTime = Date.now();
                const check = () => {
                    const motion = AppState.data?.motion;
                    const count = motion?.buffer_count || 0;
                    const capacity = motion?.buffer_capacity || 32;

                    // Allow filling up to Capacity - 4 slots to maintain high throughput
                    if (count < (capacity - 4)) {
                        resolve();
                    } else {
                        // If we've been waiting too long (>30s), something might be wrong
                        if (Date.now() - startTime > 30000) {
                            console.warn('[CutPlanner] Buffer space wait timeout');
                            return resolve();
                        }
                        setTimeout(check, 100);
                    }
                };
                check();
            });
        },

        async executeGcode(gcode) {
            try {
                const lines = gcode.split('\n')
                    .map(l => l.split(';')[0].trim())
                    .filter(l => l);

                AlertManager.add(window.i18n?.t('cut_planner.starting_job') || 'Starting...', 'info', 2000);
                await this.waitForIdle();

                for (const line of lines) {
                    if (line.startsWith('G0') || line.startsWith('G1')) {
                        await this.waitForBufferSpace();
                    }

                    let success = false;
                    let attempts = 0;
                    const maxAttempts = 3;

                    while (!success && attempts < maxAttempts) {
                        try {
                            const response = await fetch('/api/gcode', {
                                method: 'POST',
                                headers: { 'Content-Type': 'application/json' },
                                body: JSON.stringify({ command: line })
                            });

                            if (!response.ok) throw new Error(`HTTP ${response.status}`);

                            const result = await response.json();
                            if (result.success) {
                                success = true;
                            } else {
                                console.warn(`[CutPlanner] Rejected ${line}: ${result.error}. Waiting...`);
                                await new Promise(r => setTimeout(r, 1000));
                                attempts++;
                            }
                        } catch (e) {
                            console.error(`[CutPlanner] Network error: ${e.message}. Retrying...`);
                            await new Promise(r => setTimeout(r, 2000));
                            attempts++;
                        }
                    }

                    if (!success) throw new Error(`Failed to send command: ${line}`);
                }

                await this.waitForIdle();
                AlertManager.add(window.i18n?.t('cut_planner.job_completed') || 'Done!', 'success', 3000);
            } catch (error) {
                console.error('[CutPlanner] Job Error:', error);
                AlertManager.add(`Error: ${error.message}`, 'error', 5000);
            }
        },

        async waitForIdle() {
            return new Promise((resolve) => {
                const start = Date.now();
                const check = () => {
                    const isMoving = typeof AppState !== 'undefined' && AppState.data?.motion?.moving;
                    if (!isMoving || Date.now() - start > 30000) {
                        setTimeout(resolve, 100);
                    } else {
                        setTimeout(check, 100);
                    }
                };
                setTimeout(check, 50);
            });
        }
    };
}

// Initialize
if (document.readyState === 'complete') {
    CutPlanner.init();
} else {
    document.addEventListener('DOMContentLoaded', () => CutPlanner.init());
}
