/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智（xiaozhi）MQTT 语音协议实现（移植自官方 xiaozhi-esp32 mqtt_protocol.cc）
 */
#include "ai_mqtt_protocol.h"
#include "ai_network.h"
#include <utils/settings/settings.h>
#include <hal/hal.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include <cstring>

namespace framework {
namespace aichat {

namespace {
constexpr const char* TAG = "ai_mqtt";
}

AiMqttProtocol::AiMqttProtocol(NetworkInterface* network) : network_(network)
{
    event_group_handle_ = xEventGroupCreate();
}

AiMqttProtocol::~AiMqttProtocol()
{
    {
        std::lock_guard<std::mutex> lock(channel_mutex_);
        udp_.reset();
    }
    mqtt_.reset();
    if (event_group_handle_ != nullptr) {
        vEventGroupDelete(event_group_handle_);
    }
}

bool AiMqttProtocol::StartMqttClient()
{
    Settings settings("mqtt", false);
    std::string endpoint       = settings.GetString("endpoint");
    std::string client_id      = settings.GetString("client_id");
    std::string username       = settings.GetString("username");
    std::string password       = settings.GetString("password");
    publish_topic_             = settings.GetString("publish_topic");
    subscribe_topic_           = settings.GetString("subscribe_topic");

    if (endpoint.empty() || publish_topic_.empty()) {
        ESP_LOGE(TAG, "MQTT config incomplete (endpoint/topic missing)");
        return false;
    }

    std::string broker_address = endpoint;
    int broker_port            = 8883;
    size_t pos                 = endpoint.find(':');
    if (pos != std::string::npos) {
        broker_address = endpoint.substr(0, pos);
        broker_port    = std::stoi(endpoint.substr(pos + 1));
    }

    mqtt_.reset();
    mqtt_ = std::unique_ptr<Mqtt>(network_->CreateMqtt(0));
    if (mqtt_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mqtt");
        return false;
    }
    mqtt_->SetKeepAlive(240);

    mqtt_->OnConnected([this]() {
        if (on_connected_ != nullptr) {
            on_connected_();
        }
        ESP_LOGI(TAG, "MQTT connected");
    });
    mqtt_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "MQTT disconnected");
        if (on_disconnected_ != nullptr) {
            on_disconnected_();
        }
    });
    mqtt_->OnMessage([this](const std::string& topic, const std::string& payload) {
        last_incoming_time_us_ = esp_timer_get_time();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            ESP_LOGE(TAG, "Bad MQTT JSON: %s", err.c_str());
            return;
        }
        const char* type = doc["type"] | "";
        if (strcmp(type, "hello") == 0) {
            ESP_LOGI(TAG, "server hello raw: %s", payload.c_str());
            ParseServerHello(doc);
        } else if (strcmp(type, "goodbye") == 0) {
            if (on_audio_channel_closed_ != nullptr) {
                on_audio_channel_closed_();
            }
        } else if (on_incoming_json_ != nullptr) {
            on_incoming_json_(doc);
        }
    });

    ESP_LOGI(TAG, "Connecting to mqtt %s:%d", broker_address.c_str(), broker_port);
    if (!mqtt_->Connect(broker_address, broker_port, client_id, username, password)) {
        ESP_LOGE(TAG, "Failed to connect mqtt, code=%d", mqtt_->GetLastError());
        SetError("MQTT 连接失败");
        return false;
    }
    // 订阅主题：服务器下发的 subscribe_topic 可能是占位 "null"，
    // 真实下行主题为 devices/p2p/<mac 下划线>（服务器按设备 MAC 路由）
    if (subscribe_topic_.empty() || subscribe_topic_ == "null") {
        std::string mac = GetHAL().getFactoryMacString("_");
        subscribe_topic_ = "devices/p2p/" + mac;
    }
    mqtt_->Subscribe(subscribe_topic_);
    ESP_LOGI(TAG, "Subscribed to %s", subscribe_topic_.c_str());
    return true;
}

