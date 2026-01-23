/**
 * gcode.js - G-code page module
 * Handles editor, command execution, and visualizer integration
 */
(function () {
    class GCodePage {
        static executing = false;
        static paused = false;
        static currentLine = 0;
        static telemetryHandler = null;
        static _lastCardShowTime = 0;
        static _queueInterval = null;

        static async init() {
            console.log("[GCODE] Initializing v3 (Force Reload)");

            this.setupEditor();
            this.setupEventListeners();
            this.updateParserState();
            this.initQueueUI();

            // Load visualizer dependencies (Force Latest)
            await this.loadDependencies();

            // Debug check for card existence
            const vizCard = document.getElementById("visualizer-card");
            if (vizCard) {
                console.log("[GCODE] Visualizer card found in DOM");
                vizCard.querySelector('h2').textContent = "PATH PREVIEW (3D ISOMETRIC)";
            } else {
                console.error("[GCODE] Visualizer card MISSING from DOM");
            }
        }

        /**
         * Load visualizer scripts bypassing browser cache
         */
        static async loadDependencies() {
            try {
                // Force reload visualizer core
                await this.forceLoadScript("pages/gcode/gcode-visualizer.js");

                // Force reload integration logic
                await this.forceLoadScript("pages/gcode/gcode-viz-integration.js");

                console.log("[GCODE] Visualizer dependencies loaded successfully");
            } catch (error) {
                console.error("[GCODE] Failed to load visualizer dependencies:", error);
            }
        }

        /**
         * Fetches and injects script to bypass all levels of caching
         */
        static async forceLoadScript(src) {
            try {
                // Remove existing
                const existing = document.querySelector(`script[data-src="${src}"]`);
                if (existing) existing.remove();

                const response = await fetch(src, { cache: 'no-store', headers: { 'Cache-Control': 'no-cache' } });
                if (!response.ok) throw new Error(`HTTP ${response.status}`);
                const code = await response.text();

                const script = document.createElement('script');
                script.dataset.src = src;
                script.textContent = code;
                document.body.appendChild(script);

                // If it's the visualizer, we might need a small delay for the IIFE to run
                return new Promise(resolve => setTimeout(resolve, 50));
            } catch (e) {
                console.error(`[GCODE] Failed to force load ${src}:`, e);
                // Fallback to normal loading if fetch fails
                return new Promise((resolve, reject) => {
                    const s = document.createElement('script');
                    s.src = src + "?v=" + Date.now();
                    s.onload = resolve;
                    s.onerror = reject;
                    document.body.appendChild(s);
                });
            }
        }

        static setupEditor() {
            const editor = document.getElementById("gcode-input");
            const lineNumbers = document.getElementById("line-numbers");
            if (editor && lineNumbers) {
                this.inputHandler = () => this.updateLineNumbers();
                editor.addEventListener("input", this.inputHandler);
                this.scrollHandler = () => { lineNumbers.scrollTop = editor.scrollTop; };
                editor.addEventListener("scroll", this.scrollHandler);
                this.updateLineNumbers();
            }
        }

        static updateLineNumbers() {
            const editor = document.getElementById("gcode-input");
            const lineNumbers = document.getElementById("line-numbers");
            if (!editor || !lineNumbers) return;
            const lines = editor.value.split("\n").length;
            let nums = "";
            for (let i = 1; i <= lines; i++) nums += i + "\n";
            lineNumbers.textContent = nums;
        }

        static setupEventListeners() {
            document.getElementById("send-command")?.addEventListener("click", () => { this.executeCurrentCommand(); });
            document.getElementById("send-all")?.addEventListener("click", () => { this.executeAllCommands(); });
            document.getElementById("pause-execution")?.addEventListener("click", () => {
                this.paused = !this.paused;
                const btn = document.getElementById("pause-execution");
                if (btn) btn.querySelector("span").textContent = this.paused ? "▶️ Resume" : "⏸️ Pause";
            });
            document.getElementById("stop-execution")?.addEventListener("click", () => { this.stopExecution(); });
            document.getElementById("clear-editor")?.addEventListener("click", () => {
                const editor = document.getElementById("gcode-input");
                if (editor) { editor.value = ""; this.updateLineNumbers(); }
            });
            document.getElementById("load-examples")?.addEventListener("click", () => { this.loadExamples(); });

            document.querySelectorAll("[data-quick]").forEach(btn => {
                btn.addEventListener("click", () => {
                    const cmd = btn.getAttribute("data-quick");
                    this.executeCommand(cmd);
                });
            });

            this.telemetryHandler = (e) => { this.updateParserState(e.detail); };
            window.addEventListener("telemetry", this.telemetryHandler);
        }

        static executeCurrentCommand() {
            const editor = document.getElementById("gcode-input");
            if (!editor) return;
            const text = editor.value, pos = editor.selectionStart, lines = text.split("\n");
            let count = 0, cmd = "";
            for (let line of lines) {
                if (count + line.length >= pos) { cmd = line.trim(); break; }
                count += line.length + 1;
            }
            if (cmd && cmd.length > 0) this.executeCommand(cmd);
            else if (typeof AlertManager !== 'undefined') AlertManager.add("No command on current line", "warning", 2000);
        }

        static async executeAllCommands() {
            const editor = document.getElementById("gcode-input");
            if (!editor) return;
            const commands = editor.value.split("\n").map(l => l.trim()).filter(l => l.length > 0);
            if (commands.length === 0) {
                if (typeof AlertManager !== 'undefined') AlertManager.add("No commands to execute", "warning", 2000);
                return;
            }
            this.executing = true;
            this.currentLine = 0;
            this.updateExecutionButtons();
            for (let i = 0; i < commands.length && this.executing; i++) {
                while (this.paused && this.executing) await new Promise(r => setTimeout(r, 100));
                this.currentLine = i + 1;
                await this.executeCommand(commands[i], true);
                await new Promise(r => setTimeout(r, 50));
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
            const btn = document.getElementById("pause-execution");
            if (btn) btn.querySelector("span").textContent = "⏸️ Pause";
        }

        static updateExecutionButtons() {
            const btnCmd = document.getElementById("send-command"), btnAll = document.getElementById("send-all");
            if (btnCmd) btnCmd.disabled = this.executing;
            if (btnAll) btnAll.disabled = this.executing;
        }

        static async executeCommand(cmd, quiet = false) {
            if (!cmd || cmd.startsWith(";") || cmd.startsWith("(")) return;
            const isMotion = /^G(0|1|28)/i.test(cmd);
            if (isMotion) this.showExecutionCard(cmd);
            try {
                const res = await fetch("/api/gcode", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ command: cmd }),
                    credentials: "same-origin"
                });
                if (!res.ok) throw new Error(`HTTP ${res.status}`);
                const data = await res.json();
                if (data.success) {
                    if (!quiet && typeof AlertManager !== 'undefined') AlertManager.add("Command executed successfully", "success", 2000);
                    if (isMotion && data.eta_seconds !== undefined) this.updateExecutionCardETA(data.eta_seconds, data.distance_mm, data.speed_mm_min);
                    if (isMotion) {
                        const delay = (data.eta_seconds || 3) * 1000 + 2000;
                        setTimeout(() => this.hideExecutionCardIfStale(), delay);
                    }
                } else {
                    if (!quiet && typeof AlertManager !== 'undefined') AlertManager.add("Command failed", "critical", 3000);
                    if (isMotion) this.hideExecutionCard();
                }
            } catch (e) {
                console.error("[GCODE] Execute error:", e);
                if (!quiet && typeof AlertManager !== 'undefined') AlertManager.add("Communication error", "critical", 3000);
                if (isMotion) this.hideExecutionCard();
            }
        }

        static showExecutionCard(cmd) {
            const card = document.getElementById("execution-card"), txt = document.getElementById("execution-cmd"), bar = document.getElementById("execution-progress"), eta = document.getElementById("execution-eta"), pct = document.getElementById("execution-percent");
            if (card && txt && bar) {
                this._lastCardShowTime = Date.now();
                card.style.display = "block";
                txt.textContent = cmd;
                bar.style.width = "15%";
                bar.style.background = "linear-gradient(90deg, var(--primary-color), var(--color-warning))";
                if (pct) pct.textContent = "0%";
                if (eta) {
                    const est = this.estimateETA(cmd);
                    eta.textContent = est > 0 ? (est > 60 ? `ETA: ${(est / 60).toFixed(1)}m` : `ETA: ${est.toFixed(1)}s`) : "Calculating...";
                }
            }
        }

        static estimateETA(cmd) {
            const x = cmd.match(/X(-?\d+\.?\d*)/i), y = cmd.match(/Y(-?\d+\.?\d*)/i), z = cmd.match(/Z(-?\d+\.?\d*)/i), f = cmd.match(/F(-?\d+\.?\d*)/i);
            const valX = x ? parseFloat(x[1]) : 0, valY = y ? parseFloat(y[1]) : 0, valZ = z ? parseFloat(z[1]) : 0, feed = f ? parseFloat(f[1]) : 100;
            const dist = Math.sqrt(valX * valX + valY * valY + valZ * valZ);
            return feed > 0 && dist > 0 ? (dist / feed) * 60 : 0;
        }

        static hideExecutionCard() {
            const card = document.getElementById("execution-card");
            if (card) card.style.display = "none";
        }

        static updateExecutionCardETA(time, dist, speed) {
            const eta = document.getElementById("execution-eta");
            if (eta) {
                if (time > 60) eta.textContent = `ETA: ${(time / 60).toFixed(1)}m | ${dist?.toFixed(0) || "?"}mm @ ${speed?.toFixed(0) || "?"}mm/min`;
                else if (time > 0) eta.textContent = `ETA: ${time.toFixed(1)}s | ${dist?.toFixed(0) || "?"}mm @ ${speed?.toFixed(0) || "?"}mm/min`;
            }
        }

        static hideExecutionCardIfStale() {
            if (Date.now() - (this._lastCardShowTime || 0) > 2500) this.hideExecutionCard();
        }

        static updateParserState(data = null) {
            if (data) {
                const state = document.getElementById("motion-state");
                if (state) {
                    const txt = data.motion_active ? "Moving" : "Idle";
                    state.textContent = txt;
                    state.style.color = data.motion_active ? "var(--color-warning)" : "var(--color-optimal)";
                }
                if (data.parser) {
                    const mode = document.getElementById("distance-mode");
                    if (mode) mode.textContent = data.parser.absolute_mode ? "G90 (Absolute)" : "G91 (Relative)";
                    const feed = document.getElementById("feed-rate");
                    if (feed) {
                        const actual = data.parser.actual_feedrate, target = data.parser.feedrate || 0;
                        if (actual && Math.abs(actual - target) > 1) {
                            feed.textContent = `${Math.round(actual)} mm/min (calibrated)`;
                            feed.style.color = "var(--color-warning)";
                        } else {
                            feed.textContent = `${Math.round(target)} mm/min`;
                            feed.style.color = "";
                        }
                    }
                }

                // Update Progress if available
                const card = document.getElementById("execution-card"), cmd = document.getElementById("execution-cmd"), bar = document.getElementById("execution-progress"), eta = document.getElementById("execution-eta");
                if (card && cmd && bar && data.exec) {
                    const txt = data.exec.cmd || "", prog = data.exec.progress || 0, time = data.exec.eta || 0;
                    if (txt.length > 0 && prog < 100 && data.motion_active) {
                        this._lastCardShowTime = Date.now();
                        card.style.display = "block";
                        cmd.textContent = txt;
                        bar.style.width = `${Math.max(15, prog)}%`;
                        const pct = document.getElementById("execution-percent");
                        if (pct) pct.textContent = `${Math.round(prog)}%`;
                        if (eta) eta.textContent = time > 60 ? `ETA: ${(time / 60).toFixed(1)}m` : time > 0 ? `ETA: ${time.toFixed(1)}s` : "";
                    } else if (card.style.display !== "none") {
                        if (Date.now() - (this._lastCardShowTime || 0) < 3000) return;
                        if (prog >= 99.9) {
                            bar.style.width = "100%";
                            bar.style.backgroundColor = "var(--color-optimal)";
                            setTimeout(() => {
                                card.style.display = "none";
                                bar.style.backgroundColor = "var(--primary-color)";
                            }, 2000);
                        } else {
                            card.style.display = "none";
                        }
                    }
                }
            } else if (window.location.protocol !== "file:") {
                fetch("/api/gcode/state").then(res => res.json()).then(data => {
                    if (data.success) {
                        const mode = document.getElementById("distance-mode");
                        if (mode) mode.textContent = data.absolute_mode ? "G90 (Absolute)" : "G91 (Relative)";
                        const feed = document.getElementById("feed-rate");
                        if (feed) feed.textContent = (data.feedrate || 0) + " mm/min";
                    }
                }).catch(e => console.warn("[GCODE] Failed to fetch parser state:", e));
            }
        }

        static loadExamples() {
            const editor = document.getElementById("gcode-input");
            if (editor) {
                editor.value = "; Example G-code Program\n; Basic movement and positioning\n\n; Set to absolute positioning\nG90\n\n; Home all axes\nG28\n\n; Move to starting position\nG0 X0 Y0 Z10 F1000\n\n; Linear move\nG1 X50 Y50 Z5 F500\n\n; Set work coordinate zero\nG92 X0 Y0 Z0 A0\n\n; Display message on LCD\nM117 Program Complete\n\n; Get current position\nM114\n";
                this.updateLineNumbers();
            }
        }

        static cleanup() {
            console.log("[GCODE] Page cleanup");
            this.executing = false;
            if (this.telemetryHandler) window.removeEventListener("telemetry", this.telemetryHandler);
            const editor = document.getElementById("gcode-input");
            if (editor) {
                if (this.inputHandler) editor.removeEventListener("input", this.inputHandler);
                if (this.scrollHandler) editor.removeEventListener("scroll", this.scrollHandler);
            }
            if (this._queueInterval) clearInterval(this._queueInterval);
        }

        static async fetchQueueState() {
            try {
                const res = await fetch("/api/gcode/queue", { credentials: "same-origin" });
                if (!res.ok) {
                    console.warn(`[GCODE] Queue API returned ${res.status}`);
                    return;
                }
                const text = await res.text();
                // Check if text is completely empty or just whitespace
                if (!text || !text.trim()) {
                    console.warn("[GCODE] Queue API returned empty response");
                    return;
                }

                try {
                    const data = JSON.parse(text);
                    if (data.success) this.renderQueueState(data);
                } catch (jsonErr) {
                    console.error("[GCODE] Queue JSON parse error:", jsonErr, "Raw text:", text.substring(0, 100));
                }
            } catch (e) {
                console.error("[GCODE] Queue fetch error:", e);
            }
        }

        static renderQueueState(data) {
            const summary = document.getElementById("queue-summary"), list = document.getElementById("job-list"), actions = document.getElementById("recovery-actions");
            if (summary) {
                const q = data.queue;
                summary.textContent = `${q.total} jobs • ${q.completed} ✓ • ${q.failed} ✗ • ${q.pending} pending`;
            }
            if (actions) actions.style.display = data.queue.paused ? "block" : "none";
            if (list && data.jobs) {
                const icons = { 0: "⏳", 1: "🔄", 2: "✅", 3: "❌", 4: "⏭️" };
                const colors = { 0: "var(--text-muted)", 1: "var(--color-warning)", 2: "var(--color-optimal)", 3: "var(--color-critical)", 4: "var(--text-muted)" };
                list.innerHTML = data.jobs.map(job => `
                    <div class="job-item" style="display: flex; justify-content: space-between; padding: 6px 8px; border-bottom: 1px solid var(--border-color); font-family: monospace; font-size: 0.85em;">
                        <span style="color: ${colors[job.status]};">
                            ${icons[job.status]} #${job.id}: ${job.command.substring(0, 30)}${job.command.length > 30 ? "..." : ""}
                        </span>
                        ${job.error ? `<span style="color: var(--color-critical); font-size: 0.8em;">${job.error}</span>` : ""}
                    </div>
                `).join("");
            }
        }

        static async retryJob() {
            try {
                const res = await fetch("/api/gcode/queue/retry", { method: "POST", credentials: "same-origin" });
                const data = await res.json();
                if (data.success) {
                    if (typeof AlertManager !== 'undefined') AlertManager.add("Retrying job from start position...", "success", 2000);
                    this.fetchQueueState();
                } else if (typeof AlertManager !== 'undefined') AlertManager.add(data.error || "Retry failed", "critical", 3000);
            } catch (e) {
                if (typeof AlertManager !== 'undefined') AlertManager.add("Communication error", "critical", 3000);
            }
        }

        static async resumeJob() {
            try {
                const res = await fetch("/api/gcode/queue/resume", { method: "POST", credentials: "same-origin" });
                const data = await res.json();
                if (data.success) {
                    if (typeof AlertManager !== 'undefined') AlertManager.add("Resuming from current position...", "success", 2000);
                    this.fetchQueueState();
                } else if (typeof AlertManager !== 'undefined') AlertManager.add(data.error || "Resume failed", "critical", 3000);
            } catch (e) {
                if (typeof AlertManager !== 'undefined') AlertManager.add("Communication error", "critical", 3000);
            }
        }

        static async skipJob() {
            try {
                const res = await fetch("/api/gcode/queue/skip", { method: "POST", credentials: "same-origin" });
                const data = await res.json();
                if (data.success) {
                    if (typeof AlertManager !== 'undefined') AlertManager.add("Skipping to next job...", "success", 2000);
                    this.fetchQueueState();
                } else if (typeof AlertManager !== 'undefined') AlertManager.add(data.error || "Skip failed", "critical", 3000);
            } catch (e) {
                if (typeof AlertManager !== 'undefined') AlertManager.add("Communication error", "critical", 3000);
            }
        }

        static async clearQueue() {
            try {
                const res = await fetch("/api/gcode/queue", { method: "DELETE", credentials: "same-origin" });
                if (res.ok) {
                    if (typeof AlertManager !== 'undefined') AlertManager.add("Queue history cleared", "success", 2000);
                    this.fetchQueueState();
                }
            } catch (e) {
                if (typeof AlertManager !== 'undefined') AlertManager.add("Communication error", "critical", 3000);
            }
        }

        static initQueueUI() {
            document.getElementById("retry-job")?.addEventListener("click", () => this.retryJob());
            document.getElementById("resume-job")?.addEventListener("click", () => this.resumeJob());
            document.getElementById("skip-job")?.addEventListener("click", () => this.skipJob());
            document.getElementById("refresh-queue")?.addEventListener("click", () => this.fetchQueueState());
            document.getElementById("clear-queue")?.addEventListener("click", () => this.clearQueue());
            this.fetchQueueState();
            this._queueInterval = setInterval(() => this.fetchQueueState(), 5000);
        }
    }

    window.GCodePage = GCodePage;
    window.currentPageModule = GCodePage;
})();