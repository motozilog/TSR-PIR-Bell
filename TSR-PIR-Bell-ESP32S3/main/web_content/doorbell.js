import { AUDIO_COUNT, AUDIO_SILENT, DOORBELL_COUNT, AUDIO_FILES, playFile, getAudioLabel, showStatus, authFetch } from "./common.js";
import { audioNotes } from "./audio.js";

/* ===== 更新下拉框状态：仅当前选中的模式可用 ===== */
function updateSelectStates(idx) {
    const radios = document.querySelectorAll('input[name="doorbell_mode_' + idx + '"]');
    let selectedMode = "welcome";
    radios.forEach(function (r) {
        if (r.checked) selectedMode = r.value;
    });

    ["welcome", "doorbell", "alarm"].forEach(function (m) {
        const irSel  = document.getElementById("doorbell_" + m + "_ir_"  + idx);
        const keySel = document.getElementById("doorbell_" + m + "_key_" + idx);
        if (irSel && keySel) {
            const isDisabled = (m !== selectedMode);
            irSel.disabled = isDisabled;
            keySel.disabled = isDisabled;
        }
    });
}

/* ===== 外机设置 UI 生成 ===== */
export function buildDoorbellUI() {
    const container = document.getElementById("doorbellList");
    container.innerHTML = "";

    const modeDefs = [
        { value: "welcome",  label: "迎宾", defIr: 1, defKey: 7 },
        { value: "doorbell", label: "门铃", defIr: 1, defKey: 1 },
        { value: "alarm",    label: "警报", defIr: 6, defKey: 8 },
    ];

    for (let i = 1; i <= DOORBELL_COUNT; i++) {
        const block = document.createElement("div");
        block.className = "doorbell-block";

        const header = document.createElement("div");
        header.className = "doorbell-header";

        const lbl = document.createElement("span");
        lbl.className = "doorbell-label";
        lbl.textContent = "外机" + i;
        header.appendChild(lbl);

        const enableLabel = document.createElement("label");
        enableLabel.className = "switch";
        const enableInput = document.createElement("input");
        enableInput.type = "checkbox";
        enableInput.className = "doorbell-enable";
        enableInput.dataset.idx = i;
        enableInput.checked = false;
        const enableSlider = document.createElement("span");
        enableSlider.className = "switch-slider";
        enableLabel.appendChild(enableInput);
        enableLabel.appendChild(enableSlider);
        header.appendChild(enableLabel);

        const testIr = document.createElement("button");
        testIr.className = "test-btn";
        testIr.textContent = "测试红外";
        testIr.addEventListener("click", function () {
            const mode = getSelectedMode(i);
            const sel = document.getElementById("doorbell_" + mode + "_ir_" + i);
            const idx = sel ? parseInt(sel.value) : 1;
            if (idx === AUDIO_SILENT) {
                console.log("Test IR for 外机" + i + ", mode=" + mode + ", audio=silent, no play");
                return;
            }
            console.log("Test IR for 外机" + i + ", mode=" + mode + ", audio=" + idx);
            playFile(AUDIO_FILES[idx]);
        });
        header.appendChild(testIr);

        const testKey = document.createElement("button");
        testKey.className = "test-btn";
        testKey.textContent = "测试按键";
        testKey.addEventListener("click", function () {
            const mode = getSelectedMode(i);
            const sel = document.getElementById("doorbell_" + mode + "_key_" + i);
            const idx = sel ? parseInt(sel.value) : 1;
            if (idx === AUDIO_SILENT) {
                console.log("Test KEY for 外机" + i + ", mode=" + mode + ", audio=silent, no play");
                return;
            }
            console.log("Test KEY for 外机" + i + ", mode=" + mode + ", audio=" + idx);
            playFile(AUDIO_FILES[idx]);
        });
        header.appendChild(testKey);

        /* 低电量提醒开关 */
        const lowpwrLabel = document.createElement("label");
        lowpwrLabel.className = "alert-switch-label";
        lowpwrLabel.appendChild(document.createTextNode("低电量"));
        const lowpwrSwitch = document.createElement("label");
        lowpwrSwitch.className = "switch";
        const lowpwrInput = document.createElement("input");
        lowpwrInput.type = "checkbox";
        lowpwrInput.className = "doorbell-lowpwr-enable";
        lowpwrInput.dataset.idx = i;
        lowpwrInput.checked = true;
        const lowpwrSlider = document.createElement("span");
        lowpwrSlider.className = "switch-slider";
        lowpwrSwitch.appendChild(lowpwrInput);
        lowpwrSwitch.appendChild(lowpwrSlider);
        lowpwrLabel.appendChild(lowpwrSwitch);
        header.appendChild(lowpwrLabel);

        /* 离线提醒开关 */
        const offlineLabel = document.createElement("label");
        offlineLabel.className = "alert-switch-label";
        offlineLabel.appendChild(document.createTextNode("离线"));
        const offlineSwitch = document.createElement("label");
        offlineSwitch.className = "switch";
        const offlineInput = document.createElement("input");
        offlineInput.type = "checkbox";
        offlineInput.className = "doorbell-offline-enable";
        offlineInput.dataset.idx = i;
        offlineInput.checked = true;
        const offlineSlider = document.createElement("span");
        offlineSlider.className = "switch-slider";
        offlineSwitch.appendChild(offlineInput);
        offlineSwitch.appendChild(offlineSlider);
        offlineLabel.appendChild(offlineSwitch);
        header.appendChild(offlineLabel);

        /* 满电提醒开关 */
        const fullpwrLabel = document.createElement("label");
        fullpwrLabel.className = "alert-switch-label";
        fullpwrLabel.appendChild(document.createTextNode("满电"));
        const fullpwrSwitch = document.createElement("label");
        fullpwrSwitch.className = "switch";
        const fullpwrInput = document.createElement("input");
        fullpwrInput.type = "checkbox";
        fullpwrInput.className = "doorbell-fullpwr-enable";
        fullpwrInput.dataset.idx = i;
        fullpwrInput.checked = true;
        const fullpwrSlider = document.createElement("span");
        fullpwrSlider.className = "switch-slider";
        fullpwrSwitch.appendChild(fullpwrInput);
        fullpwrSwitch.appendChild(fullpwrSlider);
        fullpwrLabel.appendChild(fullpwrSwitch);
        header.appendChild(fullpwrLabel);

        block.appendChild(header);

        modeDefs.forEach(function (m, idx) {
            const modeRow = document.createElement("div");
            modeRow.className = "doorbell-mode-row";

            const modeLabel = document.createElement("label");
            modeLabel.className = "mode-radio-label";
            const radio = document.createElement("input");
            radio.type = "radio";
            radio.name = "doorbell_mode_" + i;
            radio.value = m.value;
            if (idx === 0) {
                radio.checked = true;
                modeRow.classList.add("selected");
            }

            radio.addEventListener("change", function () {
                block.querySelectorAll(".doorbell-mode-row").forEach(function (r) {
                    r.classList.remove("selected");
                });
                if (radio.checked) {
                    modeRow.classList.add("selected");
                    updateSelectStates(i); /* 切换模式时更新下拉框禁用状态 */
                }
            });

            modeLabel.appendChild(radio);
            modeLabel.appendChild(document.createTextNode(m.label));
            modeRow.appendChild(modeLabel);

            const irGroup = document.createElement("div");
            irGroup.className = "audio-select-group";
            irGroup.appendChild(document.createTextNode("红外:"));
            const irSel = document.createElement("select");
            irSel.id = "doorbell_" + m.value + "_ir_" + i;
            for (let j = 1; j <= AUDIO_COUNT; j++) {
                const opt = document.createElement("option");
                opt.value = j;
                opt.textContent = getAudioLabel(j, audioNotes);
                irSel.appendChild(opt);
            }
            const silentOptIr = document.createElement("option");
            silentOptIr.value = AUDIO_SILENT;
            silentOptIr.textContent = "无声";
            irSel.appendChild(silentOptIr);
            irSel.value = m.defIr;
            irGroup.appendChild(irSel);
            modeRow.appendChild(irGroup);

            const keyGroup = document.createElement("div");
            keyGroup.className = "audio-select-group";
            keyGroup.appendChild(document.createTextNode("按键:"));
            const keySel = document.createElement("select");
            keySel.id = "doorbell_" + m.value + "_key_" + i;
            for (let j = 1; j <= AUDIO_COUNT; j++) {
                const opt = document.createElement("option");
                opt.value = j;
                opt.textContent = getAudioLabel(j, audioNotes);
                keySel.appendChild(opt);
            }
            const silentOptKey = document.createElement("option");
            silentOptKey.value = AUDIO_SILENT;
            silentOptKey.textContent = "无声";
            keySel.appendChild(silentOptKey);
            keySel.value = m.defKey;
            keyGroup.appendChild(keySel);
            modeRow.appendChild(keyGroup);

            block.appendChild(modeRow);
        });

        container.appendChild(block);
        
        /* 初始化禁用状态 */
        updateSelectStates(i);
    }
}

