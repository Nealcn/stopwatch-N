/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智（xiaozhi）MQTT 语音协议（移植自官方 xiaozhi-esp32 mqtt_protocol.cc）
 *
 * 服务器主通道已切换为 MQTT（websocket 返回 426 Upgrade Required 已废弃）：
 *  - MQTT 信令：hello / JSON 消息（OTA 响应 mqtt 段：endpoint/client_id/username/password/topics）
 *  - 音频上行：AES-CTR 加密（16B nonce 头）+ UDP
 *  - 音频下行：UDP 接收（16B 头：type/flags/len/ssrc/ts/seq）+ AES-CTR 解密
 * 加密用 mbedtls（PSA 在 ESP-IDF 需额外配置）
 */
#pragma once

#include "ai_protocol.h"

#include <mqtt.h>
#include <udp.h>
#include <network_interface.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <mbedtls/aes.h>
#include <memory>
#include <mutex>

namespace framework {
namespace aichat {

#define AI_MQTT_SERVER_HELLO_EVENT (1 << 0)

class AiMqttProtocol : public Protocol {
public:
    explicit AiMqttProtocol(NetworkInterface* network);
    ~AiMqttProtocol() override;

    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;

private:
    bool StartMqttClient();
    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();
    void ParseServerHello(const JsonDocument& doc);
    bool DecodeHexString(const std::string& hex_string, std::string& decoded);
    bool CryptAesCtr(const uint8_t* input, size_t input_size, const uint8_t* nonce,
                     uint8_t* output);

    NetworkInterface* network_;
    std::unique_ptr<Mqtt> mqtt_;
    std::unique_ptr<Udp> udp_;
    EventGroupHandle_t event_group_handle_ = nullptr;

    std::string publish_topic_;
    std::string subscribe_topic_;
    std::string udp_server_;
    int udp_port_ = 0;
    std::string aes_nonce_;
    uint8_t aes_key_[16] = {};
    bool aes_ready_ = false;

    mutable std::mutex channel_mutex_;
    std::mutex crypto_mutex_;

    uint32_t local_sequence_  = 0;
    uint32_t remote_sequence_ = 0;
};

}  // namespace aichat
}  // namespace framework
