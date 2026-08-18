/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "touch_pad.h"
#include <hal/hal.h>
#include <mooncake_log.h>
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
        // 进入滑动：取消轻点/长按判定，输出移动（带加速度曲线）
        if (_tap_pending) {
            _tap_pending = false;
        }
        if (abs(dx) < _move_deadzone && abs(dy) < _move_deadzone) {
            return;
        }
        // 加速度：位移越大增益越高（k=0.08，g=1.0 基准），量化取整
        float k    = 0.08f;
        float mag  = std::sqrt(static_cast<float>(dx * dx + dy * dy));
        float gain = _gain * (1.0f + k * mag);
        int out_dx = static_cast<int>(dx * gain);
        int out_dy = static_cast<int>(dy * gain);
        if (out_dx == 0 && out_dy == 0) {
            out_dx = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
            out_dy = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);
        }
        _emit(TouchPadEvent::Move, out_dx, out_dy, 0);
        return;
    }

    // 未移动：长按判定（>500ms → 右键）
    if (!_right_sent && held >= _long_press_ms) {
        _right_sent  = true;
        _tap_pending = false;
        _emit(TouchPadEvent::ButtonDown, 0, 0, 2);  // 右键按下
        _emit(TouchPadEvent::ButtonUp, 0, 0, 2);    // 立即抬起（点按右键）
        mclog::tagInfo("TouchPad", "right click");
    }

    if (tp.num == 0) {
        // 抬起：轻点 → 左键
        if (_tap_pending && held < _tap_timeout_ms) {
            _emit(TouchPadEvent::ButtonDown, 0, 0, 1);
            _emit(TouchPadEvent::ButtonUp, 0, 0, 1);
            mclog::tagInfo("TouchPad", "left click");
        }
        _pressed = false;
    }
}

}  // namespace framework
