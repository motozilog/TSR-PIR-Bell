import { switchTab, playFile, authFetch, showStatus } from "./common.js";
import { buildDoorbellUI, saveDoorbellConfig, loadDoorbellConfig, refreshDoorbellStatus } from "./doorbell.js";
import {
    buildAudioUI, buildAlertAudioUI, saveAudioConfig, loadAudioConfig, refreshAudioList,
    refreshAlertAudioList, uploadPcm, uploadBootMp3, loadBootAudioConfig, saveBootAudioConfig
} from "./audio.js";
import {
    loadWifiConfig, saveWifiConfig, updateModeUI, updateIpModeUI,
    scanWifi, onSsidSelectChange, loadDeviceInfo,
    loadVolume, updateVolumeDisplay, testVolume,
    updateLedBrightnessDisplay
} from "./wifi.js";

/* ===== 密码合法性校验（前端）：1~16，可打印 ASCII 不含空格 ===== */
function isValidPassword(pw) {
    if (typeof pw !== "string") return false;
    if (pw.length < 1 || pw.length > 16) return false;
    for (let i = 0; i < pw.length; i++) {
        const c = pw.charCodeAt(i);
        if (c < 0x21 || c > 0x7E) return false;
    }
    return true;
}

/* ===== 保存密码 ===== */
function savePassword() {
    const pw  = document.getElementById("newPassword").value;
    const pw2 = document.getElementById("confirmPassword").value;

    if (!isValidPassword(pw)) {
        showStatus("passwordStatus", "密码需为1~16位英文/数字/符号，不含空格", true);
        return;
    }
    if (pw !== pw2) {
        showStatus("passwordStatus", "两次输入不一致", true);
        return;
    }

    authFetch("/api/password", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ password: pw })
    })
        .then(function (resp) {
            return resp.json().then(function (data) {
                return { ok: resp.ok, data: data };
            });
        })
        .then(function (r) {
            if (r.ok && r.data && r.data.ok) {
                showStatus("passwordStatus", "密码已保存", false);
                document.getElementById("newPassword").value = "";
                document.getElementById("confirmPassword").value = "";
            } else {
                const msg = (r.data && r.data.msg) ? r.data.msg : "保存失败";
                showStatus("passwordStatus", msg, true);
            }
        })
        .catch(function (err) {
            showStatus("passwordStatus", "保存失败：" + err.message, true);
        });
}

/* ===== WiFi 密码框显示/隐藏（内嵌 SVG） ===== */
function bindWifiPassToggle() {
    const input = document.getElementById("wifiPass");
    const btn   = document.getElementById("wifiPassToggle");
    const iconShow = document.getElementById("eyeIconShow"); // 睁眼
    const iconHide = document.getElementById("eyeIconHide"); // 闭眼
    if (!input || !btn || !iconShow || !iconHide) return;

    // 增加显式状态变量，不依赖 input.type 推断
    let isPasswordVisible = false;

    // 更新 UI 状态
    function updateUI() {
        if (input.value.length === 0) {
            btn.style.display = "none"; // 无输入时隐藏按钮
            isPasswordVisible = false;  // 重置状态
            input.type = "password";
            return;
        }
        
        btn.style.display = "flex";     // 有输入时显示按钮
        
        if (isPasswordVisible) {
            input.type = "text";
            iconShow.style.display = "none";
            iconHide.style.display = "block";
        } else {
            input.type = "password";
            iconShow.style.display = "block";
            iconHide.style.display = "none";
        }

        // 排查日志
        const activeSvgId = isPasswordVisible ? "eyeIconHide (斜线眼)" : "eyeIconShow (睁眼)";
        console.log(`[Password Toggle] 状态: isPasswordVisible=${isPasswordVisible} | input.type=${input.type} | 当前激活: ${activeSvgId}`);
    }

    // 初始化和绑定输入事件
    updateUI();
    input.addEventListener("input", updateUI);

    // 绑定点击事件
    btn.addEventListener("click", function () {
        if (input.value.length === 0) return; // 理论上不会点，防御
        
        isPasswordVisible = !isPasswordVisible;
        updateUI();
        input.focus();
    });
}

