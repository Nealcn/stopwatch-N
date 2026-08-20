/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <memory>
#include <apps/common/key_manager/key_manager.h>

#include "chat_engine.h"
#include "chat_ui.h"

/**
 * @brief 小智 AI 语音对话（阶段三 P1）
 *
 * 云端 AI 对话：触摸/按键唤醒（无离线唤醒词），xiaozhi.me 官方服务器，
 * websocket 协议 v2（Opus 16k/60ms 上行，TTS 下行播放）。
 * 交互：
 *  - 按住 B 键说话（hold-to-talk，manual 模式）
 *  - 点触屏幕 / 单击 A 键：对话开关（auto 模式，服务器 VAD 判停）
 *  - 播报中触发 = 打断（abort）
 *  - A+B = 返回 launcher
 */
class AppAiChat : public mooncake::AppAbility {
public:
    AppAiChat();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void checkShake();          // 摇晃互动：Shocked 表情 + 语料注入（10s 冷却）
    void onPetted();            // 触摸长按抚摸：Loving 表情 + 语料注入（对应 stackchan OnPetted）
    void showTransientEmotion(const char* emotion, const app_ai_chat::AvatarOverlay& extra,
                              uint32_t duration_ms);
    void checkTransientEmotion();  // 临时表情超时 → 恢复状态机表情
    void injectPhrase(const char* phrase);

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<app_ai_chat::ChatEngine> _engine;
    std::unique_ptr<app_ai_chat::ChatUi> _ui;

    bool _btnb_holding = false;
    uint64_t _last_revision = 0;
    uint32_t _last_shake_ms = 0;
    uint32_t _transient_until_ms = 0;
    // 空闲睡眠表情：Idle 且超过 kIdleSleepyMs 无用户输入 → 显示 sleepy（闭眼+zzz），
    // 有输入恢复 neutral（对齐 stackchan SetPowerSaveMode）
    static constexpr uint32_t kIdleSleepyMs = 45000;
    uint32_t _last_user_input_ms = 0;
    bool _sleepy_shown = false;
    // AI 对话期间禁用按键提示音（20ms 高音 = 用户听到的"滴滴声"，
    // 且与语音播放/录音冲突），onClose 恢复原配置
    Hal::ButtonConfig _saved_btn_cfg{};
};
