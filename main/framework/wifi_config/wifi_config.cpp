/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "wifi_config.h"
#include <framework/wifi_manager/wifi_manager.h>
#include <mooncake_log.h>
#include <esp_http_server.h>
#include <esp_mac.h>
#include <cstring>
#include <string>

namespace framework::wifi_config {

namespace {

constexpr const char* _tag = "WifiConfig";

httpd_handle_t _server          = nullptr;
std::function<void(std::string_view)> _log_cb;
char _last_result[96]           = "等待配网…";
char _ap_ssid[32]               = "";
bool _connecting                = false;

void log(std::string_view msg)
{
    mclog::tagInfo(_tag, "{}", msg);
    if (_log_cb) {
        _log_cb(msg);
    }
}

/* ---------------- 配网页（内嵌） ---------------- */

const char* _index_html = R"HTML(<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5StopWatch WiFi 配置</title>
<style>
body{font-family:"Microsoft YaHei",sans-serif;background:#12181f;color:#e8eef4;
max-width:420px;margin:40px auto;padding:0 20px}
h2{color:#7ac4ff;text-align:center}
.card{background:#1c2733;border-radius:12px;padding:20px 24px;margin-top:20px}
label{display:block;margin:14px 0 6px;font-size:14px;color:#9aa5b5}
input{width:100%;box-sizing:border-box;padding:10px 12px;border-radius:8px;
border:1px solid #2c3e50;background:#0f151c;color:#fff;font-size:15px}
button{width:100%;margin-top:22px;padding:12px;border:none;border-radius:8px;
background:#2080c0;color:#fff;font-size:16px;cursor:pointer}
button:disabled{opacity:.6}
#st{margin-top:16px;text-align:center;font-size:15px;min-height:22px}
#hint{color:#6b7686;font-size:13px;text-align:center;margin-top:8px}
</style></head><body>
<h2>M5StopWatch 配网</h2>
<div class="card">
  <form id="f">
    <label>WiFi 名称 (SSID)</label>
    <input name="ssid" required autocomplete="off">
    <label>密码</label>
    <input type="password" name="password" autocomplete="off">
    <button id="btn" type="submit">保存并连接</button>
  </form>
  <div id="st">填写路由器 WiFi 后保存，手表将自动连接。</div>
</div>
<div id="hint">连接成功后手表会显示"WiFi 已连接"，可关闭此页面</div>
<script>
const st=document.getElementById('st'),btn=document.getElementById('btn');
f.onsubmit=async e=>{e.preventDefault();btn.disabled=true;
st.textContent='正在连接…请稍候（约 15 秒）';
const r=await fetch('/wifi/save',{method:'POST',
body:new URLSearchParams(new FormData(f))});
const j=await r.json();
if(j.ok){st.textContent='✅ '+j.msg;btn.disabled=false;return;}
st.textContent='⏳ 连接中，请等待结果…';
poll();};
async function poll(){const r=await fetch('/wifi/status');const j=await r.json();
if(j.connected){st.textContent='✅ 已连接: '+j.ssid;return;}
if(j.result&&j.result.includes('失败')){st.textContent='❌ '+j.result;btn.disabled=false;return;}
st.textContent=j.result||'连接中…';setTimeout(poll,2000);}
</script></body></html>)HTML";

/* ---------------- 请求处理 ---------------- */

esp_err_t handle_index(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, _index_html);
}

esp_err_t handle_status(httpd_req_t* req)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"connected\":%s,\"ssid\":\"%s\",\"result\":\"%s\"}",
             WifiManager::get().isConnected() ? "true" : "false",
             WifiManager::get().isConnected() ? "wifi" : "", _last_result);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_sendstr(req, buf);
}

struct ConnectJob {
    std::string ssid;
    std::string pwd;
};

void connect_task(void* arg)
{
    auto* job = static_cast<ConnectJob*>(arg);
    log("connecting to " + job->ssid);
    bool ok = WifiManager::get().connectSta(job->ssid.c_str(), job->pwd.c_str(), 15000);
    if (ok) {
        snprintf(_last_result, sizeof(_last_result), "已连接 %s", job->ssid.c_str());
        log("STA connected: " + job->ssid);
    } else {
        snprintf(_last_result, sizeof(_last_result), "连接失败，请检查密码");
        log("STA connect failed: " + job->ssid);
    }
    _connecting = false;
    delete job;
    vTaskDelete(nullptr);
}

esp_err_t handle_save(httpd_req_t* req)
{
    // 读 body（application/x-www-form-urlencoded，如 ssid=xxx&password=yyy）
    char buf[512] = {};
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"empty body\"}");
        return ESP_OK;
    }
    buf[len] = '\0';

