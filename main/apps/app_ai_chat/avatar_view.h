/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 表情 Avatar（阶段三 P2）
 * 240x240 ARGB8888 lv_canvas（PSRAM buffer），参数化绘制。
 * 7 表情 + Talking 嘴型动画（100ms 节流，App onRunning 驱动 tick）。
 */
#pragma once

#include <lvgl.h>
#include <cstdint>

namespace app_ai_chat {

enum class AvatarEmotion {
    Neutral,
    Happy,
    Sad,
    Thinking,
    Angry,
    Surprised,
    Talking,  // 嘴型动画（Speaking 状态驱动）
    Alert,    // 红脸提示
};

class AvatarView {
public:
    AvatarView();
    ~AvatarView();

    /** 表情变化时整脸重绘（内部判变，幂等） */
    void setEmotion(AvatarEmotion e);
    /** 嘴型动画帧驱动：仅 Talking 时每 100ms 重绘，其余直接返回 */
    void tick(uint32_t now_ms);

    lv_obj_t* get() const { return _canvas; }

private:
    void redraw();
    void clear();

    // 像素写入（ARGB8888，little-endian: B G R A）
    void setPx(int32_t x, int32_t y, uint32_t rgba);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba);
    void fillCircle(int32_t cx, int32_t cy, int32_t r, uint32_t rgba);
    void fillEllipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t rgba);

    void drawEyes();
    void drawBrows();
    void drawMouth();

    static constexpr int kSize = 240;
    lv_obj_t* _canvas = nullptr;
    uint32_t* _buf    = nullptr;  // ARGB8888, PSRAM
    AvatarEmotion _emotion = AvatarEmotion::Neutral;
    uint32_t _last_tick_ms = 0;
    uint8_t _mouth_phase   = 0;  // 0..3 嘴型开合序列
};

}  // namespace app_ai_chat
