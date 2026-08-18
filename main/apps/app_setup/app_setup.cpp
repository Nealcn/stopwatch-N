/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_setup.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <utils/settings/settings.h>

using namespace mooncake;
using namespace view;
using namespace setup_workers;

AppSetup::AppSetup()
{
    setAppInfo().name = "Settings";
    setAppInfo().icon = (void*)&icon_setup;
}

void AppSetup::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    // open();
}

void AppSetup::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    // Reset state
    _destroy_menu    = false;
    _need_warm_reset = false;
    _magic_count     = 0;

    _menu_sections = {
        {
            "Device",
            {
                {"Brightness",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<BrightnessWorker>();
                 }},
                {"Volume",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<VolumeWorker>();
                 }},
                {"Button",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<ButtonWorker>();
                 }},
                {"Pomodoro",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<PomodoroWorker>();
                 }},
                {"WiFi",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<WifiConfigWorker>();
                 }},
            },
        },
        {
            "Time & Date",
            {
                {"Set Time",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<SetTimeWorker>();
                 }},
                {"Set Date",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<SetDateWorker>();
                 }},
            },
        },
        {
            "Firmware",
            {
                {fmt::format("Version: {}", common::FirmwareVersion),
                 [&]() {
                     _magic_count++;
                     if (_magic_count >= 10) {
                         _magic_count  = 0;
                         _destroy_menu = true;
                         _worker       = std::make_unique<AboutWorker>();
                     }
                 }},
                {"Clear AI Config",
                 [&]() {
                     // 清除小智 websocket 配置，AI 对话下次打开重新激活（出激活码）。
                     // 注意：只删 url/host/port/token（旧 token 残留会连接被拒），
                     // 保留 client_id（设备身份，服务器按它识别绑定状态；
                     // EraseAll 会导致 client_id 每次清除后漂移 → 服务器拒绝连接）
                     Settings ws_settings("websocket", true);
                     ws_settings.EraseKey("url");
                     ws_settings.EraseKey("host");
                     ws_settings.EraseKey("port");
                     ws_settings.EraseKey("token");
                     mclog::tagInfo("AppSetup", "AI websocket config cleared, will re-activate");
                 }},
            },
        },
    };

    LvglLockGuard lock;

    _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
}

void AppSetup::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    LvglLockGuard lock;

    if (_menu_page) {
        _menu_page->update();
    }

    if (_destroy_menu) {
        _menu_page.reset();
        _destroy_menu = false;
    }

    if (_worker) {
        _worker->update();
        if (_worker->isDone()) {
            _worker.reset();
            _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
        }
    }
}

void AppSetup::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _menu_sections.clear();
    _menu_page.reset();
    _worker.reset();

    if (_need_warm_reset) {
        // GetHAL().requestWarmReboot(6);
    }
}