bool AiMqttProtocol::OpenAudioChannel()
{
    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {
        if (!StartMqttClient()) {
            return false;
        }
    }

    error_occurred_ = false;
    session_id_.clear();
    xEventGroupClearBits(event_group_handle_, AI_MQTT_SERVER_HELLO_EVENT);

    if (!SendText(GetHelloMessage())) {
        return false;
    }

    // 等待服务器 hello（10s 超时）
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, AI_MQTT_SERVER_HELLO_EVENT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & AI_MQTT_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError("server timeout");
        return false;
    }

    // UDP 音频通道（上行发送 + 下行接收）
    auto udp = network_->CreateUdp(2);
    if (udp == nullptr) {
        ESP_LOGE(TAG, "Failed to create udp");
        return false;
    }
    udp->OnMessage([this](const std::string& data) {
        // UDP 加密音频包：|type 1u|flags 1u|payload_len 2u|ssrc 4u|timestamp 4u|sequence 4u|payload|
        constexpr size_t kAudioHeaderSize = 16;
        if (data.size() < kAudioHeaderSize) {
            return;
        }
        if (static_cast<uint8_t>(data[0]) != 0x01) {
            return;
        }
        uint16_t payload_len = 0;
        uint32_t timestamp   = 0;
        uint32_t sequence    = 0;
        memcpy(&payload_len, data.data() + 2, sizeof(payload_len));
        memcpy(&timestamp, data.data() + 8, sizeof(timestamp));
        memcpy(&sequence, data.data() + 12, sizeof(sequence));
        payload_len = ntohs(payload_len);
        timestamp   = ntohl(timestamp);
        sequence    = ntohl(sequence);
        if (data.size() != kAudioHeaderSize + payload_len) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(channel_mutex_);
            if (sequence <= remote_sequence_) {
                return;
            }
            remote_sequence_ = sequence;
        }
        if (aes_ready_) {
            auto packet  = std::make_unique<AudioStreamPacket>();
            packet->sample_rate     = server_sample_rate_;
            packet->frame_duration  = server_frame_duration_;
            packet->timestamp       = timestamp;
            packet->payload.resize(payload_len);
            if (!CryptAesCtr(reinterpret_cast<const uint8_t*>(data.data()) + kAudioHeaderSize,
                             payload_len, reinterpret_cast<const uint8_t*>(data.data()),
                             packet->payload.data())) {
                return;
            }
            if (on_incoming_audio_ != nullptr) {
                on_incoming_audio_(std::move(packet));
            }
        }
        last_incoming_time_us_ = esp_timer_get_time();
    });
    {
        std::lock_guard<std::mutex> lock(channel_mutex_);
        udp_ = std::move(udp);
    }
    if (!udp_->Connect(udp_server_, udp_port_)) {
        ESP_LOGE(TAG, "UDP connect failed");
        return false;
    }
    ESP_LOGI(TAG, "UDP audio channel ready: %s:%d", udp_server_.c_str(), udp_port_);

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }
    return true;
}

void AiMqttProtocol::CloseAudioChannel(bool send_goodbye)
{
    if (send_goodbye && mqtt_ != nullptr && mqtt_->IsConnected()) {
        SendText("{\"type\":\"goodbye\"}");
    }
    {
        std::lock_guard<std::mutex> lock(channel_mutex_);
        udp_.reset();
    }
    if (on_audio_channel_closed_ != nullptr) {
        on_audio_channel_closed_();
    }
}

bool AiMqttProtocol::IsAudioChannelOpened() const
{
    std::lock_guard<std::mutex> lock(channel_mutex_);
    return udp_ != nullptr && !error_occurred_ && !IsTimeout();
}

bool AiMqttProtocol::SendText(const std::string& text)
{
    if (publish_topic_.empty() || mqtt_ == nullptr || !mqtt_->IsConnected()) {
        return false;
    }
    return mqtt_->Publish(publish_topic_, text);
}

bool AiMqttProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet)
{
    std::lock_guard<std::mutex> lock(channel_mutex_);
    if (udp_ == nullptr || !aes_ready_) {
        return false;
    }

    constexpr size_t kAudioHeaderSize = 16;
    if (aes_nonce_.size() != kAudioHeaderSize || packet->payload.size() > UINT16_MAX) {
        return false;
    }

    std::string nonce(aes_nonce_);
    const uint16_t payload_len = htons(static_cast<uint16_t>(packet->payload.size()));
    const uint32_t timestamp   = htonl(packet->timestamp);
    const uint32_t sequence    = htonl(++local_sequence_);
    memcpy(nonce.data() + 2, &payload_len, sizeof(payload_len));
    memcpy(nonce.data() + 8, &timestamp, sizeof(timestamp));
    memcpy(nonce.data() + 12, &sequence, sizeof(sequence));

    std::string encrypted;
    encrypted.resize(kAudioHeaderSize + packet->payload.size());
    memcpy(encrypted.data(), nonce.data(), nonce.size());

    if (!CryptAesCtr(packet->payload.data(), packet->payload.size(),
                     reinterpret_cast<const uint8_t*>(nonce.data()),
                     reinterpret_cast<uint8_t*>(&encrypted[kAudioHeaderSize]))) {
        return false;
    }
    return udp_->Send(encrypted) > 0;
}

