import { DEFAULT_NTP_SERVER, showStatus, isValidIp, isValidNetmask, authFetch } from "./common.js";

let currentConfig = null;

/* ===== 实际网络信息显示控制 ===== */
function updateActualNetInfoVisible() {
    const block = document.getElementById("actualNetInfo");
    if (!block) return;

    const mode = getWifiMode();
    const ipMode = document.getElementById("ipMode").value;
    const show = (mode === "station" && ipMode === "dhcp");
    block.style.display = show ? "block" : "none";
}

function fillActualNetInfo(cfg) {
    const setText = function (id, val) {
        const el = document.getElementById(id);
        if (el) el.textContent = (val && val.length > 0) ? val : "--";
    };
    setText("actualIp",      cfg.actual_ip);
    setText("actualNetmask", cfg.actual_netmask);
    setText("actualGateway", cfg.actual_gateway);
    setText("actualDns1",    cfg.actual_dns1);
    setText("actualDns2",    cfg.actual_dns2);
}

export function updateModeUI() {
    const mode = getWifiMode();
    const stationFields = document.getElementById("stationFields");
    stationFields.style.display = (mode === "station") ? "block" : "none";
    updateActualNetInfoVisible();
}

function getWifiMode() {
    const radio = document.querySelector('input[name="wifiModeRadio"]:checked');
    return radio ? radio.value : "ap";
}

function setWifiMode(mode) {
    const radio = document.querySelector('input[name="wifiModeRadio"][value="' + mode + '"]');
    if (radio) {
        radio.checked = true;
    }
}

export function loadVolume() {
    authFetch("/api/volume")
        .then(function (resp) { return resp.json(); })
        .then(function (data) {
            let v = data.volume;
            if (typeof v !== "number" || v < 10) v = 70;
            if (v > 100) v = 100;
            const slider = document.getElementById("volumeSlider");
            if (slider) slider.value = v;
            updateVolumeDisplay();
        })
        .catch(function (err) {
            console.warn("Load volume failed:", err);
            const slider = document.getElementById("volumeSlider");
            if (slider) slider.value = 70;
            updateVolumeDisplay();
        });
}

export function updateVolumeDisplay() {
    const slider = document.getElementById("volumeSlider");
    const valEl = document.getElementById("volumeValue");
    if (!slider || !valEl) return;

    const v = parseInt(slider.value) || 10;
    valEl.textContent = v + "%";

    const min = parseInt(slider.min) || 10;
    const max = parseInt(slider.max) || 100;
    const pct = ((v - min) / (max - min)) * 100;
    slider.style.setProperty("--vol-pct", pct.toFixed(2) + "%");
}

export function updateLedBrightnessDisplay() {
    const slider = document.getElementById("ledBrightnessSlider");
    const valEl = document.getElementById("ledBrightnessValue");
    if (!slider || !valEl) return;

    const v = parseInt(slider.value) || 10;
    valEl.textContent = v + "%";

    const min = parseInt(slider.min) || 10;
    const max = parseInt(slider.max) || 100;
    const pct = ((v - min) / (max - min)) * 100;
    slider.style.setProperty("--vol-pct", pct.toFixed(2) + "%");
}

export function testVolume() {
    const slider = document.getElementById("volumeSlider");
    if (!slider) return;
    const vol = parseInt(slider.value) || 70;

    const btn = document.getElementById("volumeTestBtn");
    if (btn) btn.disabled = true;

    authFetch("/api/volume_test", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ volume: vol })
    })
        .then(function (resp) { return resp.json(); })
        .then(function (data) {
            if (data.ok) {
                showStatus("wifiStatus", "音量已保存：" + data.volume + "%，正在播放测试音乐", false);
            } else {
                showStatus("wifiStatus", "音量设置失败", true);
            }
        })
        .catch(function (err) {
            console.error("Volume test failed:", err);
            showStatus("wifiStatus", "音量设置失败：" + err.message, true);
        })
        .finally(function () {
            if (btn) btn.disabled = false;
        });
}

export function updateIpModeUI() {
    const ipMode = document.getElementById("ipMode").value;
    const fields = document.getElementById("staticIpFields");
    fields.style.display = (ipMode === "static") ? "block" : "none";

    const isDhcp = (ipMode === "dhcp");
    ["ipAddr", "netmask", "gateway", "dns1", "dns2"].forEach(function (id) {
        const el = document.getElementById(id);
        el.readOnly = isDhcp;
        el.style.backgroundColor = isDhcp ? "#f0f0f0" : "white";
    });

    if (isDhcp) {
        authFetch("/api/wifi_config")
            .then(function (resp) { return resp.json(); })
            .then(function (cfg) {
                document.getElementById("ipAddr").value  = cfg.actual_ip      || "";
                document.getElementById("netmask").value = cfg.actual_netmask || "";
                document.getElementById("gateway").value = cfg.actual_gateway || "";
                document.getElementById("dns1").value    = cfg.actual_dns1    || "";
                document.getElementById("dns2").value    = cfg.actual_dns2    || "";
                fillActualNetInfo(cfg);
            })
            .catch(function (err) { console.warn("Refresh actual IP failed:", err); });
    }

    updateActualNetInfoVisible();
}

