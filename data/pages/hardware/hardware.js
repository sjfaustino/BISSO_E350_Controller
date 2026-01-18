window.HardwareModule = window.HardwareModule || {
    ioInterval: null,
    pinData: null,
    signalData: null,

    init() {
        console.log("[Hardware] Initializing");
        this.loadHandlers().then(() => {
            this.loadPinConfiguration();
            this.setupEventListeners();
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
            if (el.disabled) return;
            const t = el.dataset.signal;
            el.innerHTML = '<option value="">' + window.i18n.t('hardware.select_pin') + '</option>';

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
                    case "wj66_rx": el.value = 14; break;
                    case "wj66_tx": el.value = 33; break;
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

        const staticMap = [
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
            { label: "433MHz_RX", type: "RF Input", pin: -1 },
            { label: "433MHz_TX", type: "RF Output", pin: -1 }
        ];

        const pinUsage = new Map();
        const addUsage = (p, name) => {
            if (p < 0) return;
            if (!pinUsage.has(p)) pinUsage.set(p, []);
            pinUsage.get(p).push(name);
        };

        this.signalData?.forEach(s => {
            const p = s.current_pin !== undefined ? s.current_pin : s.default_pin;
            if (p !== undefined) addUsage(p, s.name || s.key);
        });

        // Hardcoded generic usages
        addUsage(23, "Ethernet MDC"); addUsage(18, "Ethernet MDIO");
        addUsage(17, "Ethernet CLK/Pwr"); addUsage(19, "Ethernet TXD0");
        addUsage(21, "Ethernet TX_EN"); addUsage(22, "Ethernet TXD1");
        addUsage(25, "Ethernet RXD0"); addUsage(26, "Ethernet RXD1");
        addUsage(27, "Ethernet CRS_DV");
        addUsage(4, "LCD Display (SDA)"); addUsage(5, "LCD Display (SCL)");

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
        fetch("/api/config").then(res => res.json()).then(data => {
            const setChecked = (id, val) => {
                const el = document.getElementById(id);
                if (el) el.checked = (val === 1 || val === "1" || val === true);
            };
            const setValue = (id, val) => {
                const el = document.getElementById(id);
                if (el && val !== undefined) el.value = val;
            };

            setChecked("status_light_enabled", data.status_light_en);
            setChecked("eth_enabled", data.eth_en);
            setChecked("lcd_enabled", data.lcd_en);
            setChecked("buzzer_enabled", data.buzzer_en);
            setChecked("vfd_enabled", data.vfd_en);
            setValue("vfd_addr", data.vfd_addr);
            setChecked("jxk10_enabled", data.jxk10_en);
            setValue("jxk10_addr", data.jxk10_addr);
            setChecked("tach_enabled", data.yhtc05_en);
            setValue("tach_addr", data.yhtc05_addr);
            setValue("wj66_baud", data.encoder_baud || "9600");
            setValue("rs485_baud", data.rs485_baud || "9600");
            setValue("i2c_speed", data.i2c_speed || "100000");

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