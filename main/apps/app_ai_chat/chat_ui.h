/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智 AI 对话界面（阶段三 P1：状态提示 + 激活码屏；P2 扩展表情/气泡）
 */
#pragma once

#include "chat_engine.h"
#include <lvgl.h>
#include <memory>

namespace app_ai_chat {

class ChatUi {
public:
    ChatUi();
    ~ChatUi();

    /** 仅在持有 LvglLockGuard 时调用（App onRunning 内） */
    void update(const UiSnapshot& snapshot);

private:
    void setVisible(lv_obj_t* obj, bool visible);

    lv_obj_t* _root       = nullptr;
    lv_obj_t* _status_label = nullptr;
    lv_obj_t* _message_label = nullptr;
    lv_obj_t* _code_label = nullptr;
    lv_obj_t* _hint_label = nullptr;

    uint64_t _last_revision = 0;
};

}  // namespace app_ai_chat
