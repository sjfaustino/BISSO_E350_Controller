window.HardwareModule = window.HardwareModule || {
    ioInterval: null,
    pinData: null,
    signalData: null,

    init() {
        console.log("[Hardware] Initializing");
        this.updateVariantVisibility();
        this.loadHandlers().then(() => {
            this.loadPinConfiguration();
            this.setupEventListeners();
        });
    },

    isV31() {
        const sys = window.AppState?.data?.system;
        return sys && (sys.hw_model?.includes("v3.1") || sys.hw_has_sd === true);
    },

    updateVariantVisibility() {
        const isV31 = this.isV31();
        document.querySelectorAll("[data-board-variant]").forEach(el => {
            const variant = el.dataset.boardVariant;
            if (variant === "v31") {
                el.style.display = isV31 ? "block" : "none";
            } else if (variant === "v16") {
                el.style.display = isV31 ? "none" : "block";
            }
        });
    },

    loadHandlers() {
        return new Promise((resolve) => {
            if (window.HardwareHandlers) return resolve();
            const s = document.createElement('script');
            s.src = "pages/hardware/hardware-handlers.js";
            s.onload = resolve;
            s.onerror = () => { console.error("Failed to load hardware handlers"); resolve(); };
            document.head.appendChild(s);
        });
    },

    loadPinConfiguration() {
        window.API.get("hardware/pins").then(e => {
            console.log("[Hardware] Pin configuration loaded:", e);
            this.pinData = e.pins || [];
            this.signalData = e.signals || [];
            this.renderPinConfiguration(e);
            this.loadConfigValues();
        }).catch(e => {
            console.error("[Hardware] Failed to load pin configuration:", e);
            this.showStatus(window.i18n.t('hardware.load_failed'), "error");
        });
    },

    renderPinConfiguration(e) {
        console.log("[Hardware] Rendering pin configuration");
        document.querySelectorAll(".pin-select").forEach(el => {
            if (el.disabled || el.dataset.keepOptions === "true") return;
            const t = el.dataset.signal;
            el.innerHTML = '';
            const defaultOpt = document.createElement('option');
            defaultOpt.value = "";
            defaultOpt.textContent = window.i18n.t('hardware.select_pin');
            el.appendChild(defaultOpt);

            if (t && t.startsWith("output_")) {
                for (let i = 1; i <= 16; i++) {
                    const n = document.createElement("option");
                    n.value = 115 + i;
                    n.textContent = `Y${i}`;
                    el.appendChild(n);
                }
            } else if (t && t.startsWith("input_")) {
                for (let i = 1; i <= 16; i++) {
                    const n = document.createElement("option");
                    n.value = 99 + i;
                    n.textContent = `X${i}`;
                    el.appendChild(n);
                }
            } else if (this.pinData?.length > 0) {
                this.pinData.filter(p => p.gpio < 100).forEach(p => {
                    const n = document.createElement("option");
                    n.value = p.gpio;
                    n.textContent = `${p.silk} (GPIO ${p.gpio})`;
                    el.appendChild(n);
                });
            }

            if (this.signalData) {
                const n = this.signalData.find(s => s.key === t);
                if (n && n.current_pin) el.value = n.current_pin;
            }

            if (!el.value) {
                switch (t) {
                    case "wj66_rx": el.value = 16; break;
                    case "wj66_tx": el.value = 13; break;
                    case "output_status_green": el.value = 124; break;
                    case "output_status_yellow": el.value = 125; break;
                    case "output_status_red": el.value = 126; break;
                    case "output_buzzer": el.value = 127; break;
                }
            }
        });
    },

    renderPinSummary() {
        const el = document.getElementById("pin-summary-table");
        if (!el) return;

        const isV31 = this.isV31();
        const staticMap = isV31 ? [
            ...Array.from({ length: 16 }, (_, i) => ({ label: `X${i + 1}`, type: "Digital Input", pin: 100 + i })),
            ...Array.from({ length: 16 }, (_, i) => ({ label: `Y${i + 1}`, type: "Digital Output", pin: 116 + i })),
            { label: "CH1", type: "0-5V Input", pin: 4 },
            { label: "CH2", type: "0-5V Input", pin: 6 },
            { label: "CH3", type: "0-5V Input", pin: 7 },
            { label: "CH4", type: "0-5V Input", pin: 5 },
            { label: "HT1", type: "1-Wire/GPIO", pin: 47 },
            { label: "HT2", type: "1-Wire/GPIO", pin: 48 },
            { label: "HT3", type: "1-Wire/GPIO", pin: 38 },
            { label: "RS485_A", type: "RS485", pin: 17 },
            { label: "RS485_B", type: "RS485", pin: 16 },
            { label: "I2C_SDA", type: "I2C", pin: 9 },
            { label: "I2C_SCL", type: "I2C", pin: 10 },
            { label: "RF433_RX", type: "RF Input", pin: 8 },
            { label: "RF433_TX", type: "RF Output", pin: 18 }
        ] : [
            ...Array.from({ length: 16 }, (_, i) => ({ label: `X${i + 1}`, type: "Digital Input", pin: 100 + i })),
            ...Array.from({ length: 16 }, (_, i) => ({ label: `Y${i + 1}`, type: "Digital Output", pin: 116 + i })),
            { label: "CH1", type: "4/20mA Input", pin: 36 },
            { label: "CH2", type: "4/20mA Input", pin: 34 },
            { label: "CH3", type: "0-5V Input", pin: 35 },
            { label: "CH4", type: "0-5V Input", pin: 39 },
            { label: "HT1", type: "Sensor/GPIO", pin: 14 },
            { label: "HT2", type: "Sensor/GPIO", pin: 33 },
            { label: "HT3", type: "Sensor/GPIO", pin: 32 },
            { label: "RS485_A", type: "RS485", pin: 16 },
            { label: "RS485_B", type: "RS485", pin: 13 },
            { label: "I2C_SDA", type: "I2C", pin: 4 },
            { label: "I2C_SCL", type: "I2C", pin: 5 },
            { label: "433MHz_RX", type: "RF Input", pin: 2 },
            { label: "433MHz_TX", type: "RF Output", pin: 15 }
        ];

        const pinUsage = new Map();
        const addUsage = (p, name) => {
            if (p < 0) return;
            if (!pinUsage.has(p)) pinUsage.set(p, []);
            pinUsage.get(p).push(name);
        };

        this.signalData?.forEach(s => {
            // Skip WJ66 pins as they are handled dynamically below based on interface selection
            if (s.key === 'wj66_rx' || s.key === 'wj66_tx') return;

            const p = s.current_pin !== undefined ? s.current_pin : s.default_pin;
            if (p !== undefined) addUsage(p, s.name || s.key);
        });

        // Hardware revision specific fixed usages
        if (isV31) {
            addUsage(17, "RS485 (MAX13487 RX)"); addUsage(16, "RS485 (MAX13487 TX)");
            addUsage(9, "I2C (SDA)"); addUsage(10, "I2C (SCL)");
            addUsage(42, "Ethernet (CLK)"); addUsage(43, "Ethernet (MOSI)");
            addUsage(44, "Ethernet (MISO)"); addUsage(15, "Ethernet (CS)");
            addUsage(12, "SD Card (MOSI)"); addUsage(13, "SD Card (SCK)");
            addUsage(14, "SD Card (MISO)"); addUsage(11, "SD Card (CS)");
        } else {
            addUsage(23, "Ethernet MDC"); addUsage(18, "Ethernet MDIO");
            addUsage(17, "Ethernet CLK/Pwr"); addUsage(19, "Ethernet TXD0");
            addUsage(21, "Ethernet TX_EN"); addUsage(22, "Ethernet TXD1");
            addUsage(25, "Ethernet RXD0"); addUsage(26, "Ethernet RXD1");
            addUsage(27, "Ethernet CRS_DV");
            addUsage(4, "LCD Display (SDA)"); addUsage(5, "LCD Display (SCL)");
        }

        // WJ66 Encoder pins based on interface selection
        const wj66Iface = document.getElementById("wj66_interface")?.value;
        if (wj66Iface == "1") {  // Use == to handle both "1" and 1
            if (isV31) {
                addUsage(17, "WJ66 Encoder (RX)"); addUsage(16, "WJ66 Encoder (TX)");
            } else {
                addUsage(16, "WJ66 Encoder (RX)"); addUsage(13, "WJ66 Encoder (TX)");
            }
        } else {
            if (isV31) {
                addUsage(47, "WJ66 Encoder (RX)"); addUsage(48, "WJ66 Encoder (TX)");
            } else {
                addUsage(14, "WJ66 Encoder (RX)"); addUsage(33, "WJ66 Encoder (TX)");
            }
        }

        let html = `
            <table style="width: 100%; font-size: 12px; border-collapse: collapse;">
                <thead>
                    <tr style="background: var(--bg-secondary);">
                        <th style="padding: 8px; text-align: left; width: 70px;">${window.i18n.t('hardware.pin_label')}</th>
                        <th style="padding: 8px; text-align: left; width: 100px;">${window.i18n.t('hardware.type')}</th>
                        <th style="padding: 8px; text-align: left;">${window.i18n.t('hardware.connected_function')}</th>
                    </tr>
                </thead>
                <tbody>
        `;

        staticMap.forEach(item => {
            const usages = pinUsage.get(item.pin) || [];
            const unique = [...new Set(usages)];
            const usageStr = unique.length > 0 ? unique.join(" + ") : "";
            const style = unique.length > 0 ? "font-weight: 500; color: var(--text-primary);" : "";
            html += `
                <tr>
                    <td style="padding: 6px 8px; font-family: monospace; font-weight: bold; color: var(--accent-primary); border-bottom: 1px solid var(--bg-secondary);">${item.label}</td>
                    <td style="padding: 6px 8px; border-bottom: 1px solid var(--bg-secondary);">${item.type}</td>
                    <td style="padding: 6px 8px; ${style} white-space: normal; word-break: break-word; border-bottom: 1px solid var(--bg-secondary);">${usageStr || "-"}</td>
                </tr>
            `;
        });

        html += "</tbody></table>";
        el.innerHTML = html;
    },

    getPinLabel(pin) {
        if (pin == null) return "--";
        const p = this.pinData?.find(x => x.gpio === pin);
        if (p) return p.silk || p.note || `GPIO ${pin}`;
        if (pin >= 100 && pin <= 115) return "X" + (pin - 99);
        if (pin >= 116 && pin <= 131) return "Y" + (pin - 115);
        if (pin === 36) return "CH1";
        if (pin === 34) return "CH2";
        if (pin === 35) return "CH3";
        if (pin === 39) return "CH4";
        return `GPIO ${pin}`;
    },

    loadConfigValues() {
        fetch(`/api/config?_=${Date.now()}`).then(res => res.json()).then(data => {
            const setChecked = (id, val) => {
                const el = document.getElementById(id);
                if (el) el.checked = (val === 1 || val === "1" || val === true);
            };
            const setValue = (id, val) => {
                const el = document.getElementById(id);
                if (el && val !== undefined) el.value = val;
            };

            // System-level enable flags (nested under data.system)
            const sys = data.system || {};
            // Robust check: handle both full key (API standard) and short key (NVS fallback)
            const sl_en = (sys.status_light_en !== undefined) ? sys.status_light_en : sys.sl_en;
            setChecked("status_light_enabled", sl_en);

            setChecked("lcd_enabled", sys.lcd_en);
            setChecked("buzzer_enabled", sys.buzzer_en);

            // Network-level flags (nested under data.network)
            const net = data.network || {};
            setChecked("eth_enabled", net.eth_en);

            // Spindle-level flags (nested under data.spindle)
            const spindle = data.spindle || {};
            setChecked("vfd_enabled", spindle.jxk10_en);  // VFD uses JXK10 enable
            setValue("vfd_addr", data.vfd?.address);
            setChecked("jxk10_enabled", spindle.jxk10_en);
            setValue("jxk10_addr", spindle.jxk10_addr);
            setChecked("tach_enabled", spindle.yhtc05_en);
            setValue("tach_addr", spindle.yhtc05_addr);

            // Serial communication settings
            const serial = data.serial || {};
            setValue("wj66_baud", serial.encoder_baud || "9600");
            setValue("wj66_interface", serial.encoder_iface != null ? serial.encoder_iface : "1");
            setValue("wj66_address", serial.encoder_addr != null ? serial.encoder_addr : "1");
            setValue("wj66_protocol", serial.enc_proto != null ? serial.enc_proto : "0");
            setValue("rs485_baud", serial.rs485_baud || "9600");
            setValue("i2c_speed", serial.i2c_speed || "100000");

            this.updateWJ66Interface();
            this.renderPinSummary();
        }).catch(e => console.warn("[Hardware] Config load error:", e));
    },

    setupEventListeners() {
        const h = window.HardwareHandlers;
        if (!h) {
            console.warn("[Hardware] Handlers not loaded yet, retrying...");
            setTimeout(() => this.setupEventListeners(), 200);
            return;
        }

        const bindClick = (id, handler, arg) => {
            const el = document.getElementById(id);
            if (el) el.onclick = () => handler(arg);
        };

        bindClick("btn-detect-baud", h.detectBaud, "btn-detect-baud");
        bindClick("btn-detect-rs485", h.detectRS485, "btn-detect-rs485");
        bindClick("save-config-btn", h.saveConfiguration, this);
        bindClick("reset-defaults-btn", h.resetConfiguration, this);
        bindClick("btn-test-i2c", h.testI2C, "btn-test-i2c");

        const reloadBtn = document.getElementById("reload-config-btn");
        if (reloadBtn) {
            reloadBtn.onclick = () => {
                this.loadPinConfiguration();
                this.showStatus(window.i18n.t('hardware.reloaded'), "info");
            };
        }

        const interfaceEl = document.getElementById("wj66_interface");
        if (interfaceEl) {
            interfaceEl.onchange = () => this.updateWJ66Interface();
        }
    },

    updateWJ66Interface() {
        const interfaceEl = document.getElementById("wj66_interface");
        if (!interfaceEl) return;

        const isRS485 = interfaceEl.value === "1";
        const badge = document.getElementById("wj66-protocol-badge");
        const note = document.getElementById("wj66-rs485-note");
        const rxEl = document.getElementById("wj66_rx");
        const txEl = document.getElementById("wj66_tx");

        if (isRS485) {
            if (badge) badge.textContent = "RS485 Modbus/ASCII";
            if (note) note.style.display = "block";
            if (rxEl) { rxEl.value = "16"; rxEl.disabled = true; }
            if (txEl) { txEl.value = "13"; txEl.disabled = true; }
        } else {
            if (badge) badge.textContent = "RS232 TTL";
            if (note) note.style.display = "none";
            if (rxEl) { rxEl.disabled = false; if (rxEl.value == "16") rxEl.value = "14"; }
            if (txEl) { txEl.disabled = false; if (txEl.value == "13") txEl.value = "33"; }
        }

        // Update pin summary to reflect the new interface
        this.renderPinSummary();
    },

    showStatus(msg, type) {
        if (window.Toast) {
            window.Toast.show(msg, type, type === "error" ? 0 : 5000);
        } else {
            console.log(`[Hardware Status] ${type?.toUpperCase()}: ${msg}`);
        }
    },

    cleanup() {
        console.log("[Hardware] Cleaning up");
    }
};
window.currentPageModule = window.HardwareModule;