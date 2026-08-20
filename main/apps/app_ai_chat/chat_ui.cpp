/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "chat_ui.h"

#include <assets/assets.h>
#include <cstring>

namespace app_ai_chat {

namespace {
constexpr lv_color_t kColorWhite = {0xFF, 0xFF, 0xFF};
constexpr lv_color_t kColorGray  = {0x6B, 0x76, 0x86};

// 过滤字体不支持的字符（emoji/非 BMP 等，否则显示方块）。
// 保留：CJK 基本区/扩展 A、ASCII、常用标点；剔除 emoji（U+1F300+）与
// 杂项符号（U+2600-27BF）、变体选择符（U+FE00-FE0F）等
std::string strip_unsupported(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        uint32_t cp = 0;
        size_t len  = 0;
        uint8_t b   = static_cast<uint8_t>(in[i]);
        if (b < 0x80) {
            cp  = b;
            len = 1;
        } else if ((b >> 5) == 0x6) {
            cp = (b & 0x1F) << 6;
            if (i + 1 < in.size()) cp |= (static_cast<uint8_t>(in[i + 1]) & 0x3F);
            len = 2;
        } else if ((b >> 4) == 0xE) {
            cp = (b & 0x0F) << 12;
            if (i + 2 < in.size()) {
                cp |= (static_cast<uint8_t>(in[i + 1]) & 0x3F) << 6;
                cp |= (static_cast<uint8_t>(in[i + 2]) & 0x3F);
            }
            len = 3;
        } else if ((b >> 3) == 0x1E) {
            cp = (b & 0x07) << 18;
            if (i + 3 < in.size()) {
                cp |= (static_cast<uint8_t>(in[i + 1]) & 0x3F) << 12;
                cp |= (static_cast<uint8_t>(in[i + 2]) & 0x3F) << 6;
                cp |= (static_cast<uint8_t>(in[i + 3]) & 0x3F);
            }
            len = 4;
        } else {
            len = 1;
        }
        const bool keep =
            cp >= 0x20 && cp != 0x7F &&                                   // 可打印 ASCII
            !(cp >= 0x2600 && cp <= 0x27BF) &&                            // 杂项符号/装饰（☀☎✈✏…）
            !(cp >= 0x1F000 && cp <= 0x1FFFF) &&                          // emoji 区
            !(cp >= 0xFE00 && cp <= 0xFE0F) &&                            // 变体选择符
            !(cp >= 0xE000 && cp <= 0xF8FF);                              // 私用区
        if (keep) {
            out.append(in, i, len);
        }
        i += len;
    }
    return out;
}
}

ChatUi::ChatUi()
{
    _root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, 466, 466);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // 表情 Avatar（移植自 stackchan-newstep shizhou_avatar）：360 画布居中偏上
    // （240 时表情元素挤在中间一块；360 + 拉开眼/嘴布局后占满屏幕上部）
    _avatar = std::make_unique<AvatarView>(_root, 360, 360);
    if (_avatar->IsReady()) {
        lv_obj_align(_avatar->get(), LV_ALIGN_CENTER, 0, -40);
    }

    _status_label = lv_label_create(_root);
    lv_obj_align(_status_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(_status_label, "");
    lv_obj_set_style_text_font(_status_label, &lv_font_maple_mono_medium_28, 0);
    lv_obj_set_style_text_color(_status_label, kColorWhite, 0);

    _message_label = lv_label_create(_root);
    lv_obj_align(_message_label, LV_ALIGN_CENTER, 0, 150);
    lv_obj_set_width(_message_label, 380);
    lv_obj_set_style_text_align(_message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_message_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_message_label, "");
    lv_obj_set_style_text_font(_message_label, &lv_font_maple_mono_medium_24, 0);
    lv_obj_set_style_text_color(_message_label, kColorGray, 0);

    // 激活码屏（Activating 时显示）
    _code_label = lv_label_create(_root);
    lv_obj_align(_code_label, LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(_code_label, "");
    lv_obj_set_style_text_font(_code_label, &CommissionerMedium64, 0);
    lv_obj_set_style_text_color(_code_label, kColorWhite, 0);

    _hint_label = lv_label_create(_root);
    lv_obj_align(_hint_label, LV_ALIGN_CENTER, 0, 90);
    lv_obj_set_width(_hint_label, 380);
    lv_obj_set_style_text_align(_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_hint_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(_hint_label, "");
    lv_obj_set_style_text_font(_hint_label, &lv_font_maple_mono_medium_24, 0);
    lv_obj_set_style_text_color(_hint_label, kColorGray, 0);
}

ChatUi::~ChatUi()
{
    if (_root) {
        lv_obj_delete(_root);
    }
}

void ChatUi::setVisible(lv_obj_t* obj, bool visible)
{
    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void ChatUi::update(const UiSnapshot& snap)
{
    if (snap.revision == _last_revision) {
        return;
    }
    _last_revision = snap.revision;
    _last_snap = snap;

    const bool activating = (snap.state == ChatState::Activating);
    setVisible(_code_label, activating);
    setVisible(_hint_label, activating);
    setVisible(_message_label, !activating);
    if (_avatar && _avatar->IsReady()) {
        setVisible(_avatar->get(), !activating);
    }

    if (activating) {
        if (!snap.activation_code.empty()) {
            lv_label_set_text(_code_label, snap.activation_code.c_str());
            lv_label_set_text(_hint_label, "请在 xiaozhi.me 网页端输入此码绑定设备");
        } else {
            lv_label_set_text(_code_label, "");
            lv_label_set_text(_hint_label, snap.message.c_str());
        }
    } else {
        // 表情映射（移植自 stackchan-newstep：llm.emotion 直通 MapEmotion + OverlayFor）
        applyEmotionLocked(snap);

        // 状态文案
        const char* status = "";
        switch (snap.state) {
            case ChatState::Idle:       status = "AI 对话"; break;
            case ChatState::Connecting: status = "连接中…"; break;
            case ChatState::Listening:  status = "聆听中…"; break;
            case ChatState::Speaking:   status = "播报中…"; break;
            case ChatState::Error:      status = "出错了"; break;
            case ChatState::Activating: status = "激活中…"; break;
        }
        lv_label_set_text(_status_label, status);

        // 消息区：错误优先，其次最新文本（过滤 emoji/非 BMP，否则显示方块）
        if (!snap.error.empty()) {
            lv_label_set_text(_message_label, snap.error.c_str());
            lv_obj_set_style_text_color(_message_label, lv_color_hex(0xFF5555), 0);
        } else {
            const std::string clean = strip_unsupported(snap.message);
            lv_label_set_text(_message_label, clean.c_str());
            lv_obj_set_style_text_color(_message_label, kColorGray, 0);
        }
    }
}

void ChatUi::applyEmotion()
{
    if (_last_snap.revision == 0) {
        return;
    }
    applyEmotionLocked(_last_snap);
}

void ChatUi::showTransientEmotion(const char* emotion, const AvatarOverlay& extra)
{
    _avatar->setEmotion(emotion, extra);
}

void ChatUi::applySleepy()
{
    if (_avatar && _avatar->IsReady()) {
        _avatar->setEmotion("sleepy");  // 字符串路径自带 zzz Overlay
    }
}

void ChatUi::applyEmotionLocked(const UiSnapshot& snap)
{
    // 说话嘴型：Speaking 期间按文本长度估算播报时长（与 stackchan-newstep
    // SetChatMessage 一致：len*120ms，clamp 800~15000）；其余状态停止嘴型
    if (snap.state == ChatState::Speaking) {
        const size_t n = snap.message.size();
        uint32_t ms = (uint32_t)(n * 120);
        if (ms < 800) ms = 800;
        if (ms > 15000) ms = 15000;
        _avatar->startSpeaking(ms);
    } else {
        _avatar->stopSpeaking();
    }

    // 表情：llm.emotion 直通服务器语义；无 emotion 时按状态机兜底
    if (!snap.emotion.empty()) {
        _avatar->setEmotion(snap.emotion.c_str());
    } else if (snap.state == ChatState::Error) {
        _avatar->setEmotion("angry");
    } else {
        _avatar->setEmotion("neutral");
    }
}

}  // namespace app_ai_chat
