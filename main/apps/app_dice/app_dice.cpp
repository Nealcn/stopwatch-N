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
#include <esp_random.h>
#include <cmath>
#include <cstdio>
#include <lvgl.h>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

// 摇晃触发阈值（|ax|+|ay|+|az| 之和，单位 g；静止约 1.0~1.2，摇晃显著高于此值）
// TODO: 硬件实测校准
static constexpr float _shake_threshold = 1.8f;
static constexpr uint8_t _roll_frames   = 10;  // 滚动动画帧数
static constexpr uint32_t _roll_frame_ms = 80;

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

    lv_obj_t* screen = lv_screen_active();

    // 骰子面板：200x200 圆角白底，位于屏幕中心偏上
    _dice_panel = lv_obj_create(screen);
    lv_obj_set_size(_dice_panel, 200, 200);
    lv_obj_align(_dice_panel, LV_ALIGN_CENTER, 0, -25);
    lv_obj_set_style_bg_color(_dice_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(_dice_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_dice_panel, 24, 0);
    lv_obj_set_style_border_width(_dice_panel, 0, 0);

    // 6 个圆点（按需显示）
    for (auto& dot : _dots) {
        dot = lv_obj_create(_dice_panel);
        lv_obj_set_size(dot, 44, 44);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x1F2937), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }

    // 提示与结果
    _hint_label = lv_label_create(screen);
    lv_obj_align(_hint_label, LV_ALIGN_CENTER, 0, 108);
    lv_obj_set_style_text_font(_hint_label, &lv_font_maple_mono_medium_24, 0);
    lv_obj_set_style_text_color(_hint_label, lv_color_hex(0x9AA5B5), 0);
    lv_label_set_text(_hint_label, "摇晃设备掷骰");

    _result_label = lv_label_create(screen);
    lv_obj_align(_result_label, LV_ALIGN_CENTER, 0, 145);
    lv_obj_set_style_text_font(_result_label, &lv_font_maple_mono_medium_24, 0);
    lv_obj_set_style_text_color(_result_label, lv_color_hex(0x6B7686), 0);

    _set_face(1);
}

void AppDice::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    // 滚动动画：每帧随机换面，结束后定格
    if (_rolling) {
        uint32_t now = GetHAL().millis();
        if (now - _roll_tick_ms >= _roll_frame_ms) {
            _roll_tick_ms = now;
            ++_roll_tick;
            if (_roll_tick >= _roll_frames) {
                _rolling = false;
                _set_face(_value);
                char buf[16];
                snprintf(buf, sizeof(buf), "点数：%u", _value);
                lv_label_set_text(_result_label, buf);
                mclog::tagInfo(getAppInfo().name, "roll result: {}", _value);
            } else {
                _set_face(esp_random() % 6 + 1);  // 滚动中的随机面
            }
        }
        return;
    }

    _check_shake();
}

void AppDice::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    if (_dice_panel != nullptr) {
        lv_obj_delete(_dice_panel);  // 圆点随父容器一起销毁
        _dice_panel = nullptr;
    }
    if (_hint_label != nullptr) {
        lv_obj_delete(_hint_label);
        _hint_label = nullptr;
    }
    if (_result_label != nullptr) {
        lv_obj_delete(_result_label);
        _result_label = nullptr;
    }
}

void AppDice::_set_face(uint8_t value)
{
    if (value < 1 || value > 6) {
        value = 1;
    }
    // 先全部隐藏
    for (auto& dot : _dots) {
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
    // 按点数点亮对应位置的圆点
    for (int i = 0; i < 6; ++i) {
        uint8_t pos = _dot_positions[value - 1][i];
        if (pos == 0xFF) {
            break;
        }
        lv_obj_clear_flag(_dots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(_dots[i], _dot_grid_x[pos], _dot_grid_y[pos]);
    }
}

void AppDice::_check_shake()
{
    GetHAL().updateImuData();
    const auto& imu = GetHAL().getImuData();
    float magnitude = std::abs(imu.accelX) + std::abs(imu.accelY) + std::abs(imu.accelZ);
    if (magnitude > _shake_threshold) {
        _roll();
    }
}

void AppDice::_roll()
{
    _rolling       = true;
    _roll_tick     = 0;
    _roll_tick_ms  = GetHAL().millis();
    _value         = esp_random() % 6 + 1;
    lv_label_set_text(_result_label, "");
    mclog::tagInfo(getAppInfo().name, "shake detected, rolling...");
}