document.addEventListener("DOMContentLoaded", function () {
    buildDoorbellUI();
    buildAudioUI();
    buildAlertAudioUI();

    document.querySelectorAll(".tab-btn").forEach(function (btn) {
        btn.addEventListener("click", function () {
            switchTab(btn.dataset.tab);
        });
    });

    document.querySelectorAll('input[name="wifiModeRadio"]').forEach(function (radio) {
        radio.addEventListener("change", function () {
            updateModeUI();
            if (radio.value === "station" && radio.checked) {
                console.log("Switched to Station, auto-scanning...");
                scanWifi();
            }
        });
    });

    document.getElementById("saveWifiBtn").addEventListener("click", saveWifiConfig);
    document.getElementById("saveDoorbellBtn").addEventListener("click", saveDoorbellConfig);
    document.getElementById("saveAudioBtn").addEventListener("click", saveAudioConfig);
    document.getElementById("savePasswordBtn").addEventListener("click", savePassword);

    document.getElementById("scanWifiBtn").addEventListener("click", scanWifi);

    document.getElementById("wifiSsidSelect").addEventListener("mousedown", function () {
        const sel = document.getElementById("wifiSsidSelect");
        if (sel.options.length <= 1) {
            scanWifi();
        }
    });
    document.getElementById("wifiSsidSelect").addEventListener("change", onSsidSelectChange);

    document.getElementById("pcmFileInput").addEventListener("change", uploadPcm);

    document.getElementById("playDefaultBtn").addEventListener("click", function () {
        playFile("default.mp3");
    });

    document.getElementById("uploadBootMp3Btn").addEventListener("click", function () {
        document.getElementById("bootMp3Input").click();
    });
    document.getElementById("bootMp3Input").addEventListener("change", uploadBootMp3);

    document.getElementById("bootPlayCheck").addEventListener("change", saveBootAudioConfig);

    document.getElementById("ipMode").addEventListener("change", updateIpModeUI);

    /* 绑定 WiFi 密码框显隐按钮 */
    bindWifiPassToggle();

    const volSlider = document.getElementById("volumeSlider");
    if (volSlider) {
        volSlider.addEventListener("input", updateVolumeDisplay);
        volSlider.addEventListener("change", updateVolumeDisplay);
    }
    const volBtn = document.getElementById("volumeTestBtn");
    if (volBtn) {
        volBtn.addEventListener("click", testVolume);
    }

    const ledBriSlider = document.getElementById("ledBrightnessSlider");
    if (ledBriSlider) {
        ledBriSlider.addEventListener("input", updateLedBrightnessDisplay);
        ledBriSlider.addEventListener("change", updateLedBrightnessDisplay);
    }

    loadWifiConfig();
    loadDeviceInfo();
    loadVolume();
    loadAudioConfig().then(function () {
        loadDoorbellConfig();
    });
    loadBootAudioConfig();

    refreshDoorbellStatus();
    setInterval(refreshDoorbellStatus, 10000);
});

/* ===== 极客调试：F12 控制台输入 AT('报文HEX') ===== */
window.AT = function (hex) {
    if (typeof hex !== "string" || hex.length === 0) {
        console.warn("用法: AT('00015A0CE4')");
        return;
    }
    authFetch("/api/test_chirp", {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: JSON.stringify({ hex: hex })
    })
    .then(function (resp) {
        if (!resp.ok) throw new Error("HTTP " + resp.status);
        return resp.json();
    })
    .then(function (data) {
        if (data.ok) {
            console.log("%c AT 注入成功: " + hex, "color: #4CAF50; font-weight: bold;");
        } else {
            console.warn("AT 注入失败", data);
        }
    })
    .catch(function (err) {
        console.error("AT 注入请求错误:", err);
    });
};

console.log(
    "%c🛠️ 调试模式已开启\n" +
    "请输入: AT('报文HEX') 来模拟接收。\n" +
    "示例:\n" +
    "  AT('00015A0FA0')  // 外机1电量 4000mV (正常)\n" +
    "  AT('00015A0CE4')  // 外机1电量 3300mV (低电量)\n" +
    "  AT('00015300000000')  // 外机1充满电\n" +
    "  AT('00015112345678')  // 外机1按键触发\n",
    "color: #2196F3; font-size: 14px; font-weight: bold;"
);