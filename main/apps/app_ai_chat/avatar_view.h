/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 表情 Avatar（阶段三 P2 升级版）
 *
 * 移植自 stackchan-newstep（M5Stack CoreS3）shizhou_avatar::LvglAvatar：
 *   - 21 种表情 + 13 种装饰 Overlay（泪滴/爱心眼/腮红/墨镜/问号/zzz…）
 *   - 呼吸动画（随表情调幅值/周期）、自动眨眼（慢眨眼）、视线扫视（saccade）
 *   - 说话嘴型（StartSpeaking 时长内随机开合）
 *   - 50ms lv_timer 自驱，不依赖应用 tick
 *
 * 240x240 圆屏适配：CoreS3 原布局为 320x240，坐标等比缩放
 *   sx(x) = x*3/4，sy(y) = y*3/4 + 30（240 高度居中）
 */
#pragma once

#include <lvgl.h>
#include <cstdint>

namespace app_ai_chat {

// 表情（与服务器 llm.emotion 字符串对应，见 MapEmotion）
enum class AvatarEmotion {
    Neutral, Happy, Angry, Sad, Sleepy,
    Loving, Crying,
    Kissy, Cool, Confident,
    Shocked, Thinking, Surprised, Confused,
    Embarrassed, Silly, Winking, Laughing, Funny, Relaxed, Delicious
};

// 装饰层（可与表情叠加）
struct AvatarOverlay {
    bool tear = false;
    bool heart_eyes = false;
    bool kiss_heart = false;
    bool cheek_blush = false;
    bool cool_glasses = false;
    bool excl_mark = false;
    bool think_bubble = false;
    bool star_burst = false;
    bool wave_squiggle = false;
    bool drool = false;
    bool laugh_lines = false;
    bool question_mark = false;
    bool zzz = false;
};

class AvatarView {
public:
    AvatarView(lv_obj_t* parent, int w, int h);
    ~AvatarView();

    bool IsReady() const { return _canvas != nullptr; }

    /** 字符串表情（服务器 llm.emotion 直通：自动 MapEmotion + OverlayFor） */
    void setEmotion(const char* emotion);
    /** 字符串表情 + 额外装饰叠加（对应 stackchan OnPetted：Loving + heart_eyes + cheek_blush） */
    void setEmotion(const char* emotion, const AvatarOverlay& extra);
    void setEmotion(AvatarEmotion e);
    void setOverlay(const AvatarOverlay& o);
    /** 说话嘴型：duration_ms 内随机开合 */
    void startSpeaking(uint32_t duration_ms);
    void stopSpeaking();

    lv_obj_t* get() const { return _canvas; }

    /** 表情字符串 → AvatarEmotion（含装饰层，供调用方预判） */
    static AvatarEmotion MapEmotion(const char* e);
    static AvatarOverlay OverlayFor(const char* e);

private:
    static void TimerCb(lv_timer_t* t);
    void OnTick();
    void UpdateBreathParams();
    bool BlinkAllowed() const;
    bool SlowBlink() const;
    bool SaccadeEnabled() const;
    void GetGazeOverride(float* gh, float* gv) const;

    void Draw();
    void DrawMouth(lv_layer_t* layer, lv_color_t fg, lv_color_t bg);
    void DrawEye(lv_layer_t* layer, lv_color_t fg, lv_color_t bg, bool is_left);
    void DrawOverlay(lv_layer_t* layer, lv_color_t fg, lv_color_t bg);

    // 绘制原语（坐标经 sx/sy 缩放到 240 坐标系）
    void FillRect(lv_layer_t* layer, int x, int y, int w, int h, lv_color_t c);
    void FillCircle(lv_layer_t* layer, int cx, int cy, int r, lv_color_t c);
    void FillTriangle(lv_layer_t* layer, int x0, int y0, int x1, int y1, int x2, int y2, lv_color_t c);
    void FillRoundRect(lv_layer_t* layer, int x, int y, int w, int h, int radius, lv_color_t c);
    void DrawArc(lv_layer_t* layer, int cx, int cy, int r, int start_deg, int end_deg, int width, lv_color_t c, bool rounded = false);
    void DrawLine(lv_layer_t* layer, int x1, int y1, int x2, int y2, int width, bool round, lv_color_t c);

    // 320x240（CoreS3 原布局）→ 240x240 等比缩放
    static int sx(int x) { return (x * 3) / 4; }
    static int sy(int y) { return (y * 3) / 4 + 30; }

    lv_obj_t* _canvas = nullptr;
    uint8_t* _buf = nullptr;          // RGB565, PSRAM
    lv_timer_t* _timer = nullptr;

    AvatarEmotion _emotion = AvatarEmotion::Neutral;
    AvatarOverlay _overlay;

    uint32_t _tick_count = 0;
    uint32_t _next_blink_ms = 0;
    uint32_t _last_saccade_ms = 0;
    uint32_t _speaking_until_ms = 0;
    bool _eye_closed = false;
    float _eye_open_ratio = 1.0f;
    float _mouth_open = 0.0f;
    float _breath = 0.0f;
    float _gaze_h = 0.0f;
    float _gaze_v = 0.0f;

    float _breath_amp = 3.0f;
    uint32_t _breath_period_steps = 100;
    bool _breath_paused = false;
};

}  // namespace app_ai_chat
