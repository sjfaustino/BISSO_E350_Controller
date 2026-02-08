window.DiagnosticsModule = window.DiagnosticsModule || {
    updateInterval: null, trendInterval: null, spindleInterval: null, spindleData: [], spindlePaused: !1, spindleMaxPoints: 120, tachometerInterval: null, rs485Interval: null, setNA(t, e = "") { t && (t.innerHTML = '<span class="value-na">n/a</span>' + e) }, updateHeaderNA(t, e, n) { const a = document.getElementById(t); a && (a.innerHTML = n ? e : `${e} <span class="value-na" style="font-size: 0.8em; margin-left: 8px;">(N/A)</span>`) }, init() { console.log("[Diagnostics] Initializing"), window.addEventListener("state-changed", () => this.onStateChanged()), this.setupEventListeners(), this.loadInitialData(), this.updateInterval = setInterval(() => this.loadIOStatus(), 2e3), this.loadTrendData(), this.trendInterval = setInterval(() => this.loadTrendData(), 5e3), this.loadSpindleData(), this.spindleInterval = setInterval(() => this.loadSpindleData(), 500), this.loadIODiagnostics(), this.ioDiagInterval = setInterval(() => this.loadIODiagnostics(), 2e3), this.startTachometerPolling(), this.loadBootLog(), this.updateRS485Status(), this.rs485Interval = setInterval(() => this.updateRS485Status(), 3e3) }, setupEventListeners() {
        const bind = (id, fn) => document.getElementById(id)?.addEventListener("click", fn);
        bind("refresh-io-btn", () => this.loadIOStatus());
        bind("clear-faults-btn", () => this.clearFaults());
        bind("refresh-trends-btn", () => this.loadTrendData());
        bind("refresh-io-diag-btn", () => this.loadIODiagnostics());
        bind("refresh-bootlog-btn", () => this.loadBootLog());
        bind("delete-bootlog-btn", () => this.deleteBootLog());
        bind("spindle-graph-pause", () => {
            this.spindlePaused = !this.spindlePaused;
            const t = document.getElementById("spindle-graph-pause");
            t && (t.textContent = this.spindlePaused ? "Resume" : "Pause");
        });
    },
    loadBootLog() { const textarea = document.getElementById("bootlog-content"); const sizeEl = document.getElementById("bootlog-size"); if (!textarea) return; if ("file:" === window.location.protocol) { textarea.value = "(Mock mode - boot log not available)"; if (sizeEl) sizeEl.textContent = "0"; return } window.API.get("logs/boot", null, { silent: true }).then(text => { textarea.value = text || "(Empty)"; if (sizeEl) sizeEl.textContent = text.length.toString() }).catch(err => { console.warn("[Diagnostics] Boot log fetch failed:", err); textarea.value = "(Failed to load boot log)"; if (sizeEl) sizeEl.textContent = "--" }) }, deleteBootLog() { if ("file:" === window.location.protocol) { AlertManager.add("Cannot delete in mock mode", "warning", 2e3); return } if (!confirm("Delete boot log?")) return; window.API.delete("logs/boot", "delete-bootlog-btn").then(data => { if (data.success) { AlertManager.add("Boot log deleted", "success", 2e3); this.loadBootLog() } else { AlertManager.add("Failed to delete boot log", "error", 3e3) } }).catch(err => { console.error("[Diagnostics] Delete boot log failed:", err); AlertManager.add("Failed to delete boot log", "error", 3e3) }) }, loadInitialData() { if ("file:" === window.location.protocol) return console.log("[Diagnostics] Mock mode - using simulated data"), void this.simulateDiagnosticData(); this.loadIOStatus(), this.loadFaultLog() }, onStateChanged() {
        const t = AppState.data;["x", "y", "z"].forEach(e => {
            const n = t.axis?.[e], a = t.motion?.dro_connected ?? !1, s = "Axis " + e.toUpperCase() + " Diagnostics"; this.updateHeaderNA(`diag-header-axis-${e}`, s, a); "x" === e && this.updateHeaderNA("diag-header-encoder", "Encoder Status", a);
            const o = document.getElementById(`diag-${e}-quality`), i = document.getElementById(`diag-${e}-jitter`), l = document.getElementById(`diag-${e}-stalls`), d = document.getElementById(`diag-${e}-active`);
            if (a && n) {
                o && (o.textContent = (n.quality || 0).toFixed(0));
                i && (i.textContent = (n.jitter_mms || 0).toFixed(3) + " mm/s");
                l && (l.textContent = n.stalled ? "⚠️ STALL" : "0");
                d && (d.textContent = "--");
            } else {
                this.setNA(o);
                this.setNA(i, " mm/s");
                this.setNA(l);
                if (d) this.setNA(d);
            }
        }); this.updateEncoderStatus(t); if (t.vfd) {
            this.updateHeaderNA("diag-header-vfd", "VFD Diagnostics", t.vfd.connected), this.updateHeaderNA("diag-header-spindle-rt", "🪚 Saw Blade Current (Real-time)", t.vfd.connected), this.updateHeaderNA("diag-header-spindle-trend", "Spindle Current (A)", t.vfd.connected); const e = document.getElementById("diag-vfd-current"), n = document.getElementById("diag-vfd-freq"), a = document.getElementById("diag-vfd-thermal"), s = document.getElementById("diag-vfd-fault"); t.vfd.connected ? (e && (e.textContent = (t.vfd.current_amps || 0).toFixed(1) + " A"), n && (n.textContent = (t.vfd.frequency_hz || 0).toFixed(1) + " Hz"), a && (a.textContent = (t.vfd.thermal_percent || 0) + "%"), s && (s.textContent = "0x" + (t.vfd.fault_code || 0).toString(16).padStart(4, "0").toUpperCase())) : (e && (e.textContent = "N/A"), n && (n.textContent = "N/A"), a && (a.textContent = "N/A"), s && (s.textContent = "N/A"))
        } t.safety && this.setIOIndicator("io-estop", t.safety.estop, !0);

        // SD Card Status (PHASE 6.6)
        this.updateSDStatus(t.sd);
    }, updateSDStatus(sd) {
        if (!sd) return;
        const indicator = document.getElementById("sd-status-indicator");
        const statusVal = document.getElementById("sd-status-val");
        const healthVal = document.getElementById("sd-health-val");
        const usageText = document.getElementById("sd-usage-text");
        const usageBar = document.getElementById("sd-usage-bar");

        if (indicator) {
            indicator.classList.remove("on", "off", "alarm");
            indicator.classList.add(sd.mounted ? (sd.health === 0 ? "on" : "alarm") : "off");
        }

        if (statusVal) {
            statusVal.textContent = sd.mounted ? "Mounted" : "Not Mounted";
            statusVal.style.color = sd.mounted ? "var(--color-optimal)" : "var(--text-secondary)";
        }

        if (healthVal) {
            const healthMap = ["OK", "Read-Only", "Write Failed", "Read Failed", "Corruption", "Delete Failed", "Not Mounted"];
            healthVal.textContent = healthMap[sd.health] || "Unknown";
            healthVal.style.color = sd.health === 0 ? "var(--color-optimal)" : "var(--color-critical)";
        }

        if (usageText && sd.mounted) {
            const totalMB = (sd.total_bytes / (1024 * 1024)).toFixed(0);
            const usedMB = (sd.used_bytes / (1024 * 1024)).toFixed(0);
            usageText.textContent = `${usedMB} / ${totalMB} MB`;

            if (usageBar) {
                const pct = sd.total_bytes > 0 ? (sd.used_bytes / sd.total_bytes * 100) : 0;
                usageBar.style.width = pct + "%";
                usageBar.style.background = pct > 90 ? "var(--color-critical)" : pct > 75 ? "var(--color-warning)" : "var(--color-optimal)";
            }
        }
    }, updateEncoderStatus(t) {
        const connected = t.motion?.dro_connected ?? false;
        ["x", "y", "z", "a"].forEach(e => {
            const n = t.axis?.[e], a = document.getElementById(`enc-${e}-error`), s = document.getElementById(`enc-${e}-feedback`);
            if (a) {
                if (connected && n) {
                    const t = n.following_error ?? n.vfd_error_percent ?? 0,
                        unit = "a" === e ? "°" : " mm";
                    a.textContent = t.toFixed(3) + unit;
                    a.classList.remove("value-na");
                    Math.abs(t) > .5 ? a.style.color = "var(--color-critical)" : Math.abs(t) > .1 ? a.style.color = "var(--color-warning)" : a.style.color = "var(--color-optimal)";
                } else {
                    this.setNA(a, "a" === e ? "°" : " mm");
                    a.style.color = "";
                }
            }
            if (s) {
                if (connected) {
                    s.textContent = "Active";
                    s.style.color = "var(--color-optimal)";
                    s.classList.remove("value-na");
                } else {
                    this.setNA(s);
                    s.style.color = "";
                }
            }
        })
    },
    loadIOStatus() { "file:" !== window.location.protocol && window.API.get("io/status", null, { silent: true }).then(t => { t.success && (this.setIOIndicator("io-estop", t.estop, !0), this.setIOIndicator("io-door", t.door), this.setIOIndicator("io-probe", t.probe), this.setIOIndicator("io-limit-x", t.limit_x, !0), this.setIOIndicator("io-limit-y", t.limit_y, !0), this.setIOIndicator("io-limit-z", t.limit_z, !0), this.setIOIndicator("io-spindle", t.spindle_on), this.setIOIndicator("io-coolant", t.coolant_on), this.setIOIndicator("io-vacuum", t.vacuum_on), this.setIOIndicator("io-alarm", t.alarm_on, !0)) }).catch(t => { console.warn("[Diagnostics] Failed to load I/O status:", t) }) }, setIOIndicator(t, e, n = !1) { const a = document.getElementById(t); a && (a.classList.remove("on", "off", "alarm"), e ? a.classList.add(n ? "alarm" : "on") : a.classList.add("off")) }, loadFaultLog() { "file:" !== window.location.protocol && window.API.get("faults", null, { silent: true }).then(t => { this.updateFaultDisplay(t.faults || []) }).catch(t => { console.warn("[Diagnostics] Failed to load fault log:", t); const e = document.getElementById("fault-list"); e && (e.innerHTML = `<div class="log-entry log-error"><span>Error loading fault log: ${t.message}</span></div>`) }) }, updateFaultDisplay(t) {
        const e = document.getElementById("fault-count"), n = document.getElementById("fault-list"); e && (e.textContent = t.length, e.classList.toggle("has-faults", t.length > 0)), n && (0 === t.length ? n.innerHTML = '<div class="fault-empty">No faults recorded</div>' : n.innerHTML = t.map(t => `
                    <div class="fault-item">
                        <div>
                            <span class="fault-code">0x${t.code.toString(16).padStart(2, "0").toUpperCase()}</span>
                            <span class="fault-desc">${t.description || "Unknown fault"}</span>
                        </div>
                        <span class="fault-time">${this.formatFaultTime(t.timestamp)}</span>
                    </div>
                `).join(""))
    }, formatFaultTime(t) { if (!t) return "--"; return new Date(t).toLocaleTimeString() }, clearFaults() { if ("file:" === window.location.protocol) return this.updateFaultDisplay([]), void AlertManager.add("Faults cleared (mock)", "success", 2e3); window.API.post("faults/clear", {}, "clear-faults-btn").then(t => { t.success ? (AlertManager.add("Fault log cleared", "success", 2e3), this.updateFaultDisplay([])) : AlertManager.add("Failed to clear faults", "error") }).catch(t => { console.error("[Diagnostics] Clear faults failed:", t), AlertManager.add("Failed to clear faults", "error") }) }, simulateDiagnosticData() { this.setIOIndicator("io-estop", !1), this.setIOIndicator("io-door", !1), this.setIOIndicator("io-probe", !1), this.setIOIndicator("io-limit-x", !1), this.setIOIndicator("io-limit-y", !1), this.setIOIndicator("io-limit-z", !1), this.setIOIndicator("io-spindle", !0), this.setIOIndicator("io-coolant", !1), this.setIOIndicator("io-vacuum", !1), this.setIOIndicator("io-alarm", !1), ["x", "y", "z", "a"].forEach(t => { const e = document.getElementById(`enc-${t}-error`), n = document.getElementById(`enc-${t}-feedback`), a = "a" === t ? "°" : " mm"; e && (e.textContent = (.1 * Math.random() - .05).toFixed(3) + a, e.style.color = "var(--color-optimal)"), n && (n.textContent = "Active", n.style.color = "var(--color-optimal)") }), this.updateFaultDisplay([]) }, loadTrendData() { "file:" !== window.location.protocol && window.API.get("history/telemetry", null, { silent: true }).then(t => { t.success && this.renderTrendCharts(t) }).catch(t => { console.warn("[Diagnostics] Failed to load trend data:", t) }) }, renderTrendCharts(t) { if (t.cpu && t.cpu.length > 0) { this.drawLineChart("chart-cpu", t.cpu, 0, 100, "#3b82f6"); const e = t.cpu[t.cpu.length - 1], n = t.cpu.reduce((t, e) => t + e, 0) / t.cpu.length, a = Math.max(...t.cpu); document.getElementById("diag-cpu-current").textContent = e, document.getElementById("diag-cpu-avg").textContent = n.toFixed(0), document.getElementById("diag-cpu-max").textContent = a } else this.setNA(document.getElementById("diag-cpu-current")), this.setNA(document.getElementById("diag-cpu-avg")), this.setNA(document.getElementById("diag-cpu-max")); if (t.heap && t.heap.length > 0) { const e = t.heap.map(t => t / 1024), n = Math.min(...e), a = Math.max(...e); this.drawLineChart("chart-memory", e, .9 * n, 1.1 * a, "#10b981"); const s = e[e.length - 1]; document.getElementById("diag-mem-current").textContent = s.toFixed(0), document.getElementById("diag-mem-min").textContent = n.toFixed(0) } else this.setNA(document.getElementById("diag-mem-current")), this.setNA(document.getElementById("diag-mem-min")); const e = AppState.data.vfd?.connected; if (e && t.spindle_amps && t.spindle_amps.length > 0) { const e = Math.max(...t.spindle_amps, 5); this.drawLineChart("chart-spindle", t.spindle_amps, 0, 1.2 * e, "#f59e0b"); const n = t.spindle_amps[t.spindle_amps.length - 1], a = t.spindle_amps.reduce((t, e) => t + e, 0) / t.spindle_amps.length, s = Math.max(...t.spindle_amps); document.getElementById("diag-spindle-current").textContent = n.toFixed(1), document.getElementById("diag-spindle-avg").textContent = a.toFixed(1), document.getElementById("diag-spindle-max").textContent = s.toFixed(1) } else { this.setNA(document.getElementById("diag-spindle-current")), this.setNA(document.getElementById("diag-spindle-avg")), this.setNA(document.getElementById("diag-spindle-max")); const t = document.getElementById("chart-spindle"); if (t) { t.getContext("2d").clearRect(0, 0, t.width, t.height) } } }, drawLineChart(t, e, n, a, s) { const o = document.getElementById(t); if (!o) return; const i = o.getContext("2d"), l = o.width, d = o.height, r = 10, c = 10, h = 20, u = 35, m = l - u - c, p = d - r - h; if (i.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--bg-tertiary") || "#1e293b", i.fillRect(0, 0, l, d), e.length < 2) return; const g = a - n || 1; i.strokeStyle = "#334155", i.lineWidth = 1, i.fillStyle = "#94a3b8", i.font = "10px sans-serif", i.textAlign = "right", i.textBaseline = "middle"; for (let t = 0; t <= 4; t++) { const e = t / 4, a = r + p - e * p, s = n + e * g; i.beginPath(), i.moveTo(u, a), i.lineTo(l - c, a), i.stroke(), i.fillText(s.toFixed(0), u - 5, a) } i.textAlign = "center", i.textBaseline = "top", i.fillText("-60s", u, d - h + 5), i.fillText("Now", l - c, d - h + 5), i.beginPath(), i.strokeStyle = s, i.lineWidth = 2; for (let t = 0; t < e.length; t++) { const a = u + t / (e.length - 1) * m, s = r + p - (e[t] - n) / g * p; 0 === t ? i.moveTo(a, s) : i.lineTo(a, s) } i.stroke(), i.lineTo(u + m, r + p), i.lineTo(u, r + p), i.closePath(); const I = i.createLinearGradient(0, r, 0, d - h); I.addColorStop(0, s + "40"), I.addColorStop(1, s + "00"), i.fillStyle = I, i.fill() }, loadSpindleData() { this.spindlePaused || window.API.get("spindle", null, { silent: true }).then(t => { for (this.spindleData.push({ time: Date.now(), current: t.current_amps || 0, peak: t.peak_amps || 0, threshold: t.threshold_amps || 30, autoPauseThreshold: t.auto_pause_threshold || 25, autoPauseCount: t.auto_pause_count || 0, overcurrent: t.overcurrent || !1 }); this.spindleData.length > this.spindleMaxPoints;)this.spindleData.shift(); const e = this.spindleData[this.spindleData.length - 1]; document.getElementById("spindle-current-now").textContent = e.current.toFixed(1) + " A", document.getElementById("spindle-current-peak").textContent = e.peak.toFixed(1) + " A", document.getElementById("spindle-threshold-pause").textContent = e.autoPauseThreshold + " A", document.getElementById("spindle-threshold-estop").textContent = e.threshold + " A", document.getElementById("spindle-pause-count").textContent = e.autoPauseCount; const n = document.getElementById("spindle-status-badge"); n && (e.overcurrent || e.current > e.threshold ? (n.className = "badge badge-danger", n.textContent = "OVERLOAD") : e.current > e.autoPauseThreshold ? (n.className = "badge badge-warning", n.textContent = "HIGH") : (n.className = "badge badge-success", n.textContent = "OK")), this.drawSpindleChart() }).catch(t => console.error("[Diagnostics] Spindle fetch error:", t)) }, drawSpindleChart() { const t = document.getElementById("spindle-current-chart"); if (!t) return; const e = t.getContext("2d"), n = t.width, a = t.height, s = this.spindleData, o = s.length > 0 ? s[s.length - 1].autoPauseThreshold : 25, i = s.length > 0 ? s[s.length - 1].threshold : 30, l = Math.max(1.2 * i, 35); e.fillStyle = "#1a1a2e", e.fillRect(0, 0, n, a), e.strokeStyle = "#333", e.lineWidth = .5; for (let t = 0; t <= 5; t++) { const s = a - t / 5 * a; e.beginPath(), e.moveTo(0, s), e.lineTo(n, s), e.stroke(), e.fillStyle = "#888", e.font = "10px sans-serif", e.fillText((l * t / 5).toFixed(0) + "A", 5, s - 2) } const d = a - o / l * a; e.strokeStyle = "#ff9800", e.lineWidth = 2, e.setLineDash([5, 5]), e.beginPath(), e.moveTo(0, d), e.lineTo(n, d), e.stroke(); const r = a - i / l * a; if (e.strokeStyle = "#f44336", e.beginPath(), e.moveTo(0, r), e.lineTo(n, r), e.stroke(), e.setLineDash([]), !(s.length < 2)) { e.strokeStyle = "#4ade80", e.lineWidth = 2, e.beginPath(); for (let t = 0; t < s.length; t++) { const o = t / (this.spindleMaxPoints - 1) * n, i = a - s[t].current / l * a; 0 === t ? e.moveTo(o, i) : e.lineTo(o, i) } e.stroke(), e.lineTo(n, a), e.lineTo(0, a), e.closePath(), e.fillStyle = "rgba(74, 222, 128, 0.2)", e.fill() } }, cleanup() { console.log("[Diagnostics] Cleaning up"), this.updateInterval && (clearInterval(this.updateInterval), this.updateInterval = null), this.trendInterval && (clearInterval(this.trendInterval), this.trendInterval = null), this.spindleInterval && (clearInterval(this.spindleInterval), this.spindleInterval = null), this.spindleData = [], this.ioDiagInterval && (clearInterval(this.ioDiagInterval), this.ioDiagInterval = null), this.tachometerInterval && (clearInterval(this.tachometerInterval), this.tachometerInterval = null), this.rs485Interval && (clearInterval(this.rs485Interval), this.rs485Interval = null) }, loadIODiagnostics() {
        const t = document.getElementById("io-input-grid"), e = document.getElementById("io-output-grid"); if (t && 0 === t.children.length) for (let e = 1; e <= 16; e++)t.innerHTML += `
                    <div class="io-diag-pin">
                        <div class="pin-led off" id="io-in-${e}"></div>
                        <span>X${e}</span>
                    </div>`; if (e && 0 === e.children.length) for (let t = 1; t <= 16; t++)e.innerHTML += `
                    <div class="io-diag-pin">
                        <div class="pin-led off" id="io-out-${t}"></div>
                        <span>Y${t}</span>
                    </div>`; window.API.get("hardware/io", null, { silent: true }).then(t => { if (t.success) { t.inputs?.forEach((t, e) => { const n = document.getElementById(`io-in-${e + 1}`); n && (n.classList.remove("on", "off"), n.classList.add(t.state ? "on" : "off")) }), t.outputs?.forEach((t, e) => { const n = document.getElementById(`io-out-${e + 1}`); n && (n.classList.remove("on", "off"), n.classList.add(t.state ? "on" : "off")) }); const e = document.getElementById("hw-estop-state"), n = document.getElementById("io-last-update"); e && (e.textContent = t.estop ? "⚠️ ACTIVE" : "✅ OK"), n && (n.textContent = (new Date).toLocaleTimeString()) } }).catch(t => console.warn("[Diagnostics] Failed to load I/O diagnostics:", t))
    }, startTachometerPolling() { this.tachometerInterval && clearInterval(this.tachometerInterval), this.tachometerInterval = setInterval(() => this.updateTachometer(), 1e3), this.updateTachometer() }, updateTachometer() { document.getElementById("tach_rpm") && window.API.get("hardware/tachometer", null, { silent: true }).then(t => { const e = !1 !== t.enabled; if (this.updateHeaderNA("diag-header-tach", "YH-TC05 Tachometer", e), !e) { this.setNA(document.getElementById("tach_rpm"), ' <span style="font-size: 0.5em; vertical-align: middle;">RPM</span>'), this.setNA(document.getElementById("tach_peak"), ' <span style="font-size: 0.5em; vertical-align: middle;">RPM</span>'), this.setNA(document.getElementById("tach_pulses")), document.getElementById("tach_errors").textContent = "N/A"; const t = document.getElementById("tach_status"); return t.textContent = "DISABLED", void (t.className = "status-badge status-unknown") } document.getElementById("tach_rpm").textContent = t.rpm, document.getElementById("tach_peak").textContent = t.peak_rpm, document.getElementById("tach_pulses").textContent = t.pulse_count, document.getElementById("tach_errors").textContent = t.error_count; const n = document.getElementById("tach_status"); t.stalled ? (n.textContent = "STALLED", n.className = "status-badge status-disconnected") : t.spinning ? (n.textContent = "SPINNING", n.className = "status-badge status-connected") : (n.textContent = "IDLE", n.className = "status-badge status-unknown") }).catch(t => { console.warn("[Diagnostics] Tachometer fetch failed:", t), this.updateHeaderNA("diag-header-tach", "YH-TC05 Tachometer", !1) }) },
    updateRS485Status() {
        if ("file:" === window.location.protocol) return;
        window.API.get("hardware/rs485/status", null, { silent: true }).then(data => {
            const statusEl = document.getElementById("rs485-bus-status");
            const indicator = document.getElementById("rs485-status-indicator");
            const deviceCountEl = document.getElementById("rs485-device-count");
            const txCountEl = document.getElementById("rs485-tx-count");
            const errorRateEl = document.getElementById("rs485-error-rate");
            const deviceListEl = document.getElementById("rs485-device-list");

            const deviceCount = data.device_count || 0;

            if (indicator) {
                indicator.classList.remove("on", "off", "alarm");
                if (deviceCount === 0) {
                    indicator.classList.add("off"); // Grey for no devices
                } else {
                    indicator.classList.add(data.healthy ? "on" : "alarm");
                }
            }
            if (statusEl) {
                if (deviceCount === 0) {
                    statusEl.textContent = "No Devices";
                    statusEl.style.color = "var(--text-secondary)";
                } else {
                    statusEl.textContent = data.healthy ? "Healthy" : (data.watchdog_alert ? "TIMEOUT" : "Error");
                    statusEl.style.color = data.healthy ? "var(--color-optimal)" : "var(--color-critical)";
                }
            }
            if (deviceCountEl) deviceCountEl.textContent = deviceCount;
            if (txCountEl) txCountEl.textContent = this.formatNumber(data.total_transactions || 0);
            if (errorRateEl) {
                const rate = data.error_rate || 0;
                errorRateEl.textContent = rate.toFixed(1) + "%";
                errorRateEl.style.color = rate > 5 ? "var(--color-critical)" : rate > 1 ? "var(--color-warning)" : "";
            }
            if (deviceListEl) {
                if (!data.devices || data.devices.length === 0) {
                    deviceListEl.innerHTML = '<span style="color: var(--text-tertiary);">No devices registered</span>';
                } else {
                    deviceListEl.innerHTML = data.devices.map(d => {
                        const icon = d.healthy ? "✅" : "❌";
                        const errInfo = d.error_count > 0 ? ` (${d.error_count} errors)` : "";
                        return `<div style="margin-bottom: 4px;">${icon} ${d.name} @ ${d.address}${errInfo}</div>`;
                    }).join("");
                }
            }
        }).catch(e => console.warn("[Diagnostics] RS485 status fetch failed:", e));
    },
    formatNumber(n) { if (n >= 1e6) return (n / 1e6).toFixed(1) + "M"; if (n >= 1e3) return (n / 1e3).toFixed(1) + "K"; return n.toString(); }
}, window.currentPageModule = DiagnosticsModule;
