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
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<app_ai_chat::ChatEngine> _engine;
    std::unique_ptr<app_ai_chat::ChatUi> _ui;

    bool _btnb_holding = false;
    uint64_t _last_revision = 0;
};
