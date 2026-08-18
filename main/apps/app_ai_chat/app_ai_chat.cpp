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
#include <esp_random.h>
#include <cmath>

using namespace mooncake;

namespace {

// 摇晃互动语料表（P2）：随机注入一条触发 AI 回应
constexpr const char* const kShakePhrases[] = {
    "你好呀",
    "讲个笑话吧",
    "给我唱首歌",
    "今天天气怎么样",
    "讲个故事听听",
    "你现在心情怎么样",
    "推荐一首好听的歌",
    "夸夸我",
    "说个绕口令",
    "你现在在哪里",
    "周末去哪里玩好",
    "推荐一部电影",
    "怎么才能保持健康",
    "介绍一下你自己",
    "什么是快乐",
    "世界有多大",
};
constexpr size_t kShakePhraseCount = sizeof(kShakePhrases) / sizeof(kShakePhrases[0]);

// 摇晃阈值（|ax|+|ay|+|az|，静止约 1.0~1.2g；与 app_dice 一致）与 10s 冷却
constexpr float kShakeThreshold   = 1.8f;
constexpr uint32_t kShakeCooldownMs = 10000;

}  // namespace

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

    // 嘴型动画帧驱动（内部 100ms 节流，非 Speaking 直接返回）
    {
        LvglLockGuard lock;
        _ui->tick(GetHAL().millis());
    }

    // 摇晃互动
    checkShake();
}

void AppAiChat::checkShake()
{
    GetHAL().updateImuData();
    const auto& imu = GetHAL().getImuData();
    const float mag = std::abs(imu.accelX) + std::abs(imu.accelY) + std::abs(imu.accelZ);
    const uint32_t now = GetHAL().millis();
    if (mag > kShakeThreshold && now - _last_shake_ms > kShakeCooldownMs) {
        _last_shake_ms = now;
        const char* phrase = kShakePhrases[esp_random() % kShakePhraseCount];
        mclog::tagInfo(getAppInfo().name, "shake -> inject: {}", phrase);
        _engine->injectDetectedText(phrase);
    }
}

void AppAiChat::onClose()
{
    _engine->stop();
    framework::TouchPad::get().setEventCallback(nullptr);

    LvglLockGuard lock;
    _ui.reset();
}
