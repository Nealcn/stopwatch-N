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
 * @brief 骰子模拟器（A 级趣味功能，阶段一接入）
 *
 * 摇晃设备读取 IMU 采样随机数，圆形屏渲染 1–6 点骰子图案。
 * 当前为骨架：摇晃检测与骰子渲染在阶段一 W2 实现。
 */
class AppDice : public mooncake::AppAbility {
public:
    AppDice();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _placeholder;
};
