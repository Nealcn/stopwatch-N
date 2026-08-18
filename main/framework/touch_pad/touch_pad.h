/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <functional>

/**
 * @brief 触摸板手势识别（VoiceCube 桌面模式：触摸屏 = 电脑鼠标）
 *
 * 语义（对照"鼠标左右键"心智）：
 *  - 手指滑动 = 鼠标移动（相对位移，加速度曲线）
 *  - 轻点（<200ms 且位移小）= 鼠标左键
 *  - 长按（>500ms 且位移小）= 鼠标右键
 *
 * 使用：app 在 onRunning 每帧调用 update()，事件经回调输出；
 * 由调用方（app_voicecube）组包发送到桌面端。
 */
namespace framework {

struct TouchPadEvent {
    enum Type : uint8_t { Move, ButtonDown, ButtonUp };
    Type type;
    int dx = 0;         // 相对位移（Move）
    int dy = 0;
    uint8_t button = 0; // 1=左键 2=右键（ButtonDown/Up）
};

class TouchPad {
public:
    using EventCallback = std::function<void(const TouchPadEvent&)>;

    static TouchPad& get();

    /* 每帧调用：轮询触摸、识别手势并回调事件 */
    void update();

    void setEventCallback(EventCallback cb);
    /* 灵敏度（滑动增益），默认 3.0（2026-08-18 标定：466px 表盘 ↔ 桌面分辨率失调主因） */
    void setSensitivity(float gain);
    /* 标定参数（已按实测设定默认值） */
    static constexpr uint32_t _tap_timeout_ms   = 200;   // 轻点判定时长
    static constexpr uint32_t _long_press_ms    = 500;   // 长按判定时长（右键）
    static constexpr int      _tap_max_move     = 15;    // 轻点/长按最大位移
    static constexpr int      _move_deadzone    = 1;     // 移动死区（CST820 噪声级别）

private:
    TouchPad()          = default;
    ~TouchPad()         = default;
    TouchPad(const TouchPad&)            = delete;
    TouchPad& operator=(const TouchPad&) = delete;

    void _emit(TouchPadEvent::Type type, int dx, int dy, uint8_t button);

    bool _pressed      = false;
    bool _tap_pending  = false;
    bool _right_sent   = false;  // 右键已触发（长按后保持 down，抬起才 up，支持拖拽）
    uint32_t _press_ms = 0;
    int _press_x = 0, _press_y = 0;
    int _last_x  = 0, _last_y  = 0;
    int _total_dx = 0, _total_dy = 0;
    float _gain   = 3.0f;
    float _rem_x  = 0.0f, _rem_y = 0.0f;  // 增益取整残差结转（慢速微动不丢失）
    EventCallback _callback;
};

}  // namespace framework
