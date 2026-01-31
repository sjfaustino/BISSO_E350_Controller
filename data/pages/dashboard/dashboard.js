window.DashboardModule = window.DashboardModule || {
    history: { cpu: [], memory: [], spindle: [], temperature: [], latency: [], motion: [], wifi: [], timestamps: [] }, maxHistory: 300, currentTimeRange: 3e5, chart: null, graphs: {}, lastTelemetry: null, stateChangeHandler: null, updateInterval: null, historicalDataLoaded: !1, setNA(t, e = "") { t && (t.classList.remove("value-na"), t.innerHTML = '<span class="value-na">' + window.i18n.t('dashboard.na') + '</span>' + e) }, updateHeaderNA(t, e, i) { const s = document.getElementById(t); s && (s.innerHTML = i ? e : `${e} <span class="value-na" style="font-size: 0.8em; margin-left: 8px;">` + window.i18n.t('dashboard.na') + '</span>') }, setValue(t, e) { t && (t.textContent = e, t.classList.remove("value-na")) }, init() { console.log(window.i18n.t('dashboard.initializing')), this.setupEventListeners(), this.initializeGraphs(), this.historicalDataLoaded = !1, this.stateChangeHandler = () => this.onStateChanged(), window.addEventListener("state-changed", this.stateChangeHandler), this.updateInterval = setInterval(() => { this.onStateChanged(), this.updateGraphs() }, 1e3), this.loadHistoryData() }, setupEventListeners() {
        document.querySelectorAll(".card-toggle").forEach(t => { t.addEventListener("click", () => { const e = t.closest(".card").querySelector(".card-content"); e.classList.toggle("collapsed"), t.textContent = e.classList.contains("collapsed") ? "+" : "−" }) }); const t = document.getElementById("graph-time-range"); t && t.addEventListener("change", t => { this.currentTimeRange = 1e3 * parseInt(t.target.value), console.log(window.i18n.t('dashboard.time_range_changed'), this.currentTimeRange / 1e3, window.i18n.t('dashboard.seconds')), Object.values(this.graphs).forEach(t => { t && t.config && (t.config.timeWindow = this.currentTimeRange, "function" == typeof t.draw && t.draw()) }) }); const e = document.getElementById("export-graphs-btn"); e && e.addEventListener("click", () => this.exportGraphsData()), document.querySelectorAll(".time-btn").forEach(t => { t.addEventListener("click", () => { document.querySelectorAll(".time-btn").forEach(t => t.classList.remove("active")), t.classList.add("active"), this.currentTimeRange = parseInt(t.dataset.range), this.drawChart() }) }); const i = document.getElementById("dro-coord-mode"), s = document.getElementById("dro-work-offset"); i && (this.workCoordMode = localStorage.getItem("droCoordMode") || "machine", this.workOffset = localStorage.getItem("droWorkOffset") || "G54", this.updateCoordModeUI(), i.addEventListener("click", () => { this.workCoordMode = "machine" === this.workCoordMode ? "work" : "machine", localStorage.setItem("droCoordMode", this.workCoordMode), this.updateCoordModeUI(), this.onStateChanged() })), s && (s.value = this.workOffset || "G54", s.addEventListener("change", t => { this.workOffset = t.target.value, localStorage.setItem("droWorkOffset", this.workOffset), this.onStateChanged() }))
    }, loadHistoryData() {
        if (window.location.protocol === "file:") return;
        window.API.get("history/telemetry", null, { silent: true })
            .then(t => {
                if (t.success) {
                    console.log(window.i18n.t('dashboard.loading_history'), t.cpu?.length, window.i18n.t('dashboard.history_samples'));
                    if (t.cpu) this.history.cpu = t.cpu;
                    if (t.heap) {
                        // Filter out 0s from historical heap data by replacing with last known
                        let lastValid = 320000 / 1024; // Default fallback
                        this.history.memory = t.heap.map(v => {
                            if (v > 0) {
                                lastValid = v / 1024;
                                return lastValid;
                            }
                            return lastValid;
                        });
                    }
                    if (t.spindle_amps) this.history.spindle = t.spindle_amps;
                    if (t.wifi) {
                        // Filter out 0s from historical wifi data
                        let lastValid = 50; // Default fallback
                        this.history.wifi = t.wifi.map(v => {
                            if (v > 0) {
                                lastValid = v;
                                return lastValid;
                            }
                            return lastValid;
                        });
                    }
                    if (t.cpu && t.cpu.length > 0) {
                        const now = Date.now();
                        this.history.timestamps = t.cpu.map((_, idx) => now - 5000 * (t.cpu.length - 1 - idx));
                    }
                    this.historicalDataLoaded = false;
                    this.updateGraphs();
                }
            })
            .catch(err => console.warn(window.i18n.t('dashboard.failed_load_history'), err));
    },
    zeroAll() {
        if ("machine" === this.workCoordMode) {
            return void AlertManager.add(window.i18n.t('dashboard.cannot_zero_machine'), "warning");
        }
        if (AppState.data.motion?.moving) {
            return void AlertManager.add(window.i18n.t('dashboard.cannot_zero_moving'), "warning");
        }
        const t = "G92 X0 Y0 Z0 A0";
        window.API.post("gcode", { command: t }).then(e => {
            e.success ? AlertManager.add(window.i18n.t('dashboard.zero_all_success'), "success", 2e3) : AlertManager.add(window.i18n.t('dashboard.zero_all_failed'), "error")
        }).catch(t => console.error("Zero All failed", t))
    },
    setupEventListeners() {
        document.querySelectorAll(".card-toggle").forEach(t => { t.addEventListener("click", () => { const e = t.closest(".card").querySelector(".card-content"); e.classList.toggle("collapsed"), t.textContent = e.classList.contains("collapsed") ? "+" : "−" }) });
        const t = document.getElementById("graph-time-range");
        t && t.addEventListener("change", t => { this.currentTimeRange = 1e3 * parseInt(t.target.value), console.log(window.i18n.t('dashboard.time_range_changed'), this.currentTimeRange / 1e3, window.i18n.t('dashboard.seconds')), Object.values(this.graphs).forEach(t => { t && t.config && (t.config.timeWindow = this.currentTimeRange, "function" == typeof t.draw && t.draw()) }) });
        const e = document.getElementById("export-graphs-btn");
        e && e.addEventListener("click", () => this.exportGraphsData());
        document.querySelectorAll(".time-btn").forEach(t => { t.addEventListener("click", () => { document.querySelectorAll(".time-btn").forEach(t => t.classList.remove("active")), t.classList.add("active"), this.currentTimeRange = parseInt(t.dataset.range), this.drawChart() }) });
        const i = document.getElementById("dro-coord-mode"),
            s = document.getElementById("dro-work-offset"),
            n = document.getElementById("dro-zero-all");
        i && (this.workCoordMode = localStorage.getItem("droCoordMode") || "machine", this.workOffset = localStorage.getItem("droWorkOffset") || "G54", this.updateCoordModeUI(), i.addEventListener("click", () => { this.workCoordMode = "machine" === this.workCoordMode ? "work" : "machine", localStorage.setItem("droCoordMode", this.workCoordMode), this.updateCoordModeUI(), this.onStateChanged() })), s && (s.value = this.workOffset || "G54", s.addEventListener("change", t => {
            const cmd = t.target.value;
            this.workOffset = cmd;
            localStorage.setItem("droWorkOffset", cmd);
            window.API.post("gcode", { command: cmd })
                .then(res => {
                    if (!res.success) AlertManager.add(window.i18n.t('dashboard.wcs_change_failed'), "error");
                });
            this.onStateChanged();
        })), n && n.addEventListener("click", () => this.zeroAll())
    },
    updateCoordModeUI() { const t = document.getElementById("dro-coord-mode"), e = document.getElementById("dro-mode-text"), i = document.getElementById("dro-work-offset"); e && (e.textContent = "machine" === this.workCoordMode ? window.i18n.t('dashboard.machine') : window.i18n.t('dashboard.work')), t && t.classList.toggle("work-mode", "work" === this.workCoordMode), i && (i.disabled = "machine" === this.workCoordMode) }, initializeGraphs() { if (document.getElementById("cpu-graph")) try { this.graphs.cpu = new GraphVisualizer("cpu-graph", { title: window.i18n.t('dashboard.cpu_usage'), yMin: 0, yMax: 100, unit: "%", timeWindow: this.currentTimeRange }), this.graphs.cpu.addSeries("CPU", "#10b981") } catch (t) { console.warn(window.i18n.t('dashboard.cpu_graph_init_failed'), t) } if (document.getElementById("memory-graph")) try { this.graphs.memory = new GraphVisualizer("memory-graph", { title: window.i18n.t('dashboard.free_memory'), yMin: 0, yMax: 350, unit: " KB", timeWindow: this.currentTimeRange }), this.graphs.memory.addSeries("Memory", "#3b82f6") } catch (t) { console.warn(window.i18n.t('dashboard.memory_graph_init_failed'), t) } if (document.getElementById("spindle-graph")) try { this.graphs.spindle = new GraphVisualizer("spindle-graph", { title: window.i18n.t('dashboard.spindle_current'), yMin: 0, yMax: 20, unit: " A", timeWindow: this.currentTimeRange }), this.graphs.spindle.addSeries("Current", "#f59e0b") } catch (t) { console.warn(window.i18n.t('dashboard.spindle_graph_init_failed'), t) } if (document.getElementById("temperature-graph")) try { this.graphs.temperature = new GraphVisualizer("temperature-graph", { title: window.i18n.t('dashboard.cpu_temperature'), yMin: 20, yMax: 80, unit: " °C", timeWindow: this.currentTimeRange }), this.graphs.temperature.addSeries("Temp", "#ef4444") } catch (t) { console.warn(window.i18n.t('dashboard.temperature_graph_init_failed'), t) } if (document.getElementById("latency-graph")) try { this.graphs.latency = new GraphVisualizer("latency-graph", { title: window.i18n.t('dashboard.websocket_latency'), yMin: 0, yMax: 100, unit: " ms", timeWindow: this.currentTimeRange }), this.graphs.latency.addSeries("Latency", "#8b5cf6") } catch (t) { console.warn(window.i18n.t('dashboard.latency_graph_init_failed'), t) } if (document.getElementById("motion-load-graph")) try { this.graphs.motion = new GraphVisualizer("motion-load-graph", { title: window.i18n.t('dashboard.motion_system_load'), yMin: 0, yMax: 100, unit: "%", timeWindow: this.currentTimeRange }), this.graphs.motion.addSeries("Quality", "#10b981"), this.graphs.motion.addSeries("Jitter", "#f59e0b") } catch (t) { console.warn(window.i18n.t('dashboard.motion_graph_init_failed'), t) } if (document.getElementById("wifi-graph")) try { this.graphs.wifi = new GraphVisualizer("wifi-graph", { title: window.i18n.t('dashboard.wifi_signal_quality'), yMin: 0, yMax: 100, unit: "%", timeWindow: this.currentTimeRange }), this.graphs.wifi.addSeries("Signal", "#3b82f6") } catch (t) { console.warn(window.i18n.t('dashboard.wifi_graph_init_failed'), t) } console.log(window.i18n.t('dashboard.graphs_initialized')), this.restoreHistoricalData() }, restoreHistoricalData() { const t = Date.now(); if (this.history.cpu.length > 0) { console.log(window.i18n.t('dashboard.restoring_data', { count: this.history.cpu.length }), this.history.cpu.length, window.i18n.t('dashboard.historical_data_points')); for (let e = 0; e < this.history.cpu.length; e++) { const i = t - 1e3 * (this.history.cpu.length - 1 - e); this.graphs.cpu && void 0 !== this.history.cpu[e] && this.graphs.cpu.addDataPoint("CPU", this.history.cpu[e], i), this.graphs.memory && void 0 !== this.history.memory[e] && this.graphs.memory.addDataPoint("Memory", this.history.memory[e], i), this.graphs.spindle && void 0 !== this.history.spindle[e] && this.graphs.spindle.addDataPoint("Current", this.history.spindle[e], i), this.graphs.temperature && void 0 !== this.history.temperature[e] && this.graphs.temperature.addDataPoint("Temp", this.history.temperature[e], i), this.graphs.latency && void 0 !== this.history.latency[e] && this.graphs.latency.addDataPoint("Latency", this.history.latency[e], i), this.graphs.wifi && void 0 !== this.history.wifi[e] && this.graphs.wifi.addDataPoint("Signal", this.history.wifi[e], i) } } }, onStateChanged() {
        const t = AppState.data; this.lastTelemetry = t, this.updateSystemStatus(t), this.updateMotionStatus(t), this.updateVFDStatus(t), this.updateNetworkStatus(t), this.updateLCDMirror(t), t.axis && (this.updateAxisCard("x", t.axis.x), this.updateAxisCard("y", t.axis.y), this.updateAxisCard("z", t.axis.z), this.updateDRO(t));
        if (!t.axis) {
            ["x", "y", "z"].forEach(axis => this.updateAxisCard(axis, null));
            this.updateDRO({});
        }
        this.updateHistoryData(t), this.drawChart()
    }, updateLCDMirror(t) { if (!t.lcd || !t.lcd.lines) return; for (let i = 0; i < 4; i++) { const line = document.getElementById(`lcd-line-${i}`); if (line) { const content = t.lcd.lines[i] || ""; line.textContent = content.padEnd(20, " "); } } this.updateLCDStatus(t); }, updateLCDStatus(t) { const dot = document.getElementById("lcd-status-dot"); if (!dot) return; dot.className = "lcd-status-dot"; if (t.system && t.system.health === "CRITICAL") { dot.classList.add("degraded"); dot.title = "LCD Degraded Mode"; } else if (t.network && t.network.wifi_connected) { dot.classList.add("online"); dot.title = "LCD Online"; } else { dot.classList.add("offline"); dot.title = "LCD Offline"; } }, updateSystemStatus(t) { if (!t.system) return; const e = t.system.cpu_percent || 0, i = t.system.free_heap_bytes || 0, s = t.system.health || "UNKNOWN", n = t.system.status || "IDLE", a = document.getElementById("health-value"); a && (a.textContent = s, a.className = "card-value " + s.toLowerCase()); const o = document.getElementById("health-detail"); o && (o.textContent = window.i18n.t('dashboard.status') + ": " + n); const r = document.getElementById("health-bar"); r && (r.className = "progress-fill " + s.toLowerCase()); const d = document.getElementById("cpu-value"); d && (d.textContent = e.toFixed(1) + "%"); const h = document.getElementById("cpu-bar"); h && (h.style.width = e + "%", h.className = "progress-fill", e > 85 && h.classList.add("warning"), e > 95 && h.classList.add("critical")); const l = document.getElementById("mem-value"); l && (l.textContent = (i / 1024).toFixed(0) + " KB"); const c = document.getElementById("mem-bar"); if (c) { const t = 32e4, e = Math.min(100, i / t * 100); c.style.width = e + "%" } }, updateMotionStatus(t) { console.log("DASHBOARD: LOAD CARD STABILIZED (SYNTAX FIX)"); const e = !t.system || t.system.plc_hardware_present !== false; if (this.updateHeaderNA("header-motion", window.i18n.t('dashboard.motion_status'), e), this.updateHeaderNA("header-motion-load", window.i18n.t('dashboard.motion_load_header') || window.i18n.t('dashboard.motion_system_load'), true), t.motion) { const i = document.getElementById("motion-status"); i && (e ? (i.textContent = window.i18n.t(t.motion.moving ? 'dashboard.moving' : 'dashboard.stopped'), i.style.color = "") : this.setNA(i)) } if (t.safety) { const i = document.getElementById("safety-status"); if (i) if (e) { let e = window.i18n.t('dashboard.ok'); t.safety.estop ? e = window.i18n.t('dashboard.estop') : t.safety.alarm && (e = window.i18n.t('dashboard.alarm')), i.textContent = e, i.style.color = "" } else this.setNA(i) } }, updateVFDStatus(t) { if (!t.vfd) return; this.updateHeaderNA("header-vfd", window.i18n.t('dashboard.axis_drive'), t.vfd.connected), this.updateHeaderNA("header-spindle-trend", window.i18n.t('dashboard.spindle_trend_header') || window.i18n.t('dashboard.spindle_current_trend'), t.vfd.connected); const e = document.getElementById("vfd-status"), i = document.getElementById("spindle-rpm"), s = document.getElementById("spindle-speed"), n = document.getElementById("spindle-current"); if (t.vfd.connected) { const a = t.vfd.rpm > 0 ? window.i18n.t('dashboard.running') : window.i18n.t('dashboard.idle'); e && (e.textContent = a), i && (i.textContent = (t.vfd.rpm || 0).toFixed(0)), s && (s.textContent = (t.vfd.speed_m_s || 0).toFixed(1) + " m/s"), n && (n.textContent = (t.vfd.current_amps || 0).toFixed(2) + " A"); const o = document.getElementById("spindle-bar"); if (o) { const e = Math.min(100, (t.vfd.current_amps || 0) / 30 * 100); o.style.width = e + "%" } } else { e && (e.textContent = window.i18n.t('dashboard.disconnected')), this.setNA(i), this.setNA(s, " m/s"), this.setNA(n, " A"); const t = document.getElementById("spindle-bar"); t && (t.style.width = "0%") } const a = document.getElementById("vfd-freq"); t.vfd.connected ? a && (a.textContent = (t.vfd.frequency_hz || 0).toFixed(1) + " Hz") : this.setNA(a, " Hz") }, updateNetworkStatus(t) { if (!t.network) return; const e = document.getElementById("wifi-signal"); e && (e.textContent = t.network.signal_percent + "%"); const i = document.getElementById("wifi-bar"); i && (i.style.width = t.network.signal_percent + "%"); const s = document.getElementById("wifi-status"); s && (s.textContent = t.network.wifi_connected ? window.i18n.t('dashboard.connected_chk') : window.i18n.t('dashboard.disconnected_chk')) }, updateHistoryData(t) {
        this.history.cpu.push((t.system && t.system.cpu_percent) || 0);

        const freeHeap = (t.system && t.system.free_heap_bytes) || 0;
        if (freeHeap > 0) {
            this.history.memory.push(freeHeap / 1024);
        } else {
            // Use last known value to prevent 0 KB drop on graph
            const lastVal = this.history.memory.length > 0 ? this.history.memory[this.history.memory.length - 1] : 0;
            this.history.memory.push(lastVal);
        }

        this.history.spindle.push(t.vfd?.current_amps || 0);
        this.history.temperature.push(t.system?.temperature || 0);
        this.history.latency.push(SharedWebSocket.latency || 0);

        const wifiSignal = t.network?.signal_percent || 0;
        if (wifiSignal > 0) {
            this.history.wifi.push(wifiSignal);
        } else {
            // Use last known value to prevent 0% drop on graph
            const lastVal = this.history.wifi.length > 0 ? this.history.wifi[this.history.wifi.length - 1] : 0;
            this.history.wifi.push(lastVal);
        }

        let avgQuality = 80;
        if (t.axis) {
            const vals = [];
            if (t.axis.x?.quality) vals.push(t.axis.x.quality);
            if (t.axis.y?.quality) vals.push(t.axis.y.quality);
            if (t.axis.z?.quality) vals.push(t.axis.z.quality);
            if (vals.length > 0) avgQuality = vals.reduce((a, b) => a + b, 0) / vals.length;
        }

        let avgJitter = 0.5;
        if (t.axis) {
            const vals = [];
            if (t.axis.x?.jitter_mms) vals.push(t.axis.x.jitter_mms);
            if (t.axis.y?.jitter_mms) vals.push(t.axis.y.jitter_mms);
            if (t.axis.z?.jitter_mms) vals.push(t.axis.z.jitter_mms);
            if (vals.length > 0) avgJitter = vals.reduce((a, b) => a + b, 0) / vals.length;
        }

        this.history.motion.push({ quality: avgQuality, jitter: avgJitter });
        this.history.timestamps.push(Date.now());

        if (this.history.cpu.length > this.maxHistory) {
            this.history.cpu.shift();
            this.history.memory.shift();
            this.history.spindle.shift();
            this.history.temperature.shift();
            this.history.latency.shift();
            this.history.motion.shift();
            this.history.wifi.shift();
            this.history.timestamps.shift();
        }
    },
    updateAxisCard(t, e) {
        const i = AppState.data.motion?.dro_connected ?? !1,
            s = `axis-${t}`,
            n = t.toUpperCase() + " " + window.i18n.t('dashboard.axis_quality');
        this.updateHeaderNA(`header-axis-${t}`, n, i);
        const a = document.getElementById(`${s}-quality`);
        if (a) {
            i && e ? this.setValue(a, (e.quality || 0)) : this.setNA(a);
        }
        const o = document.getElementById(`${s}-bar`);
        if (o) {
            o.style.width = i && e ? (e.quality || 0) + "%" : "0%";
            i && e ? o.classList.remove("offline") : o.classList.add("offline");
        }
        const r = document.getElementById(`${s}-jitter`);
        if (r) {
            i && e ? this.setValue(r, (e.jitter_mms || 0).toFixed(3) + " mm/s") : this.setNA(r);
        }
        const d = document.getElementById(`${s}-error`);
        if (d) {
            i && e ? this.setValue(d, (e.vfd_error_percent || 0).toFixed(1) + "%") : this.setNA(d);
        }
        const h = document.getElementById(`${s}-stalled`);
        if (h) {
            if (i && e) {
                if (e.stalled) {
                    h.textContent = window.i18n.t('dashboard.stalled');
                    h.style.color = "var(--color-critical)";
                } else {
                    h.textContent = window.i18n.t('dashboard.ok_check');
                    h.style.color = "var(--color-optimal)";
                }
            } else {
                h.textContent = window.i18n.t('dashboard.offline');
                h.style.color = "var(--color-critical)";
            }
        }

        // Maintenance Status Icon
        const maintIcon = document.getElementById(`${s}-maint`);
        if (maintIcon) {
            if (i && e && e.maint) {
                maintIcon.classList.remove("hidden");
            } else {
                maintIcon.classList.add("hidden");
            }
        }
    },
    updateDRO(t) {
        const e = AppState.data.motion?.dro_connected ?? !1;
        this.updateHeaderNA("header-dro", window.i18n.t('dashboard.position_dro'), e);

        // Synchronize WCS Dropdown
        const s_wco = document.getElementById("dro-work-offset");
        if (s_wco && t.active_wcs !== undefined) {
            const wcs_name = "G" + (54 + t.active_wcs);
            if (s_wco.value !== wcs_name && !s_wco.matches(":focus")) {
                s_wco.value = wcs_name;
                this.workOffset = wcs_name;
                localStorage.setItem("droWorkOffset", wcs_name);
            }
        }

        const wco = t.wco || [0, 0, 0, 0];
        const axis_map = ["x", "y", "z", "a"];
        axis_map.forEach((ax, idx) => {
            const s = document.getElementById(`dro-${ax}`);
            if (s) {
                if (e) {
                    let val = t[`${ax}_mm`] ?? 0;
                    if (this.workCoordMode === "work") { // Only apply WCO offset in work coordinate mode
                        val -= wco[idx];
                    }
                    s.textContent = val.toFixed(3);
                    s.classList.toggle("negative", val < 0);
                } else {
                    this.setNA(s);
                    s.classList.remove("negative");
                }
            }
        });
        const i = document.getElementById("dro-status");
        i && (e ? (i.textContent = window.i18n.t('dashboard.live'), i.classList.remove("offline")) : (i.textContent = window.i18n.t('dashboard.offline'), i.classList.add("offline")))
    },
    drawChart() { const t = document.getElementById("trendsChart"); if (!t) return; const e = t.getContext("2d"), i = t.getBoundingClientRect(); t.width = i.width, t.height = i.height; const s = 35, n = t.width - s - 10, a = t.height - 10 - 20; e.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--bg-primary").trim() || "#fff", e.fillRect(0, 0, t.width, t.height), e.strokeStyle = "#e5e7eb", e.lineWidth = 1, e.fillStyle = "#6b7280", e.font = "10px sans-serif", e.textAlign = "right", e.textBaseline = "middle"; for (let i = 0; i <= 4; i++) { const n = i / 4, o = 10 + a - n * a; e.beginPath(), e.moveTo(s, o), e.lineTo(t.width - 10, o), e.stroke(); const r = 100 * n; e.fillText(r.toFixed(0), 30, o) } e.textAlign = "center", e.textBaseline = "top"; const o = this.currentTimeRange / 1e3 >= 60 ? this.currentTimeRange / 6e4 + "m" : this.currentTimeRange / 1e3 + "s"; e.fillText("-" + o, s, t.height - 20 + 5), e.fillText(window.i18n.t('dashboard.now'), t.width - 10, t.height - 20 + 5); const r = Math.min(this.history.cpu.length, 60 * this.currentTimeRange), d = this.history.cpu.slice(-r), h = this.history.memory.slice(-r), l = this.history.spindle.slice(-r), c = (t, i, o, r = "#888") => { if (t && !(t.length < 2)) { e.strokeStyle = i || r, e.lineWidth = 2, e.beginPath(); for (let i = 0; i < t.length; i++) { const r = Math.max(0, Math.min(100, t[i] / o * 100)), d = s + i / Math.max(1, t.length - 1) * n, h = 10 + a - r / 100 * a; 0 === i ? e.moveTo(d, h) : e.lineTo(d, h) } e.stroke() } }, m = getComputedStyle(document.documentElement).getPropertyValue("--chart-cpu").trim() || "#10b981", p = getComputedStyle(document.documentElement).getPropertyValue("--chart-mem").trim() || "#3b82f6", u = getComputedStyle(document.documentElement).getPropertyValue("--chart-spindle").trim() || "#f59e0b"; c(d, m, 100), c(h, p, 320), c(l, u, 30), e.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue("--text-primary").trim(), e.lineWidth = 1, e.beginPath(), e.moveTo(s, 10), e.lineTo(s, t.height - 20), e.lineTo(t.width - 10, t.height - 20), e.stroke() }, updateGraphs() {
        const t = this.graphs.cpu && "MiniChart" === this.graphs.cpu.constructor.name; if (!this.historicalDataLoaded && this.history.cpu.length > 1) {
            console.log(window.i18n.t('dashboard.bulk_loading_charts', { count: this.history.cpu.length }), this.history.cpu.length, window.i18n.t('dashboard.points_to_charts')); for (let e = 0; e < this.history.cpu.length; e++)void 0 !== this.history.cpu[e] && (t ? this.graphs.cpu?.addDataPoint(this.history.cpu[e]) : this.graphs.cpu?.addDataPoint("CPU", this.history.cpu[e])), void 0 !== this.history.memory[e] && (t ? this.graphs.memory?.addDataPoint(this.history.memory[e]) : this.graphs.memory?.addDataPoint("Memory", this.history.memory[e])), void 0 !== this.history.spindle[e] && (t ? this.graphs.spindle?.addDataPoint(this.history.spindle[e]) : this.graphs.spindle?.addDataPoint("Spindle", this.history.spindle[e])), void 0 !== this.history.temperature[e] && (t ? this.graphs.temperature?.addDataPoint(this.history.temperature[e]) : this.graphs.temperature?.addDataPoint("Temp", this.history.temperature[e]));
            return this.historicalDataLoaded = !0, void console.log(window.i18n.t('dashboard.bulk_load_complete'))
        } if (this.history.cpu.length > 0) { const e = this.history.cpu[this.history.cpu.length - 1]; t ? this.graphs.cpu?.addDataPoint(e) : this.graphs.cpu?.addDataPoint("CPU", e); { let t = this.graphs.cpu?.getStats("CPU"); if (!t && this.history.cpu.length > 0) { const i = this.history.cpu; t = { avg: i.reduce((t, e) => t + e, 0) / i.length, max: Math.max(...i), current: e } } if (t) { const i = document.getElementById("cpu-value"), s = document.getElementById("cpu-avg"), n = document.getElementById("cpu-max"); i && (i.textContent = e.toFixed(1) + "%"), s && (s.textContent = t.avg.toFixed(1)), n && (n.textContent = t.max.toFixed(1)); const a = document.getElementById("cpu-current"), o = document.getElementById("cpu-stat-avg"), r = document.getElementById("cpu-stat-max"); a && (a.textContent = e.toFixed(1) + "%"), o && (o.textContent = t.avg.toFixed(1) + "%"), r && (r.textContent = t.max.toFixed(1) + "%") } } } if (this.history.memory.length > 0) {
            const e = this.history.memory[this.history.memory.length - 1]; if (t ? this.graphs.memory?.addDataPoint(e) : this.graphs.memory?.addDataPoint("Memory", e), !t) {
                const t = this.graphs.memory?.getStats("Memory"); if (t) {
                    const i = document.getElementById("mem-value"),
                        s = document.getElementById("mem-min"),
                        avg = document.getElementById("mem-avg"),
                        max = document.getElementById("mem-max");
                    i && (i.textContent = e.toFixed(0) + " KB");
                    s && (s.textContent = t.min.toFixed(0));
                    avg && (avg.textContent = t.avg.toFixed(0));
                    max && (max.textContent = t.max.toFixed(0));

                    const n = document.getElementById("mem-current"), a = document.getElementById("mem-stat-avg"), o = document.getElementById("mem-stat-max");
                    n && (n.textContent = e.toFixed(0) + " KB"), a && (a.textContent = t.avg.toFixed(0) + " KB"), o && (o.textContent = t.max.toFixed(0) + " KB")
                }
            }
        } const e = this.lastTelemetry?.vfd?.connected; if (this.history.spindle.length > 0 && e) { const e = this.history.spindle[this.history.spindle.length - 1]; if (t ? this.graphs.spindle?.addDataPoint(e) : this.graphs.spindle?.addDataPoint("Current", e), !t) { const t = this.graphs.spindle?.getStats("Current"); if (t) { const i = document.getElementById("spindle-current"); i && (i.textContent = e.toFixed(2) + " A"); const s = document.getElementById("spindle-bar"), n = document.getElementById("spindle-freq"), a = document.getElementById("spindle-thermal"); if (s) { const t = 30, i = Math.min(e / t * 100, 100); s.style.width = i + "%", s.style.background = i > 80 ? "var(--color-critical)" : i > 60 ? "var(--color-warning)" : "var(--color-optimal)" } this.lastTelemetry && this.lastTelemetry.vfd && (n && (n.textContent = this.lastTelemetry.vfd.frequency_hz.toFixed(1) + " Hz"), a && (a.textContent = (this.lastTelemetry.vfd.thermal_percent || 0) + "%")); const o = document.getElementById("spindle-stat-current"), r = document.getElementById("spindle-stat-avg"), d = document.getElementById("spindle-stat-max"); o && (o.textContent = e.toFixed(2) + " A"), r && (r.textContent = t.avg.toFixed(2) + " A"), d && (d.textContent = t.max.toFixed(2) + " A") } } } else e || (this.setNA(document.getElementById("spindle-stat-current"), " A"), this.setNA(document.getElementById("spindle-stat-avg"), " A"), this.setNA(document.getElementById("spindle-stat-max"), " A")); if (this.history.temperature.length > 0) { const e = this.history.temperature[this.history.temperature.length - 1]; if (t ? this.graphs.temperature?.addDataPoint(e) : this.graphs.temperature?.addDataPoint("Temp", e), !t) { const t = this.graphs.temperature?.getStats("Temp"); if (t) { const i = document.getElementById("temp-current"), s = document.getElementById("temp-stat-avg"), n = document.getElementById("temp-stat-max"); i && (i.textContent = e.toFixed(1) + " °C"), s && (s.textContent = t.avg.toFixed(1) + " °C"), n && (n.textContent = t.max.toFixed(1) + " °C") } } } if (this.history.latency.length > 0 && !t) { const t = this.history.latency[this.history.latency.length - 1]; this.graphs.latency?.addDataPoint("Latency", t); const e = this.graphs.latency?.getStats("Latency"); if (e) { const i = document.getElementById("latency-current"), s = document.getElementById("latency-stat-avg"), n = document.getElementById("latency-stat-max"); i && (i.textContent = t.toFixed(0) + " ms"), s && (s.textContent = e.avg.toFixed(0) + " ms"), n && (n.textContent = e.max.toFixed(0) + " ms") } } if (this.history.motion.length > 0 && !t) { const t = this.history.motion[this.history.motion.length - 1], e = AppState.data.motion?.dro_connected ?? !1; this.updateHeaderNA("header-motion-load", window.i18n.t('dashboard.motion_system_load'), e); const i = e && t.quality || 0, s = e && t.jitter || 0; this.graphs.motion?.addDataPoint("Quality", i), this.graphs.motion?.addDataPoint("Jitter", s); const n = document.getElementById("motion-quality"), a = document.getElementById("motion-jitter"); e ? (this.setValue(n, i.toFixed(0) + "%"), this.setValue(a, s.toFixed(2) + " mm/s")) : (this.setNA(n), this.setNA(a)) } if (this.history.wifi.length > 0 && !t) { const t = this.history.wifi[this.history.wifi.length - 1]; this.graphs.wifi?.addDataPoint("Signal", t); const e = this.graphs.wifi?.getStats("Signal"); if (e) { const i = document.getElementById("wifi-current"), s = document.getElementById("wifi-stat-avg"), n = document.getElementById("wifi-stat-max"); i && (i.textContent = t.toFixed(0) + "%"), s && (s.textContent = e.avg.toFixed(0) + "%"), n && (n.textContent = e.max.toFixed(0) + "%") } }
    }, exportGraphsData() { let t = window.i18n.t('dashboard.graph_export_header') + (new Date).toLocaleString() + "\n\n"; Object.entries(this.graphs).forEach(([e, i]) => { i && "function" == typeof i.exportData && (t += `\n=== ${e.toUpperCase()} ===\n`, t += i.exportData()) }); const e = new Blob([t], { type: "text/csv" }), i = window.URL.createObjectURL(e), s = document.createElement("a"); s.href = i, s.download = `graphs-export-${Date.now()}.csv`, s.click(), window.URL.revokeObjectURL(i), AlertManager.add(window.i18n.t('dashboard.graph_exported_alert'), "success", 2e3) }, cleanup() { console.log(window.i18n.t('dashboard.cleaning_up')), this.stateChangeHandler && (window.removeEventListener("state-changed", this.stateChangeHandler), this.stateChangeHandler = null), this.updateInterval && (clearInterval(this.updateInterval), this.updateInterval = null), Object.values(this.graphs).forEach(t => { t && "function" == typeof t.destroy && t.destroy() }), this.graphs = {} }
};
// Stack Monitor Extension (Idempotent)
if (!DashboardModule._isPatched) {
    DashboardModule.initStackGraph = function () {
        if (document.getElementById("stackChart")) {
            try {
                this.graphs.stack = new GraphVisualizer("stackChart", {
                    title: "Stack Free",
                    yMin: 0,
                    unit: " B",
                    timeWindow: this.currentTimeRange
                });
                console.log("Stack Graph Initialized");
            } catch (e) {
                console.warn("Stack graph init failed", e);
            }
        }
    };

    DashboardModule.updateStackMonitor = function (t) {
        if (this.graphs.stack && t && t.stack) {
            Object.entries(t.stack).forEach(([taskName, bytes]) => {
                if (bytes !== undefined) this.graphs.stack.addDataPoint(taskName, bytes);
            });
        }
    };

    // Hook initialization
    const _oInit = DashboardModule.initializeGraphs;
    DashboardModule.initializeGraphs = function () {
        _oInit.call(this);
        this.initStackGraph();
    };

    // Hook update
    const _oUpdate = DashboardModule.onStateChanged;
    DashboardModule.onStateChanged = function () {
        _oUpdate.call(this);
        if (this.lastTelemetry) this.updateStackMonitor(this.lastTelemetry);
    };

    DashboardModule._isPatched = true;
}

window.currentPageModule = DashboardModule;