export function loadWifiConfig() {
    authFetch("/api/wifi_config")
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.json();
        })
        .then(function (cfg) {
            console.log("Loaded config:", cfg);
            currentConfig = cfg;
            if (cfg.mode) {
                setWifiMode(cfg.mode);
            }
            if (cfg.ssid) {
                document.getElementById("wifiSsid").value = cfg.ssid;
            }

            const ntpEl = document.getElementById("ntpServer");
            if (ntpEl) {
                ntpEl.value = cfg.ntp_server && cfg.ntp_server.length > 0
                    ? cfg.ntp_server : DEFAULT_NTP_SERVER;
            }

            const ledEl = document.getElementById("ledNormalOn");
            if (ledEl) {
                ledEl.checked = (cfg.led_normal_on === 1);
            }

            const ledBriSlider = document.getElementById("ledBrightnessSlider");
            if (ledBriSlider) {
                let b = cfg.led_brightness;
                if (typeof b !== "number" || b < 10) b = 50;
                if (b > 100) b = 100;
                ledBriSlider.value = b;
                updateLedBrightnessDisplay();
            }

            const passInput = document.getElementById("wifiPass");
            if (cfg.has_password) {
                passInput.value = "";
                passInput.placeholder = "（不更改）";
                passInput.dataset.hasPassword = "true";
            } else {
                passInput.placeholder = "WiFi 密码";
                passInput.dataset.hasPassword = "false";
            }

            if (cfg.ip_mode) {
                document.getElementById("ipMode").value = cfg.ip_mode;
            }

            const ipMode = cfg.ip_mode || "dhcp";
            if (ipMode === "static") {
                if (cfg.ip)      document.getElementById("ipAddr").value  = cfg.ip;
                if (cfg.netmask) document.getElementById("netmask").value = cfg.netmask;
                if (cfg.gateway) document.getElementById("gateway").value = cfg.gateway;
                if (cfg.dns1)    document.getElementById("dns1").value    = cfg.dns1;
                if (cfg.dns2)    document.getElementById("dns2").value    = cfg.dns2;
            } else {
                document.getElementById("ipAddr").value  = cfg.actual_ip      || "";
                document.getElementById("netmask").value = cfg.actual_netmask || "";
                document.getElementById("gateway").value = cfg.actual_gateway || "";
                document.getElementById("dns1").value    = cfg.actual_dns1    || "";
                document.getElementById("dns2").value    = cfg.actual_dns2    || "";

                ["ipAddr", "netmask", "gateway", "dns1", "dns2"].forEach(function (id) {
                    document.getElementById(id).readOnly = true;
                    document.getElementById(id).style.backgroundColor = "#f0f0f0";
                });
            }

            fillActualNetInfo(cfg);
            updateIpModeUI();
            updateModeUI();
        })
        .catch(function (err) {
            console.warn("No config loaded:", err);
            const ntpEl = document.getElementById("ntpServer");
            if (ntpEl) ntpEl.value = DEFAULT_NTP_SERVER;
            const ledEl = document.getElementById("ledNormalOn");
            if (ledEl) ledEl.checked = true;
            const ledBriSlider = document.getElementById("ledBrightnessSlider");
            if (ledBriSlider) {
                ledBriSlider.value = 50;
                updateLedBrightnessDisplay();
            }
            updateModeUI();
        });
}