/* ===== 获取某外机当前选中的模式 ===== */
export function getSelectedMode(doorbellIdx) {
    const radios = document.querySelectorAll('input[name="doorbell_mode_' + doorbellIdx + '"]');
    let mode = "welcome";
    radios.forEach(function (r) {
        if (r.checked) mode = r.value;
    });
    return mode;
}

/* ===== 外机配置：保存/加载 ===== */
export function collectDoorbellConfig() {
    const result = { doorbells: {} };
    for (let i = 1; i <= DOORBELL_COUNT; i++) {
        const db = {};
        db.mode = getSelectedMode(i);

        const enableEl = document.querySelector('.doorbell-enable[data-idx="' + i + '"]');
        db.enabled = (enableEl && enableEl.checked) ? 1 : 0;

        const lowpwrEl  = document.querySelector('.doorbell-lowpwr-enable[data-idx="' + i + '"]');
        const offlineEl = document.querySelector('.doorbell-offline-enable[data-idx="' + i + '"]');
        const fullpwrEl = document.querySelector('.doorbell-fullpwr-enable[data-idx="' + i + '"]');
        db.lowpwr_enabled  = (lowpwrEl  && lowpwrEl.checked)  ? 1 : 0;
        db.offline_enabled = (offlineEl && offlineEl.checked) ? 1 : 0;
        db.fullpwr_enabled = (fullpwrEl && fullpwrEl.checked) ? 1 : 0;

        ["welcome", "doorbell", "alarm"].forEach(function (m) {
            const irSel  = document.getElementById("doorbell_" + m + "_ir_"  + i);
            const keySel = document.getElementById("doorbell_" + m + "_key_" + i);
            db[m + "_ir"]  = irSel  ? (parseInt(irSel.value)  || 0) : 1;
            db[m + "_key"] = keySel ? (parseInt(keySel.value) || 0) : 1;
        });

        result.doorbells[i] = db;
    }
    return result;
}

