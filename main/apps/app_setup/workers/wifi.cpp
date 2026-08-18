/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <assets/assets.h>
#include <framework/wifi_config/wifi_config.h>
#include <framework/wifi_manager/wifi_manager.h>
#include <mooncake_log.h>
#include <cstdio>

using namespace smooth_ui_toolkit::lvgl_cpp;

namespace setup_workers {

namespace {

constexpr const char* _tag = "WifiWorker";

}  // namespace

WifiConfigWorker::WifiConfigWorker()
{
    mclog::tagInfo(_tag, "start wifi config worker");

    // 注意：worker 构造/update/析构都在 AppSetup 已持锁的上下文调用
    // （onClick 回调 / onRunning / onClose），这里绝不能再加锁（嵌套锁死锁）
    lv_obj_t* screen = lv_screen_active();

    _title_label = lv_label_create(screen);
    lv_obj_align(_title_label, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_text_font(_title_label, &lv_font_cn_26, 0);
    lv_obj_set_style_text_color(_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(_title_label, "WiFi 配置");

    _hint_label = lv_label_create(screen);
    lv_obj_align(_hint_label, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_text_font(_hint_label, &lv_font_cn_24, 0);
    lv_obj_set_style_text_color(_hint_label, lv_color_hex(0x9AA5B5), 0);
    lv_obj_set_width(_hint_label, 420);
    lv_obj_set_style_text_align(_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_hint_label, LV_LABEL_LONG_WRAP);

    _status_label = lv_label_create(screen);
    lv_obj_align(_status_label, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_text_font(_status_label, &lv_font_cn_24, 0);
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0x7AC4FF), 0);
    lv_obj_set_width(_status_label, 420);
    lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_WRAP);

    // 启动配网热点 + HTTP server
    framework::wifi_config::start([this](std::string_view msg) {
        LvglLockGuard lock;
        lv_label_set_text(_status_label, std::string(msg).c_str());
    });

    char hint[128];
    snprintf(hint, sizeof(hint), "手机连接热点 %s\n然后浏览器打开 192.168.4.1\n填写 WiFi 密码后保存",
             framework::wifi_config::apSsid());
    lv_label_set_text(_hint_label, hint);
    lv_label_set_text(_status_label, "等待配网…");
}

void WifiConfigWorker::update()
{
    // 轮询连接状态刷新
    static uint32_t last_tick = 0;
    uint32_t now              = GetHAL().millis();
    if (now - last_tick < 500) {
        return;
    }
    last_tick = now;

    if (framework::wifi_config::isConnected()) {
        lv_obj_set_style_text_color(_status_label, lv_color_hex(0x50D080), 0);
        lv_label_set_text(_status_label, "WiFi 已连接 ✓\n按侧键返回");
    } else {
        lv_label_set_text(_status_label, framework::wifi_config::lastResult());
    }
}

WifiConfigWorker::~WifiConfigWorker()
{
    mclog::tagInfo(_tag, "stop wifi config worker");
    framework::wifi_config::stop();

    if (_title_label != nullptr) {
        lv_obj_delete(_title_label);
        _title_label = nullptr;
    }
    if (_hint_label != nullptr) {
        lv_obj_delete(_hint_label);
        _hint_label = nullptr;
    }
    if (_status_label != nullptr) {
        lv_obj_delete(_status_label);
        _status_label = nullptr;
    }
}

}  // namespace setup_workers
