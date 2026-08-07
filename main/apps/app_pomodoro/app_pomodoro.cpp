/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_pomodoro.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

AppPomodoro::AppPomodoro()
{
    setAppInfo().name = "番茄倒计时";
    setAppInfo().icon = (void*)&icon_pomodoro;
}

void AppPomodoro::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppPomodoro::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    // TODO(阶段一 W2): 替换为完整番茄钟 UI（专注/休息倒计时 + 开始暂停 + 设置）
    _placeholder = std::make_unique<Button>(lv_screen_active());
    _placeholder->setAlign(LV_ALIGN_CENTER);
    _placeholder->label().setText("番茄倒计时\n25:00 / 05:00\n(骨架占位)");
    _placeholder->onClick().connect([this]() { close(); });
}

void AppPomodoro::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }
    // TODO(阶段一 W2): 倒计时状态机刷新（GetHAL().millis() 节流）
}

void AppPomodoro::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    _placeholder.reset();
}
