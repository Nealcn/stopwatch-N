/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "touch_pad.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

// 触摸板手势识别（阶段二实现）
//
// 状态机：松开 →（按下）→ 按下中：位移>阈值即进入"滑动移动"，
// 否则在抬起时判定为轻点（左键）；按下时长超阈值且未移动则长按（右键）。
// 参考官方 watch_face 的 lv_indev 轮询范式，但用 HAL getTouchPoint 直读（每帧一次）。

namespace framework {

TouchPad& TouchPad::get()
{
    static TouchPad _instance;
    return _instance;
}

void TouchPad::setEventCallback(EventCallback cb)
{
    _callback = std::move(cb);
}

void TouchPad::setSensitivity(float gain)
{
    _gain = gain;
}

void TouchPad::_emit(TouchPadEvent::Type type, int dx, int dy, uint8_t button)
{
    if (_callback) {
        _callback(TouchPadEvent{type, dx, dy, button});
    }
}

void TouchPad::update()
{
    auto tp = GetHAL().getTouchPoint();
    uint32_t now = GetHAL().millis();

    if (!_pressed && tp.num > 0) {
        // 按下
        _pressed   = true;
        _tap_pending = true;
        _right_sent  = false;
        _press_ms  = now;
        _press_x = _last_x = tp.x;
        _press_y = _last_y = tp.y;
        _total_dx = _total_dy = 0;
        return;
    }

    if (!_pressed) {
        return;
    }

    // 按下中
    int dx = tp.x - _last_x;
    int dy = tp.y - _last_y;
    _last_x = tp.x;
    _last_y = tp.y;
    _total_dx += dx;
    _total_dy += dy;

    int moved = (_total_dx * _total_dx + _total_dy * _total_dy) > (_tap_max_move * _tap_max_move);
    uint32_t held = now - _press_ms;

    if (moved) {
        // 进入滑动：取消轻点/长按判定，输出移动（带封顶加速度曲线）
        _tap_pending = false;
        if (_right_sent) {
            // 长按右键后的拖动：不再输出移动（保持右键拖拽状态）
            if (tp.num == 0) {
                _right_sent = false;
                _emit(TouchPadEvent::ButtonUp, 0, 0, 2);
                _pressed = false;
            }
            return;
        }
        if (abs(dx) < _move_deadzone && abs(dy) < _move_deadzone) {
            return;
        }
        // 加速度：慢速微调增益≈基准（精确），快滑增益升到 1.5x 后封顶，
        // 总增益封顶 6x（原 k=0.08 线性无上限，快甩增益爆炸 9x 导致过冲）
        float mag  = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        float speed_factor = 1.0f + 0.5f * std::min(mag / 25.0f, 1.0f);
        float gain = std::min(_gain * speed_factor, 6.0f);
        // 浮点残差结转：慢速 1px 微动不因取整丢失
        float fx = dx * gain + _rem_x;
        float fy = dy * gain + _rem_y;
        int out_dx = static_cast<int>(fx);
        int out_dy = static_cast<int>(fy);
        _rem_x = fx - out_dx;
        _rem_y = fy - out_dy;
        _emit(TouchPadEvent::Move, out_dx, out_dy, 0);
        return;
    }

    // 未移动：长按判定（>500ms → 右键按下，保持 down 支持拖拽）
    if (!_right_sent && held >= _long_press_ms) {
        _right_sent  = true;
        _tap_pending = false;
        _emit(TouchPadEvent::ButtonDown, 0, 0, 2);
        mclog::tagInfo("TouchPad", "right press (drag)");
    }

    if (tp.num == 0) {
        // 抬起：轻点 → 左键；右键拖拽结束 → 右键抬起
        if (_right_sent) {
            _right_sent = false;
            _emit(TouchPadEvent::ButtonUp, 0, 0, 2);
            mclog::tagInfo("TouchPad", "right release");
        } else if (_tap_pending && held < _tap_timeout_ms) {
            _emit(TouchPadEvent::ButtonDown, 0, 0, 1);
            _emit(TouchPadEvent::ButtonUp, 0, 0, 1);
            mclog::tagInfo("TouchPad", "left click");
        }
        _pressed = false;
        _rem_x = 0.0f;
        _rem_y = 0.0f;
    }
}

}  // namespace framework
