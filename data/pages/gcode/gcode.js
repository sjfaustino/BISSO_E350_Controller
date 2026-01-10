/**
 * @file gcode.js
 * @brief G-code Interface Page Controller
 */

(function () {
    class GCodePage {
        static executing = false;
        static paused = false;
        static currentLine = 0;
        static telemetryHandler = null;

        static init() {
            console.log('[GCODE] Initializing');
            this.setupEditor();
            this.setupEventListeners();
            this.updateParserState();
            this.initQueueUI(); // Initialize job queue UI
        }

        static setupEditor() {
            const editor = document.getElementById('gcode-input');
            const lineNumbers = document.getElementById('line-numbers');

            if (!editor || !lineNumbers) return;

            // Update line numbers on input
            this.inputHandler = () => this.updateLineNumbers();
            editor.addEventListener('input', this.inputHandler);

            this.scrollHandler = () => {
                lineNumbers.scrollTop = editor.scrollTop;
            };
            editor.addEventListener('scroll', this.scrollHandler);

            this.updateLineNumbers();
        }

        static updateLineNumbers() {
            const editor = document.getElementById('gcode-input');
            const lineNumbers = document.getElementById('line-numbers');

            if (!editor || !lineNumbers) return;

            const lines = editor.value.split('\n').length;
            let numbers = '';
            for (let i = 1; i <= lines; i++) {
                numbers += i + '\n';
            }
            lineNumbers.textContent = numbers;
        }

        static setupEventListeners() {
            // Send single command (current line or selection)
            document.getElementById('send-command')?.addEventListener('click', () => {
                this.executeCurrentCommand();
            });

            // Send all lines
            document.getElementById('send-all')?.addEventListener('click', () => {
                this.executeAllCommands();
            });

            // Pause execution
            document.getElementById('pause-execution')?.addEventListener('click', () => {
                this.paused = !this.paused;
                const btn = document.getElementById('pause-execution');
                if (btn) btn.querySelector('span').textContent = this.paused ? '▶️ Resume' : '⏸️ Pause';
            });

            // Stop execution
            document.getElementById('stop-execution')?.addEventListener('click', () => {
                this.stopExecution();
            });

            // Clear editor
            document.getElementById('clear-editor')?.addEventListener('click', () => {
                const input = document.getElementById('gcode-input');
                if (input) {
                    input.value = '';
                    this.updateLineNumbers();
                }
            });

            // Clear log
            document.getElementById('clear-log')?.addEventListener('click', () => {
                const log = document.getElementById('execution-log');
                if (log) log.innerHTML = '';
            });

            // Load examples
            document.getElementById('load-examples')?.addEventListener('click', () => {
                this.loadExamples();
            });

            // Quick commands
            document.querySelectorAll('[data-quick]').forEach(btn => {
                btn.addEventListener('click', () => {
                    const command = btn.getAttribute('data-quick');
                    this.executeCommand(command);
                });
            });

            // Listen for telemetry updates
            this.telemetryHandler = (e) => {
                this.updateParserState(e.detail);
            };
            window.addEventListener('telemetry', this.telemetryHandler);
        }

        static executeCurrentCommand() {
            const editor = document.getElementById('gcode-input');
            if (!editor) return;

            const text = editor.value;
            const cursorPos = editor.selectionStart;

            // Find current line
            const lines = text.split('\n');
            let charCount = 0;
            let currentLine = '';

            for (let line of lines) {
                if (charCount + line.length >= cursorPos) {
                    currentLine = line.trim();
                    break;
                }
                charCount += line.length + 1; // +1 for newline
            }

            if (currentLine && currentLine.length > 0) {
                this.executeCommand(currentLine);
            } else {
                AlertManager.add('No command on current line', 'warning', 2000);
            }
        }

        static async executeAllCommands() {
            const editor = document.getElementById('gcode-input');
            if (!editor) return;

            const lines = editor.value.split('\n').map(l => l.trim()).filter(l => l.length > 0);

            if (lines.length === 0) {
                AlertManager.add('No commands to execute', 'warning', 2000);
                return;
            }

            this.executing = true;
            this.currentLine = 0;
            this.updateExecutionButtons();

            for (let i = 0; i < lines.length; i++) {
                if (!this.executing) break;

                // Wait if paused
                while (this.paused && this.executing) {
                    await new Promise(resolve => setTimeout(resolve, 100));
                }

                this.currentLine = i + 1;
                await this.executeCommand(lines[i], true);

                // Small delay between commands
                await new Promise(resolve => setTimeout(resolve, 50));
            }

            this.executing = false;
            this.currentLine = 0;
            this.updateExecutionButtons();
        }

        static stopExecution() {
            this.executing = false;
            this.paused = false;
            this.currentLine = 0;
            this.updateExecutionButtons();
            const btn = document.getElementById('pause-execution');
            if (btn) btn.querySelector('span').textContent = '⏸️ Pause';
            this.addLogEntry('Execution stopped', 'warning');
        }

        static updateExecutionButtons() {
            const sendBtn = document.getElementById('send-command');
            const sendAllBtn = document.getElementById('send-all');

            if (sendBtn) sendBtn.disabled = this.executing;
            if (sendAllBtn) sendAllBtn.disabled = this.executing;
        }

        static async executeCommand(command, batch = false) {
            if (!command || command.startsWith(';') || command.startsWith('(')) {
                // Skip empty lines and comments
                return;
            }

            // Show execution card immediately for motion commands (G0, G1)
            const isMotionCommand = /^G[01]\s/i.test(command);
            if (isMotionCommand) {
                this.showExecutionCard(command);
            }

            try {
                // PHASE 5.10: Removed hardcoded credentials - browser handles auth via 401 prompt
                const response = await fetch('/api/gcode', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify({ command }),
                    credentials: 'same-origin' // Include auth credentials from browser
                });

                const data = await response.json();

                if (data.success) {
                    this.addLogEntry(`✓ ${command}`, 'success');
                    if (!batch) AlertManager.add('Command executed successfully', 'success', 2000);

                    // Update ETA with server-calculated value (uses calibration data)
                    if (isMotionCommand && data.eta_seconds !== undefined) {
                        this.updateExecutionCardETA(data.eta_seconds, data.distance_mm, data.speed_mm_min);
                    }

                    // Keep card visible for motion commands - telemetry will update it
                    // If no telemetry update within grace period, hide the card
                    if (isMotionCommand) {
                        const gracePeriod = (data.eta_seconds || 3) * 1000 + 2000; // ETA + 2s buffer
                        setTimeout(() => this.hideExecutionCardIfStale(), gracePeriod);
                    }
                } else {
                    this.addLogEntry(`✗ ${command} (failed)`, 'error');
                    if (!batch) AlertManager.add('Command failed', 'critical', 3000);
                    // Hide card immediately on failure
                    if (isMotionCommand) {
                        this.hideExecutionCard();
                    }
                }
            } catch (error) {
                console.error('[GCODE] Execute error:', error);
                this.addLogEntry(`✗ ${command} (error: ${error.message})`, 'error');
                if (!batch) AlertManager.add('Communication error', 'critical', 3000);
                if (isMotionCommand) {
                    this.hideExecutionCard();
                }
            }
        }

        static showExecutionCard(command) {
            const card = document.getElementById('execution-card');
            const cmdEl = document.getElementById('execution-cmd');
            const progEl = document.getElementById('execution-progress');
            const etaEl = document.getElementById('execution-eta');
            const percentEl = document.getElementById('execution-percent');

            if (card && cmdEl && progEl) {
                this._lastCardShowTime = Date.now();
                card.style.display = 'block';
                cmdEl.textContent = command;
                progEl.style.width = '15%'; // Visible initial progress
                progEl.style.background = 'linear-gradient(90deg, var(--primary-color), var(--color-warning))';
                if (percentEl) percentEl.textContent = '0%';

                // Calculate ETA from command parameters
                if (etaEl) {
                    const eta = this.estimateETA(command);
                    if (eta > 0) {
                        etaEl.textContent = eta > 60 ? `ETA: ${(eta / 60).toFixed(1)}m` : `ETA: ${eta.toFixed(1)}s`;
                    } else {
                        etaEl.textContent = 'Calculating...';
                    }
                }
            }
        }

        static estimateETA(command) {
            // Parse G-code to estimate time based on distance and feedrate
            // Format: G1 X<val> Y<val> Z<val> F<feedrate>
            const xMatch = command.match(/X(-?\d+\.?\d*)/i);
            const yMatch = command.match(/Y(-?\d+\.?\d*)/i);
            const zMatch = command.match(/Z(-?\d+\.?\d*)/i);
            const fMatch = command.match(/F(-?\d+\.?\d*)/i);

            const x = xMatch ? parseFloat(xMatch[1]) : 0;
            const y = yMatch ? parseFloat(yMatch[1]) : 0;
            const z = zMatch ? parseFloat(zMatch[1]) : 0;
            const feedrate = fMatch ? parseFloat(fMatch[1]) : 100; // Default 100 mm/min

            // Calculate approximate distance (assume starting from 0 for simplicity)
            const distance = Math.sqrt(x * x + y * y + z * z);

            // Feedrate is mm/min, convert to seconds
            if (feedrate > 0 && distance > 0) {
                return (distance / feedrate) * 60; // Return seconds
            }
            return 0;
        }

        static hideExecutionCard() {
            const card = document.getElementById('execution-card');
            if (card) card.style.display = 'none';
        }

        static updateExecutionCardETA(etaSeconds, distanceMm, speedMmMin) {
            const etaEl = document.getElementById('execution-eta');
            const percentEl = document.getElementById('execution-percent');

            if (etaEl) {
                if (etaSeconds > 60) {
                    etaEl.textContent = `ETA: ${(etaSeconds / 60).toFixed(1)}m | ${distanceMm?.toFixed(0) || '?'}mm @ ${speedMmMin?.toFixed(0) || '?'}mm/min`;
                } else if (etaSeconds > 0) {
                    etaEl.textContent = `ETA: ${etaSeconds.toFixed(1)}s | ${distanceMm?.toFixed(0) || '?'}mm @ ${speedMmMin?.toFixed(0) || '?'}mm/min`;
                }
            }
        }

        static hideExecutionCardIfStale() {
            // Only hide if no recent telemetry update refreshed the timestamp
            if (Date.now() - (this._lastCardShowTime || 0) > 2500) {
                this.hideExecutionCard();
            }
        }

        static addLogEntry(message, type = 'info') {
            const log = document.getElementById('execution-log');
            if (!log) return;

            const entry = document.createElement('div');
            entry.className = `log-entry ${type}`;

            const timestamp = new Date().toLocaleTimeString();
            entry.innerHTML = `
            <span class="log-timestamp">[${timestamp}]</span>
            <span>${message}</span>
        `;

            log.appendChild(entry);
            log.scrollTop = log.scrollHeight;

            // Limit log entries to 100
            while (log.children.length > 100) {
                log.removeChild(log.firstChild);
            }
        }

        static updateParserState(telemetry = null) {
            // Update from telemetry if available
            if (telemetry) {
                // Update motion state
                const motionState = document.getElementById('motion-state');
                if (motionState) {
                    const state = telemetry.motion_active ? 'Moving' : 'Idle';
                    motionState.textContent = state;
                    motionState.style.color = telemetry.motion_active ? 'var(--color-warning)' : 'var(--color-optimal)';
                }

                // Update Execution Card
                const card = document.getElementById('execution-card');
                const cmdEl = document.getElementById('execution-cmd');
                const progEl = document.getElementById('execution-progress');
                const etaEl = document.getElementById('execution-eta');

                if (card && cmdEl && progEl && telemetry.exec) {
                    const cmd = telemetry.exec.cmd || '';
                    const progress = telemetry.exec.progress || 0;
                    const eta = telemetry.exec.eta || 0;

                    if (cmd.length > 0 && progress < 100 && telemetry.motion_active) {
                        this._lastCardShowTime = Date.now(); // Refresh timestamp
                        card.style.display = 'block';
                        cmdEl.textContent = cmd;
                        progEl.style.width = `${Math.max(15, progress)}%`;

                        // Update percentage text
                        const percentEl = document.getElementById('execution-percent');
                        if (percentEl) percentEl.textContent = `${Math.round(progress)}%`;

                        // Format ETA
                        if (eta > 60) {
                            etaEl.textContent = `ETA: ${(eta / 60).toFixed(1)}m`;
                        } else if (eta > 0) {
                            etaEl.textContent = `ETA: ${eta.toFixed(1)}s`;
                        } else {
                            etaEl.textContent = '';
                        }
                    } else if (card.style.display !== 'none') {
                        // Grace period: Don't hide if card was just shown (3 second window)
                        const timeSinceShow = Date.now() - (this._lastCardShowTime || 0);
                        if (timeSinceShow < 3000) {
                            // Still within grace period, keep showing
                            return;
                        }

                        // Keep showing for a moment at 100% then hide
                        if (progress >= 99.9) {
                            progEl.style.width = '100%';
                            progEl.style.backgroundColor = 'var(--color-optimal)'; // Turn green
                            setTimeout(() => {
                                card.style.display = 'none';
                                progEl.style.backgroundColor = 'var(--primary-color)'; // Reset
                            }, 2000);
                        } else {
                            card.style.display = 'none';
                        }
                    }
                }
            }

            // Fetch parser state from backend API
            if (window.location.protocol !== 'file:') {
                fetch('/api/gcode/state')
                    .then(r => r.json())
                    .then(data => {
                        if (data.success) {
                            // Update modal coordinate mode display if element exists
                            const coordMode = document.getElementById('coord-mode');
                            if (coordMode) coordMode.textContent = data.absolute_mode ? 'Absolute (G90)' : 'Relative (G91)';

                            const feedrate = document.getElementById('current-feedrate');
                            if (feedrate) feedrate.textContent = (data.feedrate || 0) + ' mm/min';
                        }
                    })
                    .catch(err => {
                        console.warn('[GCODE] Failed to fetch parser state:', err);
                    });
            }
        }

        static loadExamples() {
            const examples = `; Example G-code Program
; Basic movement and positioning

; Set to absolute positioning
G90

; Home all axes
G28

; Move to starting position
G0 X0 Y0 Z10 F1000

; Linear move
G1 X50 Y50 Z5 F500

; Set work coordinate zero
G92 X0 Y0 Z0 A0

; Display message on LCD
M117 Program Complete

; Get current position
M114
`;

            const input = document.getElementById('gcode-input');
            if (input) {
                input.value = examples;
                this.updateLineNumbers();
            }
        }

        static cleanup() {
            console.log('[GCODE] Page cleanup');
            this.executing = false;
            if (this.telemetryHandler) {
                window.removeEventListener('telemetry', this.telemetryHandler);
            }

            const editor = document.getElementById('gcode-input');
            if (editor) {
                if (this.inputHandler) editor.removeEventListener('input', this.inputHandler);
                if (this.scrollHandler) editor.removeEventListener('scroll', this.scrollHandler);
            }
        }

        // ==================== QUEUE MANAGEMENT ====================

        static async fetchQueueState() {
            try {
                const response = await fetch('/api/gcode/queue', { credentials: 'same-origin' });
                const data = await response.json();
                if (data.success) {
                    this.renderQueueState(data);
                }
            } catch (error) {
                console.error('[GCODE] Queue fetch error:', error);
            }
        }

        static renderQueueState(data) {
            const summary = document.getElementById('queue-summary');
            const jobList = document.getElementById('job-list');
            const recoveryActions = document.getElementById('recovery-actions');

            if (summary) {
                const q = data.queue;
                summary.textContent = `${q.total} jobs • ${q.completed} ✓ • ${q.failed} ✗ • ${q.pending} pending`;
            }

            // Show/hide recovery actions
            if (recoveryActions) {
                recoveryActions.style.display = data.queue.paused ? 'block' : 'none';
            }

            // Render job list
            if (jobList && data.jobs) {
                const statusIcons = { 0: '⏳', 1: '🔄', 2: '✅', 3: '❌', 4: '⏭️' };
                const statusColors = { 0: 'var(--text-muted)', 1: 'var(--color-warning)', 2: 'var(--color-optimal)', 3: 'var(--color-critical)', 4: 'var(--text-muted)' };

                jobList.innerHTML = data.jobs.map(job => `
                    <div class="job-item" style="display: flex; justify-content: space-between; padding: 6px 8px; border-bottom: 1px solid var(--border-color); font-family: monospace; font-size: 0.85em;">
                        <span style="color: ${statusColors[job.status]};">
                            ${statusIcons[job.status]} #${job.id}: ${job.command.substring(0, 30)}${job.command.length > 30 ? '...' : ''}
                        </span>
                        ${job.error ? `<span style="color: var(--color-critical); font-size: 0.8em;">${job.error}</span>` : ''}
                    </div>
                `).join('');
            }
        }

        static async retryJob() {
            try {
                const response = await fetch('/api/gcode/queue/retry', { method: 'POST', credentials: 'same-origin' });
                const data = await response.json();
                if (data.success) {
                    AlertManager.add('Retrying job from start position...', 'success', 2000);
                    this.fetchQueueState();
                } else {
                    AlertManager.add(data.error || 'Retry failed', 'critical', 3000);
                }
            } catch (error) {
                AlertManager.add('Communication error', 'critical', 3000);
            }
        }

        static async resumeJob() {
            try {
                const response = await fetch('/api/gcode/queue/resume', { method: 'POST', credentials: 'same-origin' });
                const data = await response.json();
                if (data.success) {
                    AlertManager.add('Resuming from current position...', 'success', 2000);
                    this.fetchQueueState();
                } else {
                    AlertManager.add(data.error || 'Resume failed', 'critical', 3000);
                }
            } catch (error) {
                AlertManager.add('Communication error', 'critical', 3000);
            }
        }

        static async skipJob() {
            try {
                const response = await fetch('/api/gcode/queue/skip', { method: 'POST', credentials: 'same-origin' });
                const data = await response.json();
                if (data.success) {
                    AlertManager.add('Skipping to next job...', 'success', 2000);
                    this.fetchQueueState();
                } else {
                    AlertManager.add(data.error || 'Skip failed', 'critical', 3000);
                }
            } catch (error) {
                AlertManager.add('Communication error', 'critical', 3000);
            }
        }

        static async clearQueue() {
            try {
                const response = await fetch('/api/gcode/queue', { method: 'DELETE', credentials: 'same-origin' });
                if (response.ok) {
                    AlertManager.add('Queue history cleared', 'success', 2000);
                    this.fetchQueueState();
                }
            } catch (error) {
                AlertManager.add('Communication error', 'critical', 3000);
            }
        }

        static initQueueUI() {
            // Wire up recovery buttons
            document.getElementById('retry-job')?.addEventListener('click', () => this.retryJob());
            document.getElementById('resume-job')?.addEventListener('click', () => this.resumeJob());
            document.getElementById('skip-job')?.addEventListener('click', () => this.skipJob());
            document.getElementById('refresh-queue')?.addEventListener('click', () => this.fetchQueueState());
            document.getElementById('clear-queue')?.addEventListener('click', () => this.clearQueue());

            // Initial fetch
            this.fetchQueueState();

            // Poll every 5 seconds for updates
            this._queueInterval = setInterval(() => this.fetchQueueState(), 5000);
        }
    }

    // Export for router
    window.GCodePage = GCodePage;
    window.currentPageModule = GCodePage;

})();



