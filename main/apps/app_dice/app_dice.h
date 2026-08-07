/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <smooth_lvgl.hpp>
#include <cstdint>
#include <memory>
#include <apps/common/key_manager/key_manager.h>

/**
 * @brief 骰子模拟器（A 级趣味功能，阶段一 W2 实现）
 *
 * 摇晃设备读取 IMU 采样随机数，圆形屏渲染 1–6 点骰子图案（文档 3.2 A 级）。
 * 摇晃检测：加速度幅值（|ax|+|ay|+|az|）超过阈值触发掷骰，阈值需硬件实测校准。
 */
class AppDice : public mooncake::AppAbility {
public:
    AppDice();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr uint8_t _dot_positions[6][6] = {
        {4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},        // 1: 中心
        {0, 8, 0xFF, 0xFF, 0xFF, 0xFF},           // 2: 左上 右下
        {0, 4, 8, 0xFF, 0xFF, 0xFF},              // 3: 左上 中心 右下
        {0, 2, 6, 8, 0xFF, 0xFF},                 // 4: 四角
        {0, 2, 4, 6, 8, 0xFF},                    // 5: 四角 中心
        {0, 2, 3, 5, 6, 8},                       // 6: 两列
    };
    static constexpr int _dot_grid_x[9] = {40, 80, 120, 40, 80, 120, 40, 80, 120};
    static constexpr int _dot_grid_y[9] = {40, 40, 40, 80, 80, 80, 120, 120, 120};

    void _set_face(uint8_t value);
    void _roll();
    void _check_shake();

    std::unique_ptr<input::KeyManager> _key_manager;

    lv_obj_t* _dice_panel   = nullptr;  // 骰子面容器（200x200 圆角白底）
    lv_obj_t* _dots[6]      = {};       // 6 个圆点
    lv_obj_t* _hint_label   = nullptr;
    lv_obj_t* _result_label = nullptr;

    uint8_t _value      = 1;
    bool _rolling       = false;
    uint8_t _roll_tick  = 0;
    uint32_t _roll_tick_ms = 0;
};