std::string AiMqttProtocol::GetHelloMessage()
{
    JsonDocument doc;
    doc["type"]    = "hello";
    doc["version"] = 3;
    JsonObject features  = doc["features"].to<JsonObject>();
    features["aec"]      = false;
    features["mcp"]      = false;
    doc["transport"]     = "udp";
    JsonObject audio_params = doc["audio_params"].to<JsonObject>();
    audio_params["format"]         = "opus";
    audio_params["sample_rate"]    = 24000;  // 与 HAL 直采/服务器一致（官方 stopwatch 板配置）
    audio_params["channels"]       = 1;
    audio_params["frame_duration"] = 60;

    std::string message;
    serializeJson(doc, message);
    return message;
}

void AiMqttProtocol::ParseServerHello(const JsonDocument& doc)
{
    // 诊断：解析后重序列化，确认 udp 段是否进入 doc
    {
        std::string re;
        serializeJson(doc, re);
        ESP_LOGI(TAG, "parsed hello: %s", re.c_str());
    }

    const char* transport = doc["transport"] | "";
    if (strcmp(transport, "udp") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport);
        return;
    }
    session_id_ = doc["session_id"] | "";

    // audio_params（ArduinoJson V7 的 is<JsonObject>() 对 const doc 异常返回 false，
    // 直接读字段 + 值校验）
    {
        int sample_rate    = doc["audio_params"]["sample_rate"] | -1;
        int frame_duration = doc["audio_params"]["frame_duration"] | -1;
        if (sample_rate > 0) {
            server_sample_rate_ = sample_rate;
        }
        if (frame_duration > 0) {
            server_frame_duration_ = frame_duration;
        }
        ESP_LOGI(TAG, "server audio: %d Hz / %d ms", server_sample_rate_, server_frame_duration_);
    }

    // udp server/port/key/nonce（注意：ArduinoJson V7 的 is<JsonObject>() 对 const doc
    // 的对象检查异常返回 false，这里直接读字段 + 空值验证）
    udp_server_ = doc["udp"]["server"] | "";
    udp_port_   = doc["udp"]["port"] | 0;
    std::string key_hex   = doc["udp"]["key"] | "";
    std::string nonce_hex = doc["udp"]["nonce"] | "";
    if (udp_server_.empty() || udp_port_ <= 0) {
        ESP_LOGE(TAG, "UDP config missing in hello");
        return;
    }

    std::string aes_key;
    if (!DecodeHexString(nonce_hex, aes_nonce_) || !DecodeHexString(key_hex, aes_key) ||
        aes_nonce_.size() != 16 || aes_key.size() != 16) {
        ESP_LOGE(TAG, "Invalid AES key/nonce");
        return;
    }
    memcpy(aes_key_, aes_key.data(), 16);
    aes_ready_ = true;
    local_sequence_  = 0;
    remote_sequence_ = 0;

    ESP_LOGI(TAG, "Server hello: session=%s udp=%s:%d", session_id_.c_str(), udp_server_.c_str(),
             udp_port_);
    xEventGroupSetBits(event_group_handle_, AI_MQTT_SERVER_HELLO_EVENT);
}

bool AiMqttProtocol::DecodeHexString(const std::string& hex_string, std::string& decoded)
{
    decoded.clear();
    if ((hex_string.size() % 2) != 0) {
        return false;
    }
    auto char_to_hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    decoded.reserve(hex_string.size() / 2);
    for (size_t i = 0; i < hex_string.size(); i += 2) {
        int high = char_to_hex(hex_string[i]);
        int low  = char_to_hex(hex_string[i + 1]);
        if (high < 0 || low < 0) {
            decoded.clear();
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

bool AiMqttProtocol::CryptAesCtr(const uint8_t* input, size_t input_size, const uint8_t* nonce,
                                 uint8_t* output)
{
    std::lock_guard<std::mutex> lock(crypto_mutex_);
    if (!aes_ready_ || input == nullptr || nonce == nullptr || output == nullptr) {
        return false;
    }
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, aes_key_, 128);

    uint8_t stream_block[16] = {};
    size_t nc_off            = 0;
    uint8_t nonce_copy[16];
    memcpy(nonce_copy, nonce, 16);
    int ret = mbedtls_aes_crypt_ctr(&aes, input_size, &nc_off, nonce_copy, stream_block, input, output);
    mbedtls_aes_free(&aes);
    return ret == 0;
}

}  // namespace aichat
}  // namespace framework
