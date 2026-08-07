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
#include <string>
#include <apps/common/key_manager/key_manager.h>

/**
 * @brief VoiceCube 桌面模式（语音输入棒 + 触摸板鼠标）
 *
 * 交互（用户视角）：
 *  1. 连接电脑（BLE，桌面端程序配合）
 *  2. 按住侧键说话 → 松开 → 云 ASR 识别
 *  3. 识别文字在屏上大字预览（不自动粘贴）
 *  4. 触摸屏当触摸板：滑动=移动光标、轻点=左键、长按=右键
 *  5. 光标到位 → 点"确认"→ 桌面端 Ctrl+V 粘贴；"取消"→ 丢弃
 *
 * 录音链路：hal_audio 44.1kHz → 线性重采样 16kHz → Opus 60ms 帧 → BLE
 * 音频帧协议与 Nealcn/VoiceCube 完全兼容（16B 头 + Opus payload）。
 */
class AppVoiceCube : public mooncake::AppAbility {
public:
    AppVoiceCube();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class State : uint8_t { Idle, Rec, Asr, Preview, Pasting };

    void _set_state(State s);
    void _start_record();
    void _stop_record();
    void _on_control(const std::string& json);
    void _send_mouse(int dx, int dy, uint8_t btn, const char* action);
    void _update_labels();

    static void _record_task_entry(void* arg);

    std::unique_ptr<input::KeyManager> _key_manager;
    State _state = State::Idle;

    lv_obj_t* _status_label  = nullptr;
    lv_obj_t* _preview_label = nullptr;
    lv_obj_t* _hint_label    = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _confirm_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _cancel_button;

    TaskHandle_t _record_task  = nullptr;
    volatile bool _record_stop = false;
    uint32_t _session_id       = 0;
    uint32_t _seq              = 0;
    std::string _preview_text;
    uint32_t _feedback_until_ms = 0;  // Pasting 反馈显示截止时间
};
