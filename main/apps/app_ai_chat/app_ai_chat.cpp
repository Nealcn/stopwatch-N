/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_ai_chat.h"

#include <hal/hal.h>
#include <mooncake_log.h>
#include <framework/touch_pad/touch_pad.h>
#include <assets/assets.h>

using namespace mooncake;

AppAiChat::AppAiChat()
    : _engine(std::make_unique<app_ai_chat::ChatEngine>())
{
    setAppInfo().name = "AI对话";
    setAppInfo().icon = (void*)&icon_ai_chat;
}

void AppAiChat::onCreate()
{
    _key_manager = std::make_unique<input::KeyManager>();
}

void AppAiChat::onOpen()
{
    LvglLockGuard lock;

    _ui = std::make_unique<app_ai_chat::ChatUi>();

    // 触摸轻点 = 对话开关（复用 framework/touch_pad 手势识别；
    // onClose 会清回调，lambda 捕获 this 安全）
    framework::TouchPad::get().setEventCallback([this](const framework::TouchPadEvent& ev) {
        if (ev.type == framework::TouchPadEvent::ButtonUp && ev.button == 1) {
            _engine->onUserStart();
        }
    });

    _engine->start();
}

void AppAiChat::onRunning()
{
    // 返回 launcher（A+B）
    if (_key_manager->update() == input::KeyEvent::GoHome) {
        _engine->stop();
        close();
        return;
    }

    // hold-to-talk：按住 B 说话，松开结束（manual 模式）
    bool holding = GetHAL().btnB.isHolding();
    if (holding && !_btnb_holding) {
        _btnb_holding = true;
        _engine->onUserStart();
    } else if (!holding && _btnb_holding) {
        _btnb_holding = false;
        _engine->onUserStop();
    }
    // 单击 A = 对话开关（auto 模式）
    if (GetHAL().btnA.wasClicked()) {
        _engine->onUserStart();
    }

    // UI 刷新（revision 变化才重绘）
    uint64_t rev = _engine->revision();
    if (rev != _last_revision) {
        _last_revision = rev;
        LvglLockGuard lock;
        _ui->update(_engine->snapshot());
    }
}

void AppAiChat::onClose()
{
    _engine->stop();
    framework::TouchPad::get().setEventCallback(nullptr);

    LvglLockGuard lock;
    _ui.reset();
}
