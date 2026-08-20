/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "avatar_view.h"

#include <esp_heap_caps.h>
#include <cstring>
#include <cstdlib>
#include <cmath>

namespace app_ai_chat {

namespace {

constexpr lv_color_t kWhite = {0xFF, 0xFF, 0xFF};
constexpr lv_color_t kBlack = {0x00, 0x00, 0x00};

}  // namespace

AvatarView::AvatarView(lv_obj_t* parent, int w, int h)
{
    _buf = (uint8_t*)heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
    if (_buf == nullptr) {
        return;  // 分配失败：canvas 空显示，不崩溃
    }
    _canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(_canvas, _buf, w, h, LV_COLOR_FORMAT_RGB565);
    lv_obj_remove_style_all(_canvas);
    lv_obj_set_size(_canvas, w, h);
    lv_obj_clear_flag(_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(_canvas);
    _timer = lv_timer_create(&AvatarView::TimerCb, 50, this);
    _next_blink_ms = 3000;
    _last_saccade_ms = 0;
    Draw();
}

AvatarView::~AvatarView()
{
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    if (_canvas) {
        lv_obj_delete(_canvas);
        _canvas = nullptr;
    }
    if (_buf) {
        heap_caps_free(_buf);
        _buf = nullptr;
    }
}

// ---------------------------------------------------------------- 公开接口

void AvatarView::setEmotion(const char* emotion)
{
    setEmotion(MapEmotion(emotion));
    setOverlay(OverlayFor(emotion));
}

void AvatarView::setEmotion(const char* emotion, const AvatarOverlay& extra)
{
    setEmotion(MapEmotion(emotion));
    AvatarOverlay o = OverlayFor(emotion);
    o.tear |= extra.tear;
    o.heart_eyes |= extra.heart_eyes;
    o.kiss_heart |= extra.kiss_heart;
    o.cheek_blush |= extra.cheek_blush;
    o.cool_glasses |= extra.cool_glasses;
    o.excl_mark |= extra.excl_mark;
    o.think_bubble |= extra.think_bubble;
    o.star_burst |= extra.star_burst;
    o.wave_squiggle |= extra.wave_squiggle;
    o.drool |= extra.drool;
    o.laugh_lines |= extra.laugh_lines;
    o.question_mark |= extra.question_mark;
    o.zzz |= extra.zzz;
    setOverlay(o);
}

void AvatarView::setEmotion(AvatarEmotion e)
{
    if (e == _emotion) {
        return;
    }
    _emotion = e;
    Draw();
}

void AvatarView::setOverlay(const AvatarOverlay& o)
{
    _overlay = o;
    Draw();
}

void AvatarView::startSpeaking(uint32_t duration_ms)
{
    _speaking_until_ms = lv_tick_get() + duration_ms;
}

void AvatarView::stopSpeaking()
{
    _speaking_until_ms = 0;
}

// ---------------------------------------------------------------- 表情映射

AvatarEmotion AvatarView::MapEmotion(const char* e)
{
    if (!e) return AvatarEmotion::Neutral;
    if (!strcmp(e, "neutral"))     return AvatarEmotion::Neutral;
    if (!strcmp(e, "happy"))       return AvatarEmotion::Happy;
    if (!strcmp(e, "laughing"))    return AvatarEmotion::Laughing;
    if (!strcmp(e, "funny"))       return AvatarEmotion::Funny;
    if (!strcmp(e, "sad"))         return AvatarEmotion::Sad;
    if (!strcmp(e, "crying"))      return AvatarEmotion::Crying;
    if (!strcmp(e, "angry"))       return AvatarEmotion::Angry;
    if (!strcmp(e, "loving"))      return AvatarEmotion::Loving;
    if (!strcmp(e, "embarrassed")) return AvatarEmotion::Embarrassed;
    if (!strcmp(e, "surprised"))   return AvatarEmotion::Surprised;
    if (!strcmp(e, "shocked"))     return AvatarEmotion::Shocked;
    if (!strcmp(e, "thinking"))    return AvatarEmotion::Thinking;
    if (!strcmp(e, "winking"))     return AvatarEmotion::Winking;
    if (!strcmp(e, "cool"))        return AvatarEmotion::Cool;
    if (!strcmp(e, "relaxed"))     return AvatarEmotion::Relaxed;
    if (!strcmp(e, "delicious"))   return AvatarEmotion::Delicious;
    if (!strcmp(e, "kissy"))       return AvatarEmotion::Kissy;
    if (!strcmp(e, "confident"))   return AvatarEmotion::Confident;
    if (!strcmp(e, "sleepy"))      return AvatarEmotion::Sleepy;
    if (!strcmp(e, "silly"))       return AvatarEmotion::Silly;
    if (!strcmp(e, "confused"))    return AvatarEmotion::Confused;
    return AvatarEmotion::Neutral;
}

AvatarOverlay AvatarView::OverlayFor(const char* e)
{
    AvatarOverlay o;
    if (!e) return o;
    if (!strcmp(e, "crying"))      o.tear = true;
    if (!strcmp(e, "loving"))      o.heart_eyes = true;
    if (!strcmp(e, "kissy"))       o.kiss_heart = true;
    if (!strcmp(e, "embarrassed")) o.cheek_blush = true;
    if (!strcmp(e, "cool"))        o.cool_glasses = true;
    if (!strcmp(e, "shocked"))     o.excl_mark = true;
    if (!strcmp(e, "thinking"))    o.think_bubble = true;
    if (!strcmp(e, "surprised"))   o.star_burst = true;
    if (!strcmp(e, "delicious"))   o.drool = true;
    if (!strcmp(e, "confused"))    o.question_mark = true;
    if (!strcmp(e, "sleepy"))      o.zzz = true;
    return o;
}

// ---------------------------------------------------------------- 动画驱动

void AvatarView::TimerCb(lv_timer_t* t)
{
    static_cast<AvatarView*>(lv_timer_get_user_data(t))->OnTick();
}

void AvatarView::OnTick()
{
    _tick_count++;
    const uint32_t now = lv_tick_get();

    UpdateBreathParams();
    if (_breath_paused) {
        _breath = 0;
    } else {
        _breath = sinf((_tick_count % _breath_period_steps) * 2.0f * 3.14159265f / _breath_period_steps);
    }

    if (BlinkAllowed()) {
        if (now >= _next_blink_ms) {
            const uint32_t mult = SlowBlink() ? 2 : 1;
            if (_eye_closed) {
                _eye_open_ratio = 1.0f;
                _next_blink_ms = now + mult * (2500 + (rand() % 2000));
                _eye_closed = false;
            } else {
                _eye_open_ratio = 0.0f;
                _next_blink_ms = now + 150 + (rand() % 200);
                _eye_closed = true;
            }
        }
    } else {
        _eye_open_ratio = 1.0f;
        _eye_closed = false;
    }

    if (SaccadeEnabled() && now - _last_saccade_ms > 1500) {
        _gaze_h = (rand() % 21 - 10) / 10.0f;
        _gaze_v = (rand() % 21 - 10) / 10.0f;
        _last_saccade_ms = now;
    }

    const bool speaking = (_speaking_until_ms != 0 && now < _speaking_until_ms);
    if (!speaking && _speaking_until_ms != 0) _speaking_until_ms = 0;
    if (speaking) {
        _mouth_open = 0.2f + (rand() % 80) / 100.0f;
    } else {
        _mouth_open = 0.0f;
    }

    Draw();
}

// ---------------------------------------------------------------- 绘制

void AvatarView::Draw()
{
    if (!_canvas) return;

    lv_canvas_fill_bg(_canvas, kBlack, LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(_canvas, &layer);

    DrawMouth(&layer, kWhite, kBlack);
    DrawEye(&layer, kWhite, kBlack, false);
    DrawEye(&layer, kWhite, kBlack, true);
    DrawOverlay(&layer, kWhite, kBlack);

    lv_canvas_finish_layer(_canvas, &layer);
}

void AvatarView::UpdateBreathParams()
{
    _breath_amp = 3.0f;
    _breath_period_steps = 100;
    _breath_paused = false;
    switch (_emotion) {
        case AvatarEmotion::Relaxed:
            _breath_amp = 7.0f;
            _breath_period_steps = 160;
            break;
        case AvatarEmotion::Shocked:
            _breath_paused = true;
            break;
        default: break;
    }
}

bool AvatarView::BlinkAllowed() const
{
    switch (_emotion) {
        case AvatarEmotion::Cool:
        case AvatarEmotion::Confident:
        case AvatarEmotion::Shocked:
        case AvatarEmotion::Winking:
        case AvatarEmotion::Kissy:
            return false;
        default:
            return true;
    }
}

bool AvatarView::SlowBlink() const
{
    return _emotion == AvatarEmotion::Thinking || _emotion == AvatarEmotion::Relaxed;
}

bool AvatarView::SaccadeEnabled() const
{
    switch (_emotion) {
        case AvatarEmotion::Cool:
        case AvatarEmotion::Confident:
        case AvatarEmotion::Shocked:
        case AvatarEmotion::Thinking:
        case AvatarEmotion::Embarrassed:
        case AvatarEmotion::Winking:
            return false;
        default:
            return true;
    }
}

void AvatarView::GetGazeOverride(float* gh, float* gv) const
{
    switch (_emotion) {
        case AvatarEmotion::Thinking:
            *gh = 0; *gv = -1.0f; break;
        case AvatarEmotion::Embarrassed:
            *gh = 0; *gv = 0.7f; break;
        default:
            *gh = _gaze_h; *gv = _gaze_v;
    }
}

// ---------------------------------------------------------------- 绘制原语（坐标自动缩放）

void AvatarView::FillRect(lv_layer_t* layer, int x, int y, int w, int h, lv_color_t c)
{
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = 0;
    d.border_width = 0;
    lv_area_t a = {sx(x), sy(y), sx(x) + sx(w) - 1, sy(y) + sy(h) - 1};
    lv_draw_rect(layer, &d, &a);
}

void AvatarView::FillCircle(lv_layer_t* layer, int cx, int cy, int r, lv_color_t c)
{
    if (r <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = 0;
    const int rr = sx(r);
    const int xx = sx(cx);
    const int yy = sy(cy);
    lv_area_t a = {xx - rr, yy - rr, xx + rr - 1, yy + rr - 1};
    lv_draw_rect(layer, &d, &a);
}

void AvatarView::FillTriangle(lv_layer_t* layer, int x0, int y0, int x1, int y1, int x2, int y2, lv_color_t c)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.p[0].x = (float)sx(x0); d.p[0].y = (float)sy(y0);
    d.p[1].x = (float)sx(x1); d.p[1].y = (float)sy(y1);
    d.p[2].x = (float)sx(x2); d.p[2].y = (float)sy(y2);
    d.color = c;
    d.opa = LV_OPA_COVER;
    lv_draw_triangle(layer, &d);
}

void AvatarView::FillRoundRect(lv_layer_t* layer, int x, int y, int w, int h, int radius, lv_color_t c)
{
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = sx(radius);
    d.border_width = 0;
    lv_area_t a = {sx(x), sy(y), sx(x) + sx(w) - 1, sy(y) + sy(h) - 1};
    lv_draw_rect(layer, &d, &a);
}

void AvatarView::DrawArc(lv_layer_t* layer, int cx, int cy, int r, int start_deg, int end_deg, int width, lv_color_t c, bool rounded)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    d.width = sx(width);
    d.center.x = sx(cx);
    d.center.y = sy(cy);
    d.radius = sx(r);
    d.start_angle = start_deg;
    d.end_angle = end_deg;
    d.rounded = rounded ? 1 : 0;
    lv_draw_arc(layer, &d);
}

void AvatarView::DrawLine(lv_layer_t* layer, int x1, int y1, int x2, int y2, int width, bool round, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    d.width = sx(width);
    d.round_start = round ? 1 : 0;
    d.round_end = round ? 1 : 0;
    d.p1.x = (float)sx(x1); d.p1.y = (float)sy(y1);
    d.p2.x = (float)sx(x2); d.p2.y = (float)sy(y2);
    lv_draw_line(layer, &d);
}

// ---------------------------------------------------------------- 嘴

void AvatarView::DrawMouth(lv_layer_t* layer, lv_color_t fg, lv_color_t bg)
{
    const int cx = 160;
    const int cy = 195 + (int)(_breath * 3.0f);  // 下移拉开与眼睛距离（原 148）
    const int y_off = (int)(_breath * 2.0f);

    switch (_emotion) {
        case AvatarEmotion::Cool:
            DrawLine(layer, cx - 12, cy + y_off + 2, cx + 12, cy + y_off, 3, true, fg);
            return;
        case AvatarEmotion::Confident:
            DrawLine(layer, cx - 14, cy + y_off + 3, cx + 14, cy + y_off, 3, true, fg);
            return;
        case AvatarEmotion::Silly:
            DrawLine(layer, cx - 13, cy + y_off + 4, cx + 13, cy + y_off, 3, true, fg);
            return;
        case AvatarEmotion::Embarrassed:
            DrawLine(layer, cx - 12, cy + y_off, cx + 12, cy + y_off, 3, true, fg);
            return;
        case AvatarEmotion::Kissy: {
            const int my = cy + y_off;
            DrawArc(layer, cx, my - 6, 6, 270, 450, 3, fg, false);
            DrawArc(layer, cx, my + 6, 6, 270, 450, 3, fg, false);
            FillCircle(layer, cx, my, 2, fg);
            return;
        }
        case AvatarEmotion::Winking:
            DrawArc(layer, cx, cy + y_off - 5, 12, 0, 180, 3, fg);
            return;
        case AvatarEmotion::Laughing: {
            const int y_top = cy + y_off - 28;
            FillRoundRect(layer, cx - 40, y_top, 80, 30, 12, fg);
            return;
        }
        case AvatarEmotion::Funny:
            FillRoundRect(layer, cx - 25, cy + y_off - 10, 50, 20, 8, fg);
            return;
        case AvatarEmotion::Relaxed:
            DrawLine(layer, cx - 20, cy + y_off, cx + 20, cy + y_off, 4, true, fg);
            return;
        case AvatarEmotion::Delicious: {
            const int h = 4 + (int)((60 - 4) * 0.3f);
            const int w = 50 + (int)((90 - 50) * 0.7f);
            FillRoundRect(layer, cx - w / 2, cy + y_off - h / 2, w, h, 7, fg);
            return;
        }
        case AvatarEmotion::Shocked: {
            FillRoundRect(layer, cx - 25, cy + y_off - 30, 50, 60, 10, fg);
            return;
        }
        case AvatarEmotion::Surprised: {
            const int h = 4 + (int)((60 - 4) * 0.5f);
            const int w = 50 + (int)((90 - 50) * 0.5f);
            FillRoundRect(layer, cx - w / 2, cy + y_off - h / 2, w, h, 12, fg);
            return;
        }
        case AvatarEmotion::Thinking: {
            DrawLine(layer, cx - 15, cy + y_off, cx + 15, cy + y_off, 3, true, fg);
            return;
        }
        case AvatarEmotion::Confused: {
            DrawLine(layer, cx - 15, cy + y_off, cx + 15, cy + y_off + 2, 3, true, fg);
            return;
        }
        default: {
            const int h = 4 + (int)((60 - 4) * _mouth_open);
            const int w = 50 + (int)((90 - 50) * (1.0f - _mouth_open));
            const int radius = (int)(_mouth_open * 10);
            FillRoundRect(layer, cx - w / 2, cy + y_off - h / 2, w, h, radius, fg);
            return;
        }
    }
}

// ---------------------------------------------------------------- 眼睛

void AvatarView::DrawEye(lv_layer_t* layer, lv_color_t fg, lv_color_t bg, bool is_left)
{
    if (_emotion == AvatarEmotion::Cool) return;

    const int cx_base = is_left ? 250 : 70;  // 眼距拉开（原 230/90）
    const int cy_base_y = is_left ? 115 : 112;  // 下移（原 96/93）
    const int cy = cy_base_y + (int)(_breath * 3.0f);

    float gh, gv;
    GetGazeOverride(&gh, &gv);
    const int off_x = (int)(gh * 3.0f);
    const int off_y = (int)(gv * 3.0f);

    if (_overlay.heart_eyes) {
        const lv_color_t red = lv_color_make(0xFF, 0x40, 0x70);
        const int hcx = cx_base + off_x;
        const int hcy = cy + off_y;
        FillCircle(layer, hcx - 6, hcy - 3, 7, red);
        FillCircle(layer, hcx + 6, hcy - 3, 7, red);
        FillTriangle(layer, hcx - 12, hcy + 1, hcx + 12, hcy + 1, hcx, hcy + 13, red);
        return;
    }

    if (_emotion == AvatarEmotion::Shocked) {
        FillCircle(layer, cx_base, cy, 13, fg);
        FillCircle(layer, cx_base, cy, 3, bg);
        return;
    }

    if (_emotion == AvatarEmotion::Surprised) {
        FillCircle(layer, cx_base, cy, 10, fg);
        return;
    }

    if (_emotion == AvatarEmotion::Confused) {
        const int r = is_left ? 8 : 6;
        FillCircle(layer, cx_base + off_x, cy + off_y, r, fg);
        return;
    }

    if (_emotion == AvatarEmotion::Winking) {
        if (is_left) {
            DrawLine(layer, cx_base + 8, cy - 4, cx_base - 8, cy, 5, true, fg);
            DrawLine(layer, cx_base - 8, cy, cx_base + 8, cy + 4, 5, true, fg);
        } else {
            FillCircle(layer, cx_base, cy, 8, fg);
        }
        return;
    }

    if (_emotion == AvatarEmotion::Silly) {
        const int r = 8;
        FillCircle(layer, cx_base + off_x, cy + off_y, r, fg);
        const int x0 = cx_base + off_x - r;
        const int y0 = cy + off_y;
        const int w = r * 2 + 4;
        int h = r + 2;
        if (!is_left) h += 2;
        FillCircle(layer, cx_base + off_x, cy + off_y, (int)(r / 1.5f), bg);
        FillRect(layer, x0, y0, w, h, bg);
        return;
    }

    if (_emotion == AvatarEmotion::Laughing) {
        const int r = 8;
        FillCircle(layer, cx_base + off_x, cy + off_y, r, fg);
        const int x0 = cx_base + off_x - r - 2;
        const int y0 = cy + off_y;
        const int w = r * 2 + 8;
        const int h = r + 4;
        FillCircle(layer, cx_base + off_x, cy + off_y, (int)(r / 1.5f), bg);
        FillRect(layer, x0, y0, w, h, bg);
        return;
    }

    if (_emotion == AvatarEmotion::Sleepy) {
        if (is_left) {
            DrawLine(layer, cx_base - 8 + off_x, cy - 2 + off_y,
                            cx_base + 8 + off_x, cy + 2 + off_y, 4, true, fg);
        } else {
            DrawLine(layer, cx_base - 8 + off_x, cy + 2 + off_y,
                            cx_base + 8 + off_x, cy - 2 + off_y, 4, true, fg);
        }
        return;
    }

    if (_emotion == AvatarEmotion::Relaxed) {
        const int r = 8;
        FillCircle(layer, cx_base + off_x, cy + off_y, r, fg);
        const int x0 = cx_base + off_x - r - 1;
        const int y0 = cy + off_y - 1;
        const int w = r * 2 + 6;
        const int h = r + 3;
        FillCircle(layer, cx_base + off_x, cy + off_y, (int)(r / 1.5f), bg);
        FillRect(layer, x0, y0, w, h, bg);
        return;
    }

    const int r = 8;

    if (_eye_open_ratio > 0) {
        FillCircle(layer, cx_base + off_x, cy + off_y, r, fg);

        if (_emotion == AvatarEmotion::Angry || _emotion == AvatarEmotion::Sad
            || _emotion == AvatarEmotion::Crying) {
            const int x0 = cx_base + off_x - r;
            const int y0 = cy + off_y - r;
            const int x1 = x0 + r * 2;
            const int y1 = y0;
            const bool sad = (_emotion == AvatarEmotion::Sad || _emotion == AvatarEmotion::Crying);
            const int x2 = ((!is_left) != (!sad)) ? x0 : x1;
            const int y2 = y0 + r;
            FillTriangle(layer, x0, y0, x1, y1, x2, y2, bg);
        }

        if (_emotion == AvatarEmotion::Happy
            || _emotion == AvatarEmotion::Kissy || _emotion == AvatarEmotion::Funny
            || _emotion == AvatarEmotion::Delicious) {
            FillCircle(layer, cx_base + off_x, cy + off_y, r + 2, bg);
            DrawArc(layer, cx_base + off_x, cy + off_y + r,
                    r, 180, 360, 3, fg, true);
        }
    } else {
        FillRect(layer, cx_base - r + off_x, cy - 2 + off_y, r * 2, 4, fg);
    }
}

// ---------------------------------------------------------------- 装饰层

void AvatarView::DrawOverlay(lv_layer_t* layer, lv_color_t fg, lv_color_t bg)
{
    if (_overlay.tear) {
        const lv_color_t blue = lv_color_make(0x40, 0xA0, 0xFF);
        const int tx = 70;  // 跟随左眼（原 90）
        const int ty = 135 + (int)(_breath * 3.0f);  // 跟随眼睛下移（原 115）
        FillCircle(layer, tx, ty, 7, blue);
        FillTriangle(layer, tx - 6, ty - 2, tx + 6, ty - 2, tx, ty - 15, blue);
    }

    if (_overlay.cheek_blush) {
        const lv_color_t pink = lv_color_make(0xFF, 0x64, 0x82);
        for (int i = 0; i < 3; i++) {
            const int x_start = 30 + i * 8;  // 跟随眼距（原 47）
            const int x_end = x_start + 6;
            DrawLine(layer, x_start, 170, x_end, 162, 3, true, pink);  // 下移（原 138/130）
        }
        for (int i = 0; i < 3; i++) {
            const int x_start = 270 + i * 8;  // 跟随右眼（原 251）
            const int x_end = x_start + 6;
            DrawLine(layer, x_start, 170, x_end, 162, 3, true, pink);
        }
    }

    if (_overlay.cool_glasses) {
        FillRoundRect(layer, 70, 105, 50, 24, 5, fg);   // 跟随眼睛（原 85/84）
        FillRoundRect(layer, 230, 105, 50, 24, 5, fg);  // 原 185/84
        DrawLine(layer, 70, 105, 280, 105, 2, false, fg);  // 原 85-235
    }

    if (_overlay.excl_mark) {
        DrawLine(layer, 320, 60, 320, 78, 4, true, fg);  // 右上（原 291/50）
        FillCircle(layer, 320, 86, 2, fg);               // 原 76
    }

    if (_overlay.think_bubble) {
        FillRoundRect(layer, 270, 55, 50, 25, 12, fg);   // 右上（原 245/47）
        FillCircle(layer, 283, 68, 3, bg);
        FillCircle(layer, 295, 68, 3, bg);
        FillCircle(layer, 307, 68, 3, bg);
        FillCircle(layer, 298, 93, 6, fg);
        FillCircle(layer, 283, 118, 4, fg);
    }

    if (_overlay.star_burst) {
        const int cx_s = 320, cy_s = 70;  // 右上（原 290/60）
        FillRect(layer, cx_s - 3, cy_s - 3, 6, 6, fg);
        FillTriangle(layer, cx_s, cy_s - 18, cx_s - 3, cy_s - 3, cx_s + 3, cy_s - 3, fg);
        FillTriangle(layer, cx_s, cy_s + 18, cx_s - 3, cy_s + 3, cx_s + 3, cy_s + 3, fg);
        FillTriangle(layer, cx_s - 18, cy_s, cx_s - 3, cy_s - 3, cx_s - 3, cy_s + 3, fg);
        FillTriangle(layer, cx_s + 18, cy_s, cx_s + 3, cy_s - 3, cx_s + 3, cy_s + 3, fg);
    }

    if (_overlay.wave_squiggle) {
        DrawLine(layer, 148, 35, 154, 31, 2, true, fg);  // 头顶居中（原 28）
        DrawLine(layer, 154, 31, 160, 35, 2, true, fg);
        DrawLine(layer, 160, 35, 166, 31, 2, true, fg);
        DrawLine(layer, 166, 31, 172, 35, 2, true, fg);
    }

    if (_overlay.drool) {
        const lv_color_t blue = lv_color_make(0x40, 0xA0, 0xFF);
        const int dx = 160;  // 嘴下方（原 143）
        const int dy = 205 + (int)(_breath * 3.0f);  // 跟随嘴（原 168）
        FillCircle(layer, dx, dy, 4, blue);
        FillTriangle(layer, dx - 3, dy - 2, dx + 3, dy - 2, dx, dy - 8, blue);
    }

    if (_overlay.laugh_lines) {
        DrawLine(layer, 230, 195, 240, 187, 3, true, fg);  // 嘴两侧（原 210/150）
        DrawLine(layer, 238, 201, 248, 193, 3, true, fg);  // 原 218/156
    }

    if (_overlay.question_mark) {
        DrawArc(layer, 320, 60, 7, 180, 90, 4, fg, true);  // 右上（原 290/50）
        FillCircle(layer, 320, 77, 3, fg);                  // 原 67
    }

    if (_overlay.zzz) {
        auto draw_z = [&](int cx_z, int cy_z, int size, int w) {
            const int h = size / 2;
            DrawLine(layer, cx_z - h, cy_z - h, cx_z + h, cy_z - h, w, false, fg);
            DrawLine(layer, cx_z + h, cy_z - h, cx_z - h, cy_z + h, w, false, fg);
            DrawLine(layer, cx_z - h, cy_z + h, cx_z + h, cy_z + h, w, false, fg);
        };
        draw_z(285, 90, 8, 2);   // 右上（原 258/80）
        draw_z(300, 80, 10, 3);  // 原 270/70
        draw_z(318, 65, 14, 3);  // 原 286/55
    }

    if (_overlay.kiss_heart) {
        const lv_color_t red = lv_color_make(0xFF, 0x40, 0x70);
        const int hx = 160;  // 嘴上方（原 195）
        const int hy = 175;  // 原 130
        FillCircle(layer, hx - 3, hy - 1, 4, red);
        FillCircle(layer, hx + 3, hy - 1, 4, red);
        FillTriangle(layer, hx - 6, hy + 1, hx + 6, hy + 1, hx, hy + 8, red);
    }
}

}  // namespace app_ai_chat
