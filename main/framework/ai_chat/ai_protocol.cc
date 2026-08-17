/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智（xiaozhi）语音协议消息构造（移植自 Stackchan-Newstep protocol.cc）
 */
#include "ai_protocol.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cstdio>

#define TAG "ai_protocol"

namespace framework {
namespace aichat {

namespace {
constexpr int kTimeoutSeconds = 120;
}

void Protocol::SendDetectedText(const std::string& text)
{
    std::string json = "{\"session_id\":\"" + session_id_ +
                       "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + text + "\"}";
    SendText(json);
}

void Protocol::SendStartListening(ListeningMode mode)
{
    std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";
    if (mode == kListeningModeRealtime) {
        message += ",\"mode\":\"realtime\"";
    } else if (mode == kListeningModeAutoStop) {
        message += ",\"mode\":\"auto\"";
    } else {
        message += ",\"mode\":\"manual\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendStopListening()
{
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"listen\",\"state\":\"stop\"}";
    SendText(message);
}

void Protocol::SendAbortSpeaking(AbortReason reason)
{
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
    if (reason == kAbortReasonWakeWordDetected) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendMcpMessage(const std::string& payload)
{
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    SendText(message);
}

bool Protocol::IsTimeout() const
{
    if (last_incoming_time_us_ == 0) {
        return false;
    }
    uint64_t now_us = esp_timer_get_time();
    bool timeout    = (now_us - last_incoming_time_us_) > (uint64_t)kTimeoutSeconds * 1000 * 1000;
    if (timeout) {
        ESP_LOGE(TAG, "Channel timeout %d seconds", kTimeoutSeconds);
    }
    return timeout;
}

void Protocol::SetError(const std::string& message)
{
    error_occurred_ = true;
    if (on_network_error_) {
        on_network_error_(message);
    }
}

}  // namespace aichat
}  // namespace framework
