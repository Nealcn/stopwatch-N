/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智 WebSocket 语音协议（移植自 Stackchan-Newstep websocket_protocol.cc，
 *        JSON 由 cJSON 换 ArduinoJson，Device-Id/Client-Id 换成本工程 HAL/Settings）
 */
#include "ai_websocket_protocol.h"

#include "ai_network.h"
#include <hal/hal.h>
#include <utils/settings/settings.h>

#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <arpa/inet.h>
#include <cstring>

#define TAG "ai_ws"

namespace framework {
namespace aichat {

AiWebsocketProtocol::AiWebsocketProtocol(NetworkInterface* network)
    : network_(network)
{
    event_group_handle_ = xEventGroupCreate();
}

AiWebsocketProtocol::~AiWebsocketProtocol()
{
    if (event_group_handle_) {
        vEventGroupDelete(event_group_handle_);
    }
}

bool AiWebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet)
{
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
        auto bp2 = reinterpret_cast<BinaryProtocol2*>(serialized.data());
        bp2->version      = htons(static_cast<uint16_t>(version_));
        bp2->type         = 0;  // OPUS
        bp2->reserved     = 0;
        bp2->timestamp    = htonl(packet->timestamp);
        bp2->payload_size = htonl(static_cast<uint32_t>(packet->payload.size()));
        memcpy(bp2->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else if (version_ == 3) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());
        auto bp3 = reinterpret_cast<BinaryProtocol3*>(serialized.data());
        bp3->type         = 0;  // OPUS
        bp3->reserved     = 0;
        bp3->payload_size = htons(static_cast<uint16_t>(packet->payload.size()));
        memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    }
    // version 1：裸 Opus payload
    return websocket_->Send(reinterpret_cast<const char*>(packet->payload.data()),
                            packet->payload.size(), true);
}

bool AiWebsocketProtocol::SendText(const std::string& text)
{
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }
    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError("server error");
        return false;
    }
    return true;
}

bool AiWebsocketProtocol::IsAudioChannelOpened() const
{
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void AiWebsocketProtocol::CloseAudioChannel(bool send_goodbye)
{
    (void)send_goodbye;  // WebSocket 无需 goodbye
    websocket_.reset();
}

bool AiWebsocketProtocol::OpenAudioChannel()
{
    Settings settings("websocket", false);
    std::string url   = settings.GetString("url");
    std::string token = settings.GetString("token");
    int version       = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    error_occurred_ = false;

    websocket_ = std::unique_ptr<WebSocket>(network_->CreateWebSocket(1));
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    if (!token.empty()) {
        // token 无空格则补 "Bearer " 前缀
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    websocket_->SetHeader("Device-Id", GetHAL().getFactoryMacString(":").c_str());
    websocket_->SetHeader("Client-Id", GetOrCreateClientId().c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        last_incoming_time_us_ = esp_timer_get_time();
        if (binary) {
            if (on_incoming_audio_ != nullptr) {
                if (version_ == 2) {
                    if (len < sizeof(BinaryProtocol2)) {
                        return;
                    }
                    BinaryProtocol2 hdr;
                    memcpy(&hdr, data, sizeof(BinaryProtocol2));
                    hdr.version      = ntohs(hdr.version);
                    hdr.type         = ntohs(hdr.type);
                    hdr.timestamp    = ntohl(hdr.timestamp);
                    hdr.payload_size = ntohl(hdr.payload_size);
                    if (sizeof(BinaryProtocol2) + hdr.payload_size > len) {
                        return;
                    }
                    auto payload = reinterpret_cast<const uint8_t*>(data) + sizeof(BinaryProtocol2);
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate     = server_sample_rate_,
                        .frame_duration  = server_frame_duration_,
                        .timestamp       = hdr.timestamp,
                        .payload         = std::vector<uint8_t>(payload, payload + hdr.payload_size)}));
                } else if (version_ == 3) {
                    if (len < sizeof(BinaryProtocol3)) {
                        return;
                    }
                    BinaryProtocol3 hdr;
                    memcpy(&hdr, data, sizeof(BinaryProtocol3));
                    hdr.payload_size = ntohs(hdr.payload_size);
                    if (sizeof(BinaryProtocol3) + hdr.payload_size > len) {
                        return;
                    }
                    auto payload = reinterpret_cast<const uint8_t*>(data) + sizeof(BinaryProtocol3);
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate     = server_sample_rate_,
                        .frame_duration  = server_frame_duration_,
                        .timestamp       = 0,
                        .payload         = std::vector<uint8_t>(payload, payload + hdr.payload_size)}));
                } else {
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate     = server_sample_rate_,
                        .frame_duration  = server_frame_duration_,
                        .timestamp       = 0,
                        .payload         = std::vector<uint8_t>(
                            reinterpret_cast<const uint8_t*>(data),
                            reinterpret_cast<const uint8_t*>(data) + len)}));
                }
            }
        } else {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                ESP_LOGE(TAG, "Bad JSON: %s", err.c_str());
                return;
            }
            const char* type = doc["type"] | "";
            if (strcmp(type, "hello") == 0) {
                ParseServerHello(doc);
            } else {
                if (on_incoming_json_ != nullptr) {
                    on_incoming_json_(doc);
                }
            }
        }
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting to websocket server: %s (version %d)", url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server, code=%d", websocket_->GetLastError());
        SetError("server not connected");
        return false;
    }

    // 发送客户端 hello
    if (!SendText(GetHelloMessage())) {
        return false;
    }

    // 等待服务器 hello（10s 超时）
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, AI_WS_SERVER_HELLO_EVENT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & AI_WS_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError("server timeout");
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

std::string AiWebsocketProtocol::GetHelloMessage()
{
    JsonDocument doc;
    doc["type"]      = "hello";
    doc["version"]   = version_;
    JsonObject features   = doc["features"].to<JsonObject>();
    features["aec"]       = false;  // 本工程无 AEC
    features["mcp"]       = false;  // TODO(P2)：ai_mcp 工具落地后置 true
    doc["transport"]      = "websocket";
    JsonObject audio_params = doc["audio_params"].to<JsonObject>();
    audio_params["format"]         = "opus";
    audio_params["sample_rate"]    = 16000;
    audio_params["channels"]       = 1;
    audio_params["frame_duration"] = 60;

    std::string message;
    serializeJson(doc, message);
    return message;
}

void AiWebsocketProtocol::ParseServerHello(const JsonDocument& doc)
{
    const char* transport = doc["transport"] | "";
    if (strcmp(transport, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport);
        return;
    }

    const char* session_id = doc["session_id"] | "";
    if (session_id[0] != '\0') {
        session_id_ = session_id;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    if (doc["audio_params"].is<JsonObject>()) {
        int sample_rate    = doc["audio_params"]["sample_rate"] | -1;
        int frame_duration = doc["audio_params"]["frame_duration"] | -1;
        if (sample_rate > 0) {
            server_sample_rate_ = sample_rate;
        }
        if (frame_duration > 0) {
            server_frame_duration_ = frame_duration;
        }
    }

    xEventGroupSetBits(event_group_handle_, AI_WS_SERVER_HELLO_EVENT);
}

}  // namespace aichat
}  // namespace framework