export function saveWifiConfig() {
    const mode = getWifiMode();
    const ssid = document.getElementById("wifiSsid").value;
    const password = document.getElementById("wifiPass").value;

    if (mode === "station" && ssid.length === 0) {
        showStatus("wifiStatus", "SSID 不能为空", true);
        return;
    }

    const ipMode = document.getElementById("ipMode").value;
    const ipAddr = document.getElementById("ipAddr").value;
    const netmask = document.getElementById("netmask").value;
    const gateway = document.getElementById("gateway").value;
    const dns1 = document.getElementById("dns1").value;
    const dns2 = document.getElementById("dns2").value;

    let ntpServer = document.getElementById("ntpServer").value.trim();
    if (ntpServer.length === 0) ntpServer = DEFAULT_NTP_SERVER;

    const ledEl = document.getElementById("ledNormalOn");
    const ledNormalOn = (ledEl && ledEl.checked) ? 1 : 0;

    const ledBriEl = document.getElementById("ledBrightnessSlider");
    let ledBrightness = ledBriEl ? parseInt(ledBriEl.value) : 50;
    if (isNaN(ledBrightness) || ledBrightness < 10) ledBrightness = 10;
    if (ledBrightness > 100) ledBrightness = 100;

    if (mode === "station" && ipMode === "static") {
        if (!ipAddr || !netmask) {
            showStatus("wifiStatus", "静态IP模式下，IP和掩码不能为空", true);
            return;
        }
        if (!isValidIp(ipAddr)) {
            showStatus("wifiStatus", "IP 格式不正确", true);
            return;
        }
        if (!isValidNetmask(netmask)) {
            showStatus("wifiStatus", "掩码格式不正确", true);
            return;
        }
        if (gateway && !isValidIp(gateway)) {
            showStatus("wifiStatus", "网关格式不正确", true);
            return;
        }
        if (dns1 && !isValidIp(dns1)) {
            showStatus("wifiStatus", "DNS主格式不正确", true);
            return;
        }
        if (dns2 && !isValidIp(dns2)) {
            showStatus("wifiStatus", "DNS备格式不正确", true);
            return;
        }
    }

    const cfg = {
        mode: mode,
        ssid: ssid,
        password: password,
        ip_mode: ipMode,
        ip: ipAddr,
        netmask: netmask,
        gateway: gateway,
        dns1: dns1,
        dns2: dns2,
        ntp_server: ntpServer,
        led_normal_on: ledNormalOn,
        led_brightness: ledBrightness
    };

    if (mode === "ap") {
        cfg.password = "";
    }

    console.log("Saving config:", cfg);

    authFetch("/api/wifi_config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(cfg)
    })
        .then(function (resp) {
            return resp.json().then(function (data) {
                return { ok: resp.ok, data: data };
            });
        })
        .then(function (r) {
            const data = r.data || {};
            if (r.ok && data.ok) {
                showStatus("wifiStatus", "配置已保存", false);

                setTimeout(function () {
                    const oldMode = currentConfig ? currentConfig.mode : null;
                    const newMode = mode;
                    let msg = "";

                    if (oldMode !== "station" && newMode === "station") {
                        msg = "配置已保存，是否立即重启设备以进入新模式？\n\n" +
                              "重启后：\n- 将连接到 WiFi：" + ssid;
                    } else if (oldMode !== "ap" && newMode === "ap") {
                        msg = "配置已保存，是否立即重启设备以进入新模式？\n\n" +
                              "重启后：\n- 将切换为 AP 模式\n- WiFi 名称：OSHWIR\n- 网页地址：192.168.5.1";
                    } else {
                        if (newMode === "station") {
                            msg = "配置已保存，是否立即重启设备？\n\n重启后将连接到 WiFi：" + ssid;
                        } else {
                            msg = "配置已保存，是否立即重启设备？\n\n重启后：\n- AP 模式，WiFi 名称：OSHWIR\n- 网页地址：192.168.5.1";
                        }
                    }

                    if (confirm(msg)) {
                        rebootDevice();
                    } else {
                        showStatus("wifiStatus", "配置已保存（重启后生效）", false);
                    }
                }, 100);
            } else {
                const msg = data.msg || "保存失败";
                showStatus("wifiStatus", msg, true);
            }
        })
        .catch(function (err) {
            console.error("Save failed:", err);
            showStatus("wifiStatus", "保存失败：" + err.message, true);
        });
}

function rebootDevice() {
    showStatus("wifiStatus", "正在重启...", false);
    authFetch("/api/reboot", { method: "POST" })
        .then(function () {})
        .catch(function () {});
}

export function scanWifi() {
    const btn = document.getElementById("scanWifiBtn");
    const sel = document.getElementById("wifiSsidSelect");
    btn.disabled = true;
    btn.textContent = "扫描中...";

    authFetch("/api/wifi_scan")
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.json();
        })
        .then(function (list) {
            console.log("Scan result:", list);
            sel.innerHTML = '<option value="">-- 请选择 --</option>';
            list.forEach(function (ap) {
                const opt = document.createElement("option");
                opt.value = ap.ssid;
                opt.textContent = ap.ssid + " (" + ap.rssi + "dBm)";
                sel.appendChild(opt);
            });
            if (list.length === 0) {
                sel.innerHTML = '<option value="">未发现 WiFi</option>';
            }
        })
        .catch(function (err) {
            console.error("Scan failed:", err);
            sel.innerHTML = '<option value="">扫描失败</option>';
        })
        .finally(function () {
            btn.disabled = false;
            btn.textContent = "扫描";
        });
}

export function onSsidSelectChange() {
    const sel = document.getElementById("wifiSsidSelect");
    const inp = document.getElementById("wifiSsid");
    if (sel.value) {
        inp.value = sel.value;
    }
}

export function loadMacAddress() {
    authFetch("/api/mac")
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.json();
        })
        .then(function (data) {
            console.log("MAC:", data.mac);
            document.getElementById("macAddr").textContent = data.mac;
        })
        .catch(function (err) {
            console.warn("Failed to load MAC:", err);
            document.getElementById("macAddr").textContent = "(不可用)";
        });
}