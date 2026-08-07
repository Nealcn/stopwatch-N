/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <smooth_lvgl.hpp>
#include <memory>
#include <apps/common/key_manager/key_manager.h>

/**
 * @brief 番茄工作倒计时（A 级趣味功能，阶段一接入）
 *
 * 25 分钟专注 + 5 分钟休息多轮循环，到点震动 + 语音播报。
 * 当前为骨架：UI 与计时逻辑在阶段一 W2 实现（参照 app_alarm_clock 的 model + view 分层）。
 */
class AppPomodoro : public mooncake::AppAbility {
public:
    AppPomodoro();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _placeholder;
};
