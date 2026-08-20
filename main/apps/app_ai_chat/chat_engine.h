/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智 AI 对话状态机与任务编排（阶段三 P1）
 * 借鉴 Stackchan-Newstep application.cc 的事件组 + Schedule 模式，按 mooncake App 重构。
 */
#pragma once

#include <framework/ai_chat/ai_protocol.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <deque>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

namespace framework {
namespace aichat {
class AiWebsocketProtocol;
}
}  // namespace framework

namespace app_ai_chat {

/** 状态机状态（精简自 stackchan device_state） */
enum class ChatState {
    Idle,        // 空闲（通道可能仍开着）
    Activating,  // 激活/获取配置中
    Connecting,  // 正在建立语音通道
    Listening,   // 聆听（录音上行）
    Speaking,    // 播报（TTS 下行）
    Error,       // 错误（message 说明）
};

/** UI 快照（App 的 onRunning 轮询 revision 变化后刷新 LVGL） */
struct UiSnapshot {
    ChatState state    = ChatState::Idle;
    std::string message;         // 状态提示 / 最新文本（stt / tts sentence）
    std::string emotion;         // llm.emotion（P2 表情映射用）
    std::string activation_code; // 激活码（Activating 时）
    std::string error;           // Error 说明
    uint64_t revision = 0;
};

/** 引擎内部 JSON 事件（WS 回调线程提取后入队，chat_net 消费） */
struct JsonEvent {
    std::string type;     // tts / stt / llm / mcp / system / alert / listen
    std::string state;    // tts: start|sentence_start|stop；listen: start|stop；alert: status
    std::string text;     // tts sentence / stt / alert message
    std::string emotion;  // llm.emotion
    std::string command;  // system.command
    std::string payload;  // mcp 原始 JSON
};

class ChatEngine {
public:
    ChatEngine();
    ~ChatEngine();

    /** App onOpen：创建 chat_net 任务；无 websocket 配置且 WiFi 在线则自动激活 */
    void start();
    /** App onClose：停止一切任务与通道 */
    void stop();

    // ---- 用户输入（App 主线程调用，线程安全） ----
    /** 触摸/按键按下：Idle→Listening(若通道未开先 Connecting)；Speaking→打断。
     *  mode: ManualStop=hold-to-talk（设备发 listen stop）；AutoStop=服务器 VAD 判停 */
    void onUserStart(framework::aichat::ListeningMode mode = framework::aichat::kListeningModeAutoStop);
    /** 松开/再次触发：Listening→停止录音→listen stop→Idle */
    void onUserStop();
    /** 摇晃/语料注入（P2 互动用） */
    void injectDetectedText(const std::string& text);

    // ---- UI ----
    const UiSnapshot& snapshot() const { return snapshot_; }
    uint64_t revision() const { return snapshot_.revision; }

private:
    enum EngineEvent : uint32_t {
        EVT_CONNECT          = (1 << 0),  // 请求建立通道（连接完成事件见下）
        EVT_USER_STOP        = (1 << 1),  // 用户停止聆听
        EVT_REC_DONE         = (1 << 2),  // chat_rec 已退出（可发 listen stop）
        EVT_PLAY_DONE        = (1 << 3),  // chat_play 已退出
        EVT_ABORT            = (1 << 4),  // 打断播放
        EVT_ACTIVATION_DONE  = (1 << 5),  // 激活完成/失败
        EVT_CHANNEL_CLOSED   = (1 << 6),  // WS 断开
        EVT_JSON_QUEUED      = (1 << 7),  // 有 JSON 事件待消费
        EVT_AUDIO_QUEUED     = (1 << 8),  // 有音频包待消费
        EVT_STOP             = (1 << 9),  // 引擎停止（onClose）
    };

    // ---- chat_net（协议事件循环） ----
    static void chatNetTask(void* arg);
    void chatNetLoop();

    void handleJsonEvent(const JsonEvent& ev);
    void handleTts(const JsonEvent& ev);
    void handleListen(const JsonEvent& ev);
    void handleSystem(const JsonEvent& ev);
    void handleAlert(const JsonEvent& ev);

    bool tryOpenChannel();
    void closeChannel();

    ChatState state() const;
    void setState(ChatState s, const std::string& message = "", const std::string& error = "");
    void setEmotion(const std::string& emotion);
    void startRecording();
    void stopRecordingAndWait();
    void startPlayback();
    void stopPlaybackAndWait(bool abort);

    // ---- chat_rec（录音上行） ----
    static void chatRecTask(void* arg);
    void recLoop();

    // ---- chat_play（下行播放） ----
    static void chatPlayTask(void* arg);
    void playLoop();

    // ---- activation ----
    static void activationTask(void* arg);
    void activationLoop();

    // ---- 状态与队列（mutex 保护） ----
    mutable std::mutex mutex_;
    // 基类指针：websocket 或 mqtt 协议（服务器主通道已切 MQTT）
    std::unique_ptr<framework::aichat::Protocol> protocol_;
    EventGroupHandle_t events_ = nullptr;

    volatile bool running_ = false;
    TaskHandle_t net_task_ = nullptr;
    TaskHandle_t rec_task_ = nullptr;
    TaskHandle_t play_task_ = nullptr;

    volatile bool rec_stop_   = false;  // chat_rec 退出标记
    volatile bool play_stop_  = false;  // chat_play 退出标记
    volatile bool play_drain_ = false;  // 播完剩余缓冲后退出（tts stop 正常结束）

    std::deque<JsonEvent> json_queue_;  // WS 线程 → chat_net
    std::deque<std::unique_ptr<framework::aichat::AudioStreamPacket>> audio_queue_;  // WS 线程 → chat_play

    // 会话参数（server hello 后确定）
    int server_sample_rate_   = 16000;
    int server_frame_duration_ = 60;
    // 聆听模式（最近一次 onUserStart 决定；通道打开后 SendStartListening 使用）
    framework::aichat::ListeningMode listening_mode_ = framework::aichat::kListeningModeAutoStop;
    // 简易 VAD（auto 模式）：服务器端不做判停（实测 listen stop 从不下发），
    // 设备检测到"语音开始后连续静音"即自动停止录音
    static constexpr int16_t kVadVoicePeak = 600;    // 语音块峰值（环境噪声 max 200~300）
    static constexpr uint32_t kVadSilenceMs = 900;   // 语音后静音判停时长
    static constexpr uint32_t kVadNoVoiceTimeoutMs = 8000;  // 从未检测到语音的兜底超时（防"卡聆听"）
    uint64_t _last_voice_ms = 0;                     // 最近一次语音块的毫秒时间戳
    uint64_t _rec_start_ms  = 0;                     // 本轮录音开始毫秒时间戳（无语音超时用）
    bool _voice_detected    = false;                 // 本轮录音是否检测到过语音
    bool _vad_no_voice      = false;                 // 本轮录音因"无语音超时"停止（回待命而非识别中）

    UiSnapshot snapshot_;

    // 播放缓冲
    std::vector<int16_t> play_buffer_;
    static constexpr size_t kPlayAccumulateSamples = 24000 * 4 / 10;  // 400ms @24k（jitter buffer）
    static constexpr size_t kPlayChunkSamples      = 24000 * 2 / 10;  // 200ms @24k
};

}  // namespace app_ai_chat
