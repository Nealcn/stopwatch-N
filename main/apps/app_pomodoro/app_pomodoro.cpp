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
#include <apps/common/audio/audio.h>
#include <framework/audio_mutex/audio_mutex.h>
#include <utils/settings/settings.h>
#include <cstdio>
#include <lvgl.h>

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

    // 自定义时长（设置 → Device → Pomodoro 写入 NVS ns "pomodoro"）
    {
        Settings settings("pomodoro");
        _focus_ms = (uint32_t)settings.GetInt("focus_min", 25) * 60 * 1000;
        _break_ms = (uint32_t)settings.GetInt("break_min", 5) * 60 * 1000;
        _remaining_ms = _focus_ms;
        mclog::tagInfo(getAppInfo().name, "durations: focus={}min break={}min",
                       settings.GetInt("focus_min", 25), settings.GetInt("break_min", 5));
    }

    // 重进应用时重置状态（onClose 不清 _state）
    _phase   = Phase::Focus;
    _state   = State::Idle;
    _cycle   = 0;

    LvglLockGuard lock;

    lv_obj_t* screen = lv_screen_active();

    // 倒计时大字（48px 等宽字体）
    _time_label = lv_label_create(screen);
    lv_obj_align(_time_label, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_font(_time_label, &lv_font_maple_mono_medium_48, 0);
    lv_obj_set_style_text_color(_time_label, lv_color_hex(0xFF7A8A), 0);

    // 状态文字
    _status_label = lv_label_create(screen);
    lv_obj_align(_status_label, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_style_text_font(_status_label, &lv_font_cn_24, 0);
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0x9AA5B5), 0);

    // 轮次提示
    _cycle_label = lv_label_create(screen);
    lv_obj_align(_cycle_label, LV_ALIGN_CENTER, 0, 55);
    lv_obj_set_style_text_font(_cycle_label, &lv_font_cn_24, 0);
    lv_obj_set_style_text_color(_cycle_label, lv_color_hex(0x6B7686), 0);

    // 开始/暂停按钮
    _toggle_button = std::make_unique<Button>(screen);
    _toggle_button->align(LV_ALIGN_BOTTOM_MID, 0, -50);
    _toggle_button->setSize(160, 70);
    _toggle_button->label().setText("开始");
    // 中文字体：默认 Montserrat 无 CJK，否则按钮显示为 □□
    lv_obj_set_style_text_font(_toggle_button->label().get(), &lv_font_cn_24, 0);
    _toggle_button->onClick().connect([this]() { _toggle(); });

    _update_labels();
}

void AppPomodoro::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (_state == State::Running) {
        uint32_t now = GetHAL().millis();
        if (now - _last_tick_ms >= 1000) {
            uint32_t elapsed = now - _last_tick_ms;
            _last_tick_ms    = now;
            if (elapsed >= _remaining_ms) {
                _remaining_ms = 0;
                _on_phase_complete();
            } else {
                _remaining_ms -= elapsed;
            }
            _update_labels();
        }
    }
}

void AppPomodoro::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    _toggle_button.reset();
    if (_time_label != nullptr) {
        lv_obj_delete(_time_label);
        _time_label = nullptr;
    }
    if (_status_label != nullptr) {
        lv_obj_delete(_status_label);
        _status_label = nullptr;
    }
    if (_cycle_label != nullptr) {
        lv_obj_delete(_cycle_label);
        _cycle_label = nullptr;
    }
}

void AppPomodoro::_toggle()
{
    if (_state == State::Idle || _state == State::Paused) {
        _state        = State::Running;
        _last_tick_ms = GetHAL().millis();
        _toggle_button->label().setText("暂停");
    } else if (_state == State::Running) {
        _state = State::Paused;
        _toggle_button->label().setText("继续");
    }
    _update_labels();
}

void AppPomodoro::_update_labels()
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", _remaining_ms / 60000, (_remaining_ms % 60000) / 1000);
    lv_label_set_text(_time_label, buf);

    const char* status = "点击下方按钮开始";
    if (_state == State::Running) {
        status = (_phase == Phase::Focus) ? "专注中" : "休息中";
    } else if (_state == State::Paused) {
        status = "已暂停";
    }
    lv_label_set_text(_status_label, status);

    char cycle[24];
    snprintf(cycle, sizeof(cycle), "第 %u 轮", _cycle + 1);
    lv_label_set_text(_cycle_label, cycle);
}

void AppPomodoro::_on_phase_complete()
{
    // 到点提醒：震动 + 语音播报（经全局音频互斥锁）
    _play_phase_complete_feedback();
    GetHAL().vibrate(400, 120);

    if (_phase == Phase::Focus) {
        _phase = Phase::Break;
        _remaining_ms = _break_ms;
        mclog::tagInfo(getAppInfo().name, "focus done, break start");
    } else {
        _phase = Phase::Focus;
        _remaining_ms = _focus_ms;
        ++_cycle;
        mclog::tagInfo(getAppInfo().name, "break done, cycle {}", _cycle + 1);
    }

    // 自动进入下一阶段（文档：多轮循环）
    _state        = State::Running;
    _last_tick_ms = GetHAL().millis();
    _toggle_button->label().setText("暂停");

    // 阶段切换时更新主色
    lv_obj_set_style_text_color(_time_label,
                                _phase == Phase::Focus ? lv_color_hex(0xFF7A8A) : lv_color_hex(0x7AC4FF), 0);
}

void AppPomodoro::_play_phase_complete_feedback()
{
    // 上行三音提醒（C5-E5-G5），播放通道经音频互斥锁保护
    if (!framework::AudioMutex::get().acquirePlay(500)) {
        mclog::tagWarn(getAppInfo().name, "audio channel busy, skip feedback");
        return;
    }
    audio::play_melody(std::vector<int>{72, 76, 79}, 0.15f, 0.6f);
    framework::AudioMutex::get().releasePlay();
}
