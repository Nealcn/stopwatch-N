/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_dice.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

AppDice::AppDice()
{
    setAppInfo().name = "骰子模拟器";
    setAppInfo().icon = (void*)&icon_dice;
}

void AppDice::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppDice::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    // TODO(阶段一 W2): 替换为完整骰子 UI（1–6 点渲染 + 摇晃触发）
    _placeholder = std::make_unique<Button>(lv_screen_active());
    _placeholder->setAlign(LV_ALIGN_CENTER);
    _placeholder->label().setText("骰子模拟器\n摇晃设备掷骰\n(骨架占位)");
    _placeholder->onClick().connect([this]() { close(); });
}

void AppDice::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }
    // TODO(阶段一 W2): IMU 摇晃检测（updateImuData 冲击阈值）→ 随机点数
}

void AppDice::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    _placeholder.reset();
}
