/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "avatar_view.h"

#include <esp_heap_caps.h>
#include <cstring>
#include <initializer_list>

namespace app_ai_chat {

namespace {

// ARGB8888 颜色
constexpr uint32_t kTransparent = 0x00000000;
constexpr uint32_t kSkin        = 0xFFF2C49B;  // 暖肤色
constexpr uint32_t kSkinAlert   = 0xFFE08A8A;  // 红脸（alert）
constexpr uint32_t kEyeWhite    = 0xFFFFFFFF;
constexpr uint32_t kPupil       = 0xFF3A2A22;  // 深棕
constexpr uint32_t kMouth       = 0xFF8C4A3A;

// 脸/五官布局（240 坐标系）
constexpr int kFaceCx = 120;
constexpr int kFaceCy = 128;
constexpr int kFaceR  = 90;
constexpr int kEyeY   = 112;
constexpr int kEyeLx  = 88;
constexpr int kEyeRx  = 152;
constexpr int kMouthY = 165;

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b) { return 0xFF000000u | (r << 16) | (g << 8) | b; }

}  // namespace

AvatarView::AvatarView()
{
    _canvas = lv_canvas_create(lv_screen_active());
    // PSRAM buffer：内部 RAM 留给 Opus 编解码
    _buf = (uint32_t*)heap_caps_malloc(kSize * kSize * 4, MALLOC_CAP_SPIRAM);
    if (_buf == nullptr) {
        return;  // 分配失败：canvas 空显示，不崩溃
    }
    lv_canvas_set_buffer(_canvas, _buf, kSize, kSize, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_remove_style_all(_canvas);
    lv_obj_set_size(_canvas, kSize, kSize);
    clear();
}

AvatarView::~AvatarView()
{
    if (_canvas) {
        lv_obj_delete(_canvas);
        _canvas = nullptr;
    }
    if (_buf) {
        heap_caps_free(_buf);
        _buf = nullptr;
    }
}

void AvatarView::setEmotion(AvatarEmotion e)
{
    if (e == _emotion) {
        return;
    }
    _emotion = e;
    _mouth_phase = 0;
    redraw();
}

void AvatarView::tick(uint32_t now_ms)
{
    if (_emotion != AvatarEmotion::Talking || _buf == nullptr) {
        return;
    }
    if (now_ms - _last_tick_ms < 100) {
        return;
    }
    _last_tick_ms = now_ms;
    _mouth_phase  = (_mouth_phase + 1) & 3;
    redraw();
}

// ---------------------------------------------------------------- 绘制

void AvatarView::redraw()
{
    if (_buf == nullptr) {
        return;
    }
    clear();

    const uint32_t skin = (_emotion == AvatarEmotion::Alert) ? kSkinAlert : kSkin;
    fillCircle(kFaceCx, kFaceCy, kFaceR, skin);

    drawBrows();
    drawEyes();
    drawMouth();

    lv_obj_invalidate(_canvas);
}

void AvatarView::clear()
{
    memset(_buf, 0, kSize * kSize * 4);
}

void AvatarView::setPx(int32_t x, int32_t y, uint32_t c)
{
    if (x < 0 || x >= kSize || y < 0 || y >= kSize) {
        return;
    }
    _buf[y * kSize + x] = c;
}

void AvatarView::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
    for (int32_t yy = y; yy < y + h; ++yy) {
        for (int32_t xx = x; xx < x + w; ++xx) {
            setPx(xx, yy, c);
        }
    }
}

void AvatarView::fillCircle(int32_t cx, int32_t cy, int32_t r, uint32_t c)
{
    const int32_t r2 = r * r;
    for (int32_t yy = cy - r; yy <= cy + r; ++yy) {
        for (int32_t xx = cx - r; xx <= cx + r; ++xx) {
            const int32_t dx = xx - cx;
            const int32_t dy = yy - cy;
            if (dx * dx + dy * dy <= r2) {
                setPx(xx, yy, c);
            }
        }
    }
}

void AvatarView::fillEllipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t c)
{
    const int32_t rx2 = rx * rx;
    const int32_t ry2 = ry * ry;
    for (int32_t yy = cy - ry; yy <= cy + ry; ++yy) {
        for (int32_t xx = cx - rx; xx <= cx + rx; ++xx) {
            const int32_t dx = xx - cx;
            const int32_t dy = yy - cy;
            // (dx/rx)^2 + (dy/ry)^2 <= 1，整数化避免浮点
            if (dx * dx * ry2 + dy * dy * rx2 <= rx2 * ry2) {
                setPx(xx, yy, c);
            }
        }
    }
}

