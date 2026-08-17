/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智 WebSocket 语音协议（移植自 Stackchan-Newstep websocket_protocol.cc）
 */
#pragma once

#include "ai_protocol.h"

#include <web_socket.h>
#include <network_interface.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <memory>

namespace framework {
namespace aichat {

#define AI_WS_SERVER_HELLO_EVENT (1 << 0)

class AiWebsocketProtocol : public Protocol {
public:
    explicit AiWebsocketProtocol(NetworkInterface* network);
    ~AiWebsocketProtocol() override;

    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;

private:
    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();
    void ParseServerHello(const JsonDocument& doc);

    NetworkInterface* network_;
    std::unique_ptr<WebSocket> websocket_;
    EventGroupHandle_t event_group_handle_ = nullptr;
    int version_ = 1;
};

}  // namespace aichat
}  // namespace framework