export function saveDoorbellConfig() {
    const cfg = collectDoorbellConfig();
    console.log("Saving doorbell config:", cfg);

    authFetch("/api/doorbell_config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(cfg)
    })
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.text();
        })
        .then(function () {
            showStatus("doorbellStatus", "外机配置已保存", false);
            refreshDoorbellStatus();
        })
        .catch(function (err) {
            console.error("Save doorbell config failed:", err);
            showStatus("doorbellStatus", "保存失败：" + err.message, true);
        });
}

export function loadDoorbellConfig() {
    return authFetch("/api/doorbell_config")
        .then(function (resp) { return resp.json(); })
        .then(function (cfg) {
            console.log("Loaded doorbell config:", cfg);
            if (!cfg.doorbells) return;
            for (let i = 1; i <= DOORBELL_COUNT; i++) {
                const db = cfg.doorbells[i];
                if (!db) continue;

                const enableEl = document.querySelector('.doorbell-enable[data-idx="' + i + '"]');
                if (enableEl) enableEl.checked = (db.enabled === 1);

                const lowpwrEl  = document.querySelector('.doorbell-lowpwr-enable[data-idx="' + i + '"]');
                const offlineEl = document.querySelector('.doorbell-offline-enable[data-idx="' + i + '"]');
                const fullpwrEl = document.querySelector('.doorbell-fullpwr-enable[data-idx="' + i + '"]');
                if (lowpwrEl)  lowpwrEl.checked  = (db.lowpwr_enabled  === 1);
                if (offlineEl) offlineEl.checked = (db.offline_enabled === 1);
                if (fullpwrEl) fullpwrEl.checked = (db.fullpwr_enabled === 1);

                if (db.mode) {
                    const radio = document.querySelector(
                        'input[name="doorbell_mode_' + i + '"][value="' + db.mode + '"]');
                    if (radio) {
                        radio.checked = true;
                        radio.dispatchEvent(new Event("change"));
                    }
                }

                ["welcome", "doorbell", "alarm"].forEach(function (m) {
                    const irSel  = document.getElementById("doorbell_" + m + "_ir_"  + i);
                    const keySel = document.getElementById("doorbell_" + m + "_key_" + i);
                    const irVal  = db[m + "_ir"];
                    const keyVal = db[m + "_key"];
                    if (irSel  && irVal  !== undefined) irSel.value  = irVal;
                    if (keySel && keyVal !== undefined) keySel.value = keyVal;
                });

                updateSelectStates(i); /* 加载后更新禁用状态 */
            }
            refreshDoorbellStatus();
        })
        .catch(function (err) {
            console.warn("Load doorbell config failed:", err);
        });
}