    // URL 解码后的字段解析（ssid / password）
    auto url_decode = [](const char* s) {
        std::string out;
        for (const char* p = s; *p; ++p) {
            if (*p == '%' && p[1] && p[2]) {
                char hex[3] = {p[1], p[2], 0};
                out += static_cast<char>(strtol(hex, nullptr, 16));
                p += 2;
            } else if (*p == '+') {
                out += ' ';
            } else {
                out += *p;
            }
        }
        return out;
    };

    std::string ssid, pwd;
    char* saveptr = nullptr;
    for (char* tok = strtok_r(buf, "&", &saveptr); tok; tok = strtok_r(nullptr, "&", &saveptr)) {
        char* eq = strchr(tok, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        if (strcmp(tok, "ssid") == 0) {
            ssid = url_decode(eq + 1);
        } else if (strcmp(tok, "password") == 0) {
            pwd = url_decode(eq + 1);
        }
    }

    if (ssid.empty()) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"ssid 为空\"}");
        return ESP_OK;
    }
    if (_connecting) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"已在连接中，请稍候\"}");
        return ESP_OK;
    }

    _connecting = true;
    snprintf(_last_result, sizeof(_last_result), "连接中 %s…", ssid.c_str());
    auto* job = new ConnectJob{ssid, pwd};
    // 连接最长 15s，放独立任务避免阻塞 HTTP server。
    // 栈 8KB 内部 RAM：esp_wifi 调用链 + NVS 写凭据（cache 禁用期间 PSRAM 栈会崩）
    if (xTaskCreate(connect_task, "wifi_connect", 8192, job, 2, nullptr) != pdPASS) {
        _connecting = false;
        snprintf(_last_result, sizeof(_last_result), "连接任务创建失败");
        delete job;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"已开始连接\"}");
    return ESP_OK;
}

}  // namespace

/* ---------------- 对外接口 ---------------- */

// 生成热点名：M5StopWatch-XXXX（MAC 后两位）——同步执行（UI 需要显示）
void generate_ap_ssid()
{
    if (_ap_ssid[0] != '\0') {
        return;
    }
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(_ap_ssid, sizeof(_ap_ssid), "M5StopWatch-%02X%02X", mac[4], mac[5]);
}

void start_task(void* arg)
{
    (void)arg;

    mclog::tagInfo(_tag, "[dbg] start_task entered");

    generate_ap_ssid();

    // AP 启动在独立任务执行：esp_wifi_start 涉及 PHY 初始化可能耗时数百 ms，
    // 若在 LVGL 持锁上下文同步调用会卡死整个 UI
    if (!WifiManager::get().startAp(_ap_ssid, "")) {
        log("AP 启动失败");
        vTaskDelete(nullptr);
        return;
    }

    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers  = 8;
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 15;
    config.uri_match_fn      = httpd_uri_match_wildcard;

    if (httpd_start(&_server, &config) != ESP_OK) {
        log("HTTP server 启动失败");
        vTaskDelete(nullptr);
        return;
    }

    httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = &handle_index, .user_ctx = nullptr};
    httpd_uri_t save  = {.uri = "/wifi/save", .method = HTTP_POST, .handler = &handle_save, .user_ctx = nullptr};
    httpd_uri_t st    = {.uri = "/wifi/status", .method = HTTP_GET, .handler = &handle_status, .user_ctx = nullptr};
    httpd_register_uri_handler(_server, &index);
    httpd_register_uri_handler(_server, &save);
    httpd_register_uri_handler(_server, &st);

    log("配网热点已启动，手机连接后浏览器打开 http://192.168.4.1");
    vTaskDelete(nullptr);
}

void start(const std::function<void(std::string_view)>& onLog)
{
    _log_cb = onLog;
    generate_ap_ssid();  // 同步生成热点名（UI 立即显示）
    // 异步启动（AP + HTTP server），避免阻塞 LVGL 持锁上下文。
    // 栈 8KB 放 PSRAM：esp_wifi_start 首次调用做 PHY 校准，4KB 实测栈溢出（A1 飞到 RTC 区）；
    // 内部 RAM 栈会被 Opus 预分配挤占导致 xTaskCreate 静默失败（任务从未执行）
    if (xTaskCreateWithCaps(start_task, "wifi_cfg", 8192, nullptr, 2, nullptr,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        log("配网任务创建失败");
    }
}

void stop()
{
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
    }
    WifiManager::get().stopAp();
}

bool isConnected()
{
    return WifiManager::get().isConnected();
}

const char* lastResult()
{
    return _last_result;
}

const char* apSsid()
{
    return _ap_ssid;
}

}  // namespace framework::wifi_config
