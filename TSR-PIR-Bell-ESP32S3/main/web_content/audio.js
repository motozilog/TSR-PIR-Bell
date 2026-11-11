import { AUDIO_COUNT, AUDIO_FILES, DOORBELL_COUNT, ALERT_COUNT,
         LOWPWR_FILES, OFFLINE_FILES, FULLPWR_FILES, getAlertAudioLabel,
         playFile, getAudioLabel, showStatus, authFetch, DEFAULT_AUDIO_NOTES } from "./common.js";

/* 音频备注与文件存在状态，供 doorbell.js 读取 */
export let audioNotes = {};
export let audioExists = {};

/* 提醒音频存在状态 */
export let lowpwrExists  = {};
export let offlineExists = {};
export let fullpwrExists = {};

let currentUploadTarget = null;

/* ===== 刷新门铃下拉框的选项文本 ===== */
export function refreshDoorbellAudioOptions() {
    for (let i = 1; i <= DOORBELL_COUNT; i++) {
        ["welcome", "doorbell", "alarm"].forEach(function (m) {
            const irSel = document.getElementById("doorbell_" + m + "_ir_" + i);
            const keySel = document.getElementById("doorbell_" + m + "_key_" + i);
            if (!irSel || !keySel) return;

            const irVal = irSel.value;
            const keyVal = keySel.value;

            irSel.innerHTML = "";
            keySel.innerHTML = "";
            for (let j = 1; j <= AUDIO_COUNT; j++) {
                const label = getAudioLabel(j, audioNotes);

                const opt1 = document.createElement("option");
                opt1.value = j;
                opt1.textContent = label;
                irSel.appendChild(opt1);

                const opt2 = document.createElement("option");
                opt2.value = j;
                opt2.textContent = label;
                keySel.appendChild(opt2);
            }
            /* 最后追加"无声"选项 */
            const silentIr = document.createElement("option");
            silentIr.value = 0xFF;
            silentIr.textContent = "无声";
            irSel.appendChild(silentIr);

            const silentKey = document.createElement("option");
            silentKey.value = 0xFF;
            silentKey.textContent = "无声";
            keySel.appendChild(silentKey);

            if (irVal) irSel.value = irVal;
            if (keyVal) keySel.value = keyVal;
        });
    }
}

/* ===== 音频设置 UI 生成 ===== */
export function buildAudioUI() {
    const container = document.getElementById("audioList");
    container.innerHTML = "";

    for (let i = 1; i <= AUDIO_COUNT; i++) {
        const row = document.createElement("div");
        row.className = "audio-row";

        const playBtn = document.createElement("button");
        playBtn.className = "play-btn missing";
        playBtn.id = "audio_play_" + i;
        playBtn.textContent = "音频" + String(i).padStart(2, "0");
        playBtn.disabled = true;
        playBtn.addEventListener("click", function () {
            if (!audioExists[i]) return;
            playFile(AUDIO_FILES[i]);
        });
        row.appendChild(playBtn);

        const upBtn = document.createElement("button");
        upBtn.className = "audio-action-btn";
        upBtn.id = "audio_act_" + i;
        upBtn.textContent = "上传";
        upBtn.dataset.target = AUDIO_FILES[i];
        upBtn.addEventListener("click", function () {
            currentUploadTarget = AUDIO_FILES[i];
            document.getElementById("pcmFileInput").click();
        });
        row.appendChild(upBtn);

        const note = document.createElement("input");
        note.type = "text";
        note.className = "note-input";
        note.id = "audio_note_" + i;
        note.placeholder = DEFAULT_AUDIO_NOTES[i] || "备注";
        note.value = audioNotes[i] || "";
        row.appendChild(note);

        container.appendChild(row);
    }

    refreshAudioList();
}

/* ===== 提醒音频 UI 生成（无备注） ===== */
function buildAlertRows(containerId, files, existsMap, kind) {
    const container = document.getElementById(containerId);
    if (!container) return;
    container.innerHTML = "";

    for (let i = 1; i <= ALERT_COUNT; i++) {
        const row = document.createElement("div");
        row.className = "audio-row";

        const playBtn = document.createElement("button");
        playBtn.className = "play-btn missing";
        playBtn.id = kind + "_play_" + i;
        playBtn.textContent = getAlertAudioLabel(kind, i);
        playBtn.disabled = true;
        playBtn.addEventListener("click", function () {
            if (!existsMap[i]) return;
            playFile(files[i]);
        });
        row.appendChild(playBtn);

        const upBtn = document.createElement("button");
        upBtn.className = "audio-action-btn";
        upBtn.id = kind + "_act_" + i;
        upBtn.textContent = "上传";
        upBtn.dataset.target = files[i];
        upBtn.addEventListener("click", function () {
            currentUploadTarget = files[i];
            document.getElementById("pcmFileInput").click();
        });
        row.appendChild(upBtn);

        container.appendChild(row);
    }
}

