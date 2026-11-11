/* 公共常量与工具函数 */

export const AUDIO_COUNT = 10;
export const DOORBELL_COUNT = 4;
export const DEFAULT_NTP_SERVER = "ntp.aliyun.com";
export const AUDIO_SILENT = 0xFF;

export const AUDIO_FILES = {};
for (let i = 1; i <= AUDIO_COUNT; i++) {
    AUDIO_FILES[i] = "audio" + String(i).padStart(2, "0") + ".mp3";
}

/* 低电量/离线/满电提醒音频：固定按外机号 1~4 */
export const ALERT_COUNT = 4;

export const LOWPWR_FILES  = {};
export const OFFLINE_FILES = {};
export const FULLPWR_FILES = {};
for (let i = 1; i <= ALERT_COUNT; i++) {
    LOWPWR_FILES[i]  = "lowpwr"  + String(i).padStart(2, "0") + ".mp3";
    OFFLINE_FILES[i] = "offline" + String(i).padStart(2, "0") + ".mp3";
    FULLPWR_FILES[i] = "fullpwr" + String(i).padStart(2, "0") + ".mp3";
}

/* 提醒音频标签 */
export function getAlertAudioLabel(kind, idx) {
    let prefix = "低电量";
    if (kind === "offline") prefix = "离线";
    else if (kind === "fullpwr") prefix = "满电";
    return prefix + String(idx).padStart(2, "0");
}

/* ===== Tab 切换 ===== */
export function switchTab(tabId) {
    document.querySelectorAll(".tab-btn").forEach(function (btn) {
        btn.classList.toggle("active", btn.dataset.tab === tabId);
    });
    document.querySelectorAll(".tab-content").forEach(function (c) {
        c.classList.toggle("active", c.id === tabId);
    });
}

/* ===== 统一 fetch：同源自动带 Cookie；401 跳登录页 ===== */
export function authFetch(url, opts) {
    opts = opts || {};
    /* 同源请求默认 credentials: same-origin 会带 Cookie，无需显式设置 */
    return fetch(url, opts).then(function (resp) {
        if (resp.status === 401) {
            location.replace("login.html");
            throw new Error("unauthorized");
        }
        return resp;
    });
}

/* ===== 音频播放 ===== */
export function playFile(filename) {
    console.log("Requesting play:", filename);
    authFetch("/api/play?file=" + encodeURIComponent(filename), { method: "POST" })
        .then(function (resp) {
            if (!resp.ok) throw new Error("HTTP " + resp.status);
            return resp.text();
        })
        .then(function (text) {
            console.log("Play response:", text);
        })
        .catch(function (err) {
            console.error("Play failed:", err);
        });
}

/* 默认音频备注：配置为空时生效 */
export const DEFAULT_AUDIO_NOTES = {
    1:  "普通话",
    2:  "粤语",
    3:  "英语",
    4:  "日语",
    5:  "俄语",
    6:  "告警",
    7:  "提醒",
    8:  "门铃",
    9:  "下班",
    10: "嘉立创",
};

/* ===== 音频标签：备注优先 ===== */
export function getAudioLabel(idx, audioNotes) {
    if (idx === AUDIO_SILENT) {
        return "无声";
    }
    if (audioNotes && audioNotes[idx] && audioNotes[idx].trim() !== "") {
        return audioNotes[idx];
    }
    if (DEFAULT_AUDIO_NOTES[idx]) {
        return DEFAULT_AUDIO_NOTES[idx];
    }
    return "音频" + String(idx).padStart(2, "0");
}

/* ===== 通用状态显示 ===== */
export function showStatus(elId, msg, isError) {
    const el = document.getElementById(elId);
    if (!el) return;
    el.textContent = msg;
    el.style.color = isError ? "red" : "green";
}

/* ===== IP 校验 ===== */
export function isValidIp(str) {
    if (!str) return false;
    const re = /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/;
    return re.test(str);
}

export function isValidNetmask(str) {
    if (!isValidIp(str)) return false;
    const parts = str.split(".").map(Number);
    const num = ((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]) >>> 0;
    if (num === 0) return false;
    const inv = (~num) >>> 0;
    return ((inv + 1) & inv) === 0;
}