void AvatarView::drawBrows()
{
    switch (_emotion) {
        case AvatarEmotion::Happy:
            // 双眉上抬
            fillRect(kEyeLx - 12, 78, 26, 5, kPupil);
            fillRect(kEyeRx - 14, 78, 26, 5, kPupil);
            break;
        case AvatarEmotion::Sad:
            // 内端上抬（外低内高）
            fillRect(kEyeLx - 12, 82, 13, 5, kPupil);
            fillRect(kEyeLx + 1, 86, 13, 5, kPupil);
            fillRect(kEyeRx - 14, 86, 13, 5, kPupil);
            fillRect(kEyeRx - 1, 82, 13, 5, kPupil);
            break;
        case AvatarEmotion::Thinking:
            // 左眉上抬（歪头思考）
            fillRect(kEyeLx - 12, 76, 26, 5, kPupil);
            fillRect(kEyeRx - 14, 84, 26, 5, kPupil);
            break;
        case AvatarEmotion::Angry:
            // 内端下压（倒八字）
            fillRect(kEyeLx - 12, 88, 13, 5, kPupil);
            fillRect(kEyeLx + 1, 84, 13, 5, kPupil);
            fillRect(kEyeRx - 14, 84, 13, 5, kPupil);
            fillRect(kEyeRx - 1, 88, 13, 5, kPupil);
            break;
        case AvatarEmotion::Surprised:
            fillRect(kEyeLx - 12, 76, 26, 5, kPupil);
            fillRect(kEyeRx - 14, 76, 26, 5, kPupil);
            break;
        default:
            // Neutral / Talking / Alert：平眉
            fillRect(kEyeLx - 12, 84, 26, 5, kPupil);
            fillRect(kEyeRx - 14, 84, 26, 5, kPupil);
            break;
    }
}

void AvatarView::drawEyes()
{
    const int pupil_r  = 9;
    const int pupil_rx = 8;

    for (int cx : {kEyeLx, kEyeRx}) {
        // 眼白
        fillCircle(cx, kEyeY, 20, kEyeWhite);

        switch (_emotion) {
            case AvatarEmotion::Happy:
                // 笑眼：瞳孔压扁下移
                fillEllipse(cx, kEyeY + 4, 8, 6, kPupil);
                break;
            case AvatarEmotion::Sad:
                // 瞳孔下移，上方留白
                fillEllipse(cx, kEyeY + 5, pupil_rx, 7, kPupil);
                break;
            case AvatarEmotion::Thinking:
                // 瞳孔上移（眼神向上）
                fillEllipse(cx, kEyeY - 3, pupil_rx, 6, kPupil);
                break;
            case AvatarEmotion::Angry:
                // 瞳孔缩小
                fillCircle(cx, kEyeY, 7, kPupil);
                break;
            case AvatarEmotion::Surprised:
                fillCircle(cx, kEyeY, 12, kPupil);
                break;
            case AvatarEmotion::Alert:
                fillCircle(cx, kEyeY, 12, kPupil);
                break;
            default:
                fillCircle(cx, kEyeY, pupil_r, kPupil);
                break;
        }
    }
}

void AvatarView::drawMouth()
{
    switch (_emotion) {
        case AvatarEmotion::Happy: {
            // 开口大笑：深色椭圆 + 上覆肤色椭圆形成月牙
            fillEllipse(kFaceCx, kMouthY + 4, 24, 11, kMouth);
            fillEllipse(kFaceCx, kMouthY - 3, 17, 5, kSkin);
            break;
        }
        case AvatarEmotion::Sad:
            fillEllipse(kFaceCx, kMouthY + 3, 11, 4, kMouth);
            break;
        case AvatarEmotion::Angry:
            fillRect(kFaceCx - 12, kMouthY + 1, 24, 5, kMouth);
            break;
        case AvatarEmotion::Surprised:
            fillCircle(kFaceCx, kMouthY + 1, 10, kMouth);
            break;
        case AvatarEmotion::Talking: {
            // 嘴型开合序列 [4,10,14,10]（~2.5Hz，400ms 周期）
            static const int kOpen[] = {4, 10, 14, 10};
            fillEllipse(kFaceCx, kMouthY, 22, kOpen[_mouth_phase], kMouth);
            break;
        }
        case AvatarEmotion::Thinking:
            fillEllipse(kFaceCx, kMouthY, 10, 3, kMouth);
            break;
        case AvatarEmotion::Alert:
            fillEllipse(kFaceCx, kMouthY + 2, 8, 4, kMouth);
            break;
        default:
            fillEllipse(kFaceCx, kMouthY, 15, 5, kMouth);
            break;
    }
}

}  // namespace app_ai_chat