export function buildAlertAudioUI() {
    buildAlertRows("lowpwrList",  LOWPWR_FILES,  lowpwrExists,  "lowpwr");
    buildAlertRows("offlineList", OFFLINE_FILES, offlineExists, "offline");
    buildAlertRows("fullpwrList", FULLPWR_FILES, fullpwrExists, "fullpwr");
    refreshAlertAudioList();
}

/* ===== 刷新音频文件存在状态 ===== */
export function refreshAudioList() {
    return authFetch("/api/audio_list")
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.json();
        })
        .then(function (data) {
            const arr = data.exists || [];
            for (let i = 1; i <= AUDIO_COUNT; i++) {
                audioExists[i] = (arr[i - 1] === 1);

                const playBtn = document.getElementById("audio_play_" + i);
                const actBtn  = document.getElementById("audio_act_" + i);
                if (!playBtn || !actBtn) continue;

                if (audioExists[i]) {
                    playBtn.classList.remove("missing");
                    playBtn.disabled = false;
                    actBtn.textContent = "更换";
                } else {
                    playBtn.classList.add("missing");
                    playBtn.disabled = true;
                    actBtn.textContent = "上传";
                }
            }
        })
        .catch(function (err) {
            console.warn("Load audio list failed:", err);
        });
}

/* ===== 刷新提醒音频存在状态 ===== */
export function refreshAlertAudioList() {
    return authFetch("/api/alert_audio_list")
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.json();
        })
        .then(function (data) {
            const lp = data.lowpwr  || [];
            const of = data.offline || [];
            const fp = data.fullpwr || [];
            for (let i = 1; i <= ALERT_COUNT; i++) {
                lowpwrExists[i]  = (lp[i - 1] === 1);
                offlineExists[i] = (of[i - 1] === 1);
                fullpwrExists[i] = (fp[i - 1] === 1);

                ["lowpwr", "offline", "fullpwr"].forEach(function (kind) {
                    let exists = lowpwrExists[i];
                    if (kind === "offline") exists = offlineExists[i];
                    else if (kind === "fullpwr") exists = fullpwrExists[i];

                    const playBtn = document.getElementById(kind + "_play_" + i);
                    const actBtn  = document.getElementById(kind + "_act_" + i);
                    if (!playBtn || !actBtn) return;
                    if (exists) {
                        playBtn.classList.remove("missing");
                        playBtn.disabled = false;
                        actBtn.textContent = "更换";
                    } else {
                        playBtn.classList.add("missing");
                        playBtn.disabled = true;
                        actBtn.textContent = "上传";
                    }
                });
            }
        })
        .catch(function (err) {
            console.warn("Load alert audio list failed:", err);
        });
}

/* ===== 音频备注：保存/加载 ===== */
export function loadAudioConfig() {
    return authFetch("/api/audio_config")
        .then(function (resp) { return resp.json(); })
        .then(function (cfg) {
            console.log("Loaded audio config:", cfg);
            audioNotes = cfg.notes || {};
            for (let i = 1; i <= AUDIO_COUNT; i++) {
                const el = document.getElementById("audio_note_" + i);
                if (el && audioNotes[i]) {
                    el.value = audioNotes[i];
                }
            }
            refreshDoorbellAudioOptions();
        })
        .catch(function (err) {
            console.warn("Load audio config failed:", err);
        });
}

export function saveAudioConfig() {
    const notes = {};
    for (let i = 1; i <= AUDIO_COUNT; i++) {
        const el = document.getElementById("audio_note_" + i);
        if (el && el.value.trim() !== "") {
            notes[i] = el.value.trim();
        }
    }

    const cfg = { notes: notes };
    console.log("Saving audio config:", cfg);

    authFetch("/api/audio_config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(cfg)
    })
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.text();
        })
        .then(function () {
            audioNotes = notes;
            showStatus("uploadStatus", "音频备注已保存", false);
            refreshDoorbellAudioOptions();
        })
        .catch(function (err) {
            console.error("Save audio config failed:", err);
            showStatus("uploadStatus", "保存失败：" + err.message, true);
        });
}