/* ===== 外机状态刷新 ===== */
const DB_STATE_TEXT = {
    online:     "在线",
    disabled:   "未启用",
    low_battery:"低电量",
    offline:    "离线"
};
const DB_STATE_CLASS = {
    online:     "db-status-online",
    disabled:   "db-status-disabled",
    low_battery:"db-status-lowbatt",
    offline:    "db-status-offline"
};

export function refreshDoorbellStatus() {
    return authFetch("/api/doorbell_status")
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.json();
        })
        .then(function (data) {
            const row = document.getElementById("doorbellStatusRow");
            if (!row) return;
            const list = data.status || [];
            row.innerHTML = "";

            list.forEach(function (item) {
                const idx = item.idx;
                const state = item.state || "offline";

                const wrap = document.createElement("span");
                wrap.className = "db-status-item";
                wrap.appendChild(document.createTextNode("外机" + idx));

                const tag = document.createElement("span");
                tag.className = "db-status " + (DB_STATE_CLASS[state] || "db-status-offline");
                tag.textContent = DB_STATE_TEXT[state] || "离线";

                if (state === "offline") {
                    tag.classList.add("clickable");
                    tag.title = "点击查询电量";
                    tag.addEventListener("click", function () {
                        queryDoorbellBattery(idx);
                    });
                }
                wrap.appendChild(tag);
                row.appendChild(wrap);
            });
        })
        .catch(function (err) {
            console.warn("Load doorbell status failed:", err);
        });
}

export function queryDoorbellBattery(idx) {
    console.log("Query battery for 外机" + idx);
    authFetch("/api/doorbell_query_battery?idx=" + idx, { method: "POST" })
        .then(function (resp) { return resp.json(); })
        .then(function (data) {
            console.log("Query battery response:", data);
        })
        .catch(function (err) {
            console.warn("Query battery failed:", err);
        });
}