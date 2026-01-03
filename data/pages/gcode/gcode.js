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
                } else {
                    this.addLogEntry(`✗ ${command} (failed)`, 'error');
                    if (!batch) AlertManager.add('Command failed', 'critical', 3000);
                }
            } catch (error) {
                console.error('[GCODE] Execute error:', error);
                this.addLogEntry(`✗ ${command} (error: ${error.message})`, 'error');
                if (!batch) AlertManager.add('Communication error', 'critical', 3000);
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
    }

    // Export for router
    window.GCodePage = GCodePage;
    window.currentPageModule = GCodePage;

})();



