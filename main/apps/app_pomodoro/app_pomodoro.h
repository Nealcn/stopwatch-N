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
 * @brief 番茄工作倒计时（A 级趣味功能，阶段一 W2 实现）
 *
 * 25 分钟专注 + 5 分钟休息多轮循环，到点震动 + 语音播报（文档 3.2 A 级）。
 * 固定 25/5 时长，自定义时长留待后续版本（系统设置联动）。
 */
class AppPomodoro : public mooncake::AppAbility {
public:
    AppPomodoro();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Phase : uint8_t { Focus, Break };
    enum class State : uint8_t { Idle, Running, Paused };

    static constexpr uint32_t _focus_ms = 25 * 60 * 1000;
    static constexpr uint32_t _break_ms = 5 * 60 * 1000;

    void _toggle();
    void _update_labels();
    void _on_phase_complete();
    void _play_phase_complete_feedback();

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _toggle_button;

    lv_obj_t* _time_label   = nullptr;
    lv_obj_t* _status_label = nullptr;
    lv_obj_t* _cycle_label  = nullptr;

    Phase _phase          = Phase::Focus;
    State _state          = State::Idle;
    uint32_t _remaining_ms = _focus_ms;
    uint32_t _last_tick_ms = 0;
    uint8_t _cycle         = 0;
};
