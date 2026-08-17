/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智（xiaozhi）语音协议抽象层
 * 移植自 Stackchan-Newstep main/protocols/protocol.h（协议 v2/v3），
 * JSON 处理由 cJSON 换成工程内已有的 ArduinoJson。
 */
#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace framework {
namespace aichat {

/** 下行音频包（服务器采样率/帧长随 hello 或每包可变化） */
struct AudioStreamPacket {
    int sample_rate = 0;
    int frame_duration = 0;
    uint32_t timestamp = 0;
    std::vector<uint8_t> payload;
};

/** 音频帧二进制封装 v2：大端 16B 头（version=2, type=0 为 OPUS） */
struct BinaryProtocol2 {
    uint16_t version;      // 2
    uint16_t type;         // 0: OPUS
    uint32_t reserved;
    uint32_t timestamp;    // ms，供服务器端 AEC 参考
    uint32_t payload_size;
    uint8_t payload[];     // Opus 帧
} __attribute__((packed));

/** 音频帧二进制封装 v3：大端 6B 头 */
struct BinaryProtocol3 {
    uint8_t type;
    uint8_t reserved;
    uint16_t payload_size;
    uint8_t payload[];
} __attribute__((packed));

enum AbortReason {
    kAbortReasonNone = 0,
    kAbortReasonWakeWordDetected,
};

enum ListeningMode {
    kListeningModeAutoStop = 0,  // 服务器 VAD 判停
    kListeningModeManualStop,    // 设备发 listen stop 判停（hold-to-talk）
    kListeningModeRealtime,      // 需要 AEC 支持（本工程不支持）
};

class Protocol {
public:
    using IncomingJsonCallback  = std::function<void(const JsonDocument&)>;
    using IncomingAudioCallback = std::function<void(std::unique_ptr<AudioStreamPacket>)>;

    virtual ~Protocol() = default;

    int server_sample_rate() const { return server_sample_rate_; }
    int server_frame_duration() const { return server_frame_duration_; }
    const std::string& session_id() const { return session_id_; }

    void OnIncomingJson(IncomingJsonCallback cb) { on_incoming_json_ = std::move(cb); }
    void OnIncomingAudio(IncomingAudioCallback cb) { on_incoming_audio_ = std::move(cb); }
    void OnAudioChannelOpened(std::function<void()> cb) { on_audio_channel_opened_ = std::move(cb); }
    void OnAudioChannelClosed(std::function<void()> cb) { on_audio_channel_closed_ = std::move(cb); }
    void OnNetworkError(std::function<void(const std::string&)> cb) { on_network_error_ = std::move(cb); }
    void OnConnected(std::function<void()> cb) { on_connected_ = std::move(cb); }
    void OnDisconnected(std::function<void()> cb) { on_disconnected_ = std::move(cb); }

    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel(bool send_goodbye = true) = 0;
    virtual bool IsAudioChannelOpened() const = 0;
    virtual bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) = 0;

    /** 触发词注入（摇晃互动等）：{"type":"listen","state":"detect","text":...} */
    void SendDetectedText(const std::string& text);
    /** 开始聆听：mode 见 ListeningMode */
    void SendStartListening(ListeningMode mode);
    /** 结束聆听（manual 模式） */
    void SendStopListening();
    /** 打断播放 */
    void SendAbortSpeaking(AbortReason reason = kAbortReasonNone);
    /** MCP 消息（P2 起用） */
    void SendMcpMessage(const std::string& payload);

    /** 120s 无下行数据视为超时 */
    bool IsTimeout() const;
    /** 标记网络错误并回调 */
    void SetError(const std::string& message);

protected:
    virtual bool SendText(const std::string& text) = 0;

    IncomingJsonCallback  on_incoming_json_;
    IncomingAudioCallback on_incoming_audio_;
    std::function<void()> on_audio_channel_opened_;
    std::function<void()> on_audio_channel_closed_;
    std::function<void(const std::string&)> on_network_error_;
    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;

    int server_sample_rate_   = 16000;
    int server_frame_duration_ = 60;
    bool error_occurred_ = false;
    std::string session_id_;
    mutable uint64_t last_incoming_time_us_ = 0;
};

}  // namespace aichat
}  // namespace framework
