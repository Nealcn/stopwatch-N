/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智 AI 对话界面（阶段三 P1：状态提示 + 激活码屏；P2 扩展表情/气泡）
 */
#pragma once

#include "avatar_view.h"
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
    /** 按最近一次快照重算表情（触摸/摇晃临时表情超时后恢复用） */
    void applyEmotion();
    /** 临时表情（触摸抚摸/摇晃触发；超时由 App 调 applyEmotion 恢复） */
    void showTransientEmotion(const char* emotion, const AvatarOverlay& extra);
    /** 睡眠表情（对齐 stackchan SetPowerSaveMode→"sleepy"：长闲置显示闭眼+zzz，
     *  不受 3s 临时表情定时恢复影响，直到 App 调 applyEmotion） */
    void applySleepy();

private:
    void setVisible(lv_obj_t* obj, bool visible);
    /** 表情 + 说话嘴型逻辑（update 与 applyEmotion 共用） */
    void applyEmotionLocked(const UiSnapshot& snap);

    lv_obj_t* _root       = nullptr;
    lv_obj_t* _status_label = nullptr;
    lv_obj_t* _message_label = nullptr;
    lv_obj_t* _code_label = nullptr;
    lv_obj_t* _hint_label = nullptr;
    std::unique_ptr<AvatarView> _avatar;

    uint64_t _last_revision = 0;
    UiSnapshot _last_snap;
};

}  // namespace app_ai_chat