/* ===== 音频上传 ===== */
export function uploadPcm() {
    const fileInput = document.getElementById("pcmFileInput");
    const file = fileInput.files[0];
    if (!file) {
        showStatus("uploadStatus", "未选择文件", true);
        return;
    }

    const target = currentUploadTarget;
    if (!target) {
        showStatus("uploadStatus", "未指定目标文件", true);
        return;
    }

    console.log("Uploading", file.name, "as", target, "size:", file.size);
    const MAX_SIZE = 200 * 1024;
    if (file.size > MAX_SIZE) {
        showStatus("uploadStatus", "文件过大（" + (file.size / 1024).toFixed(1) + " KB），最大 200 KB", true);
        fileInput.value = "";
        currentUploadTarget = null;
        return;
    }
    if (!/\.mp3$/i.test(file.name)) {
        showStatus("uploadStatus", "只允许 .mp3 文件", true);
        fileInput.value = "";
        currentUploadTarget = null;
        return;
    }

    showStatus("uploadStatus", "上传并校验播放中：" + file.name + " → " + target, false);

    authFetch("/api/upload_pcm?name=" + encodeURIComponent(target), {
        method: "POST",
        headers: { "Content-Type": "application/octet-stream" },
        body: file
    })
        .then(function (resp) {
            return resp.json();
        })
        .then(function (data) {
            if (data.ok) {
                showStatus("uploadStatus", "上传成功：" + file.name + " → " + target, false);
                refreshAudioList();
                refreshAlertAudioList();
            } else {
                showStatus("uploadStatus", "上传失败：" + (data.msg || "未知错误"), true);
            }
        })
        .catch(function (err) {
            console.error("Upload failed:", err);
            showStatus("uploadStatus", "上传失败：" + err.message, true);
        })
        .finally(function () {
            fileInput.value = "";
            currentUploadTarget = null;
        });
}

/* ===== 开机音频 ===== */
export function uploadBootMp3() {
    const input = document.getElementById("bootMp3Input");
    const file = input.files[0];
    if (!file) return;

    const MAX = 200 * 1024;
    if (file.size > MAX) {
        showStatus("bootMp3Status", "文件过大（" + (file.size / 1024).toFixed(1) + " KB），最大 200 KB", true);
        input.value = "";
        return;
    }
    if (!/\.mp3$/i.test(file.name)) {
        showStatus("bootMp3Status", "只允许 .mp3 文件", true);
        input.value = "";
        return;
    }

    showStatus("bootMp3Status", "上传并校验播放中...", false);

    authFetch("/api/upload_default_mp3", {
        method: "POST",
        headers: { "Content-Type": "application/octet-stream" },
        body: file
    })
        .then(function (resp) {
            return resp.json();
        })
        .then(function (data) {
            if (data.played) {
                alert(data.ok ? "播放成功，已设为开机音频" : data.msg);
            } else {
                alert("播放失败：" + data.msg);
            }
            showStatus("bootMp3Status", data.msg, !data.ok);
        })
        .catch(function (err) {
            console.error("Upload boot mp3 failed:", err);
            showStatus("bootMp3Status", "上传失败：" + err.message, true);
        })
        .finally(function () {
            input.value = "";
        });
}

export function loadBootAudioConfig() {
    authFetch("/api/boot_audio")
        .then(function (resp) { return resp.json(); })
        .then(function (cfg) {
            const chk = document.getElementById("bootPlayCheck");
            if (chk) chk.checked = (cfg.play === 1);
        })
        .catch(function (err) { console.warn("Load boot audio cfg failed:", err); });
}

export function saveBootAudioConfig() {
    const chk = document.getElementById("bootPlayCheck");
    const play = chk && chk.checked ? 1 : 0;
    authFetch("/api/boot_audio", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ play: play })
    })
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            showStatus("bootMp3Status", play ? "已开启启动时播放" : "已关闭启动时播放", false);
        })
        .catch(function (err) {
            showStatus("bootMp3Status", "保存失败：" + err.message, true);
        });
}