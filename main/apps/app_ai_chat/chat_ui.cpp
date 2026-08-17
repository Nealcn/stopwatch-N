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
}

ChatUi::ChatUi()
{
    _root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, 466, 466);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    _status_label = lv_label_create(_root);
    lv_obj_align(_status_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(_status_label, "");
    lv_obj_set_style_text_font(_status_label, &lv_font_maple_mono_medium_28, 0);
    lv_obj_set_style_text_color(_status_label, kColorWhite, 0);

    _message_label = lv_label_create(_root);
    lv_obj_align(_message_label, LV_ALIGN_CENTER, 0, 40);
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
    lv_obj_add_flag(obj, visible ? LV_OBJ_FLAG_NONE : LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(obj, visible ? LV_OBJ_FLAG_HIDDEN : LV_OBJ_FLAG_NONE);
}

void ChatUi::update(const UiSnapshot& snap)
{
    if (snap.revision == _last_revision) {
        return;
    }
    _last_revision = snap.revision;

    const bool activating = (snap.state == ChatState::Activating);
    setVisible(_code_label, activating);
    setVisible(_hint_label, activating);
    setVisible(_message_label, !activating);

    if (activating) {
        if (!snap.activation_code.empty()) {
            lv_label_set_text(_code_label, snap.activation_code.c_str());
            lv_label_set_text(_hint_label, "请在 xiaozhi.me 网页端输入此码绑定设备");
        } else {
            lv_label_set_text(_code_label, "");
            lv_label_set_text(_hint_label, snap.message.c_str());
        }
    } else {
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

        // 消息区：错误优先，其次最新文本
        if (!snap.error.empty()) {
            lv_label_set_text(_message_label, snap.error.c_str());
            lv_obj_set_style_text_color(_message_label, lv_color_hex(0xFF5555), 0);
        } else {
            lv_label_set_text(_message_label, snap.message.c_str());
            lv_obj_set_style_text_color(_message_label, kColorGray, 0);
        }
    }
}

}  // namespace app_ai_chat
