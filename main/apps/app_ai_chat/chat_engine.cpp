/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @brief 小智 AI 对话状态机与任务编排（阶段三 P1）
 */
#include "chat_engine.h"

#include <framework/ai_chat/ai_opus.h>
#include <framework/ai_chat/ai_resample.h>
#include <framework/ai_chat/ai_ota.h>
#include <framework/ai_chat/ai_network.h>
#include <framework/ai_chat/ai_websocket_protocol.h>
#include <framework/audio_mutex/audio_mutex.h>
#include <framework/wifi_manager/wifi_manager.h>
#include <hal/hal.h>
#include <utils/settings/settings.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>

#define TAG "chat_engine"

using framework::aichat::AudioStreamPacket;
using framework::aichat::Protocol;
using framework::aichat::AiWebsocketProtocol;
using framework::aichat::ListeningMode;

namespace app_ai_chat {

namespace {
constexpr uint32_t kPingIntervalMs  = 30 * 1000;
constexpr uint32_t kTimeoutCheckMs  = 5 * 1000;
constexpr uint32_t kRecChunkMs      = 100;       // 录音块（44.1kHz）
constexpr size_t   kAudioQueueMax   = 64;        // 下行音频包上限（≈3.8s @60ms）
}

ChatEngine::ChatEngine() = default;
ChatEngine::~ChatEngine() = default;

// ---------------------------------------------------------------- 生命周期

void ChatEngine::start()
{
    if (running_) {
        return;
    }
    running_ = true;
    events_  = xEventGroupCreate();

    xTaskCreate(chatNetTask, "chat_net", 6 * 1024, this, 2, &net_task_);
}

void ChatEngine::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;
    if (events_) {
        xEventGroupSetBits(events_, EVT_STOP);
    }
    // 等 chat_net 收尾（含子任务/通道清理）
    if (net_task_ && net_task_ != xTaskGetCurrentTaskHandle()) {
        uint32_t waited = 0;
        while (net_task_ && waited < 5000) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
    }
    if (events_) {
        vEventGroupDelete(events_);
        events_ = nullptr;
    }
    snapshot_ = UiSnapshot{};
}

// ---------------------------------------------------------------- 用户输入

void ChatEngine::onUserStart()
{
    if (!running_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (snapshot_.state) {
            case ChatState::Idle:
                setState(ChatState::Connecting, "连接中…");
                xEventGroupSetBits(events_, EVT_CONNECT);
                break;
            case ChatState::Listening:
                // 再次触发 = 停止聆听
                xEventGroupSetBits(events_, EVT_USER_STOP);
                break;
            case ChatState::Speaking:
                xEventGroupSetBits(events_, EVT_ABORT);
                break;
            default:
                break;  // Connecting / Activating / Error 忽略
        }
    }
}

void ChatEngine::onUserStop()
{
    if (!running_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state == ChatState::Listening) {
        xEventGroupSetBits(events_, EVT_USER_STOP);
    }
}

void ChatEngine::injectDetectedText(const std::string& text)
{
    if (!running_ || !protocol_) {
        return;
    }
    // 移植 stackchan WakeWordInvoke 的注入分支：不走 abort 路径，直接注入文本
    protocol_->SendDetectedText(text);
}

// ---------------------------------------------------------------- 状态

ChatState ChatEngine::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.state;
}

void ChatEngine::setState(ChatState s, const std::string& message, const std::string& error)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.state    = s;
        snapshot_.message  = message;
        snapshot_.error    = error;
        snapshot_.revision++;
    }
    ESP_LOGI(TAG, "state -> %d msg=%s", static_cast<int>(s), message.c_str());
}

void ChatEngine::setEmotion(const std::string& emotion)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.emotion = emotion;
        snapshot_.revision++;
    }
}

// ---------------------------------------------------------------- chat_net

void ChatEngine::chatNetTask(void* arg)
{
    static_cast<ChatEngine*>(arg)->chatNetLoop();
}

void ChatEngine::chatNetLoop()
{
    uint64_t last_ping_ms = 0;
    uint64_t last_timeout_check_ms = 0;

    // 未配置 websocket 且 WiFi 在线 → 先激活
    {
        Settings ws_settings("websocket", false);
        if (ws_settings.GetString("url").empty()) {
            if (framework::WifiManager::get().isConnected()) {
                setState(ChatState::Activating, "正在激活设备…");
                // 8KB：TLS 握手在任务内执行，栈需求大（对齐 stackchan 4096*2）
                xTaskCreate(activationTask, "activation", 8 * 1024, this, 2, nullptr);
            } else {
                setState(ChatState::Idle, "未配网：请在设置中配置 WiFi 后重试", "no wifi");
            }
        } else {
            setState(ChatState::Idle, "点击屏幕或按侧键开始对话");
        }
    }

    while (running_) {
        // 10ms 事件等待 + 自身节拍（心跳/超时）
        EventBits_t bits = xEventGroupWaitBits(events_,
                                               EVT_CONNECT | EVT_USER_STOP | EVT_REC_DONE | EVT_PLAY_DONE |
                                                   EVT_ABORT | EVT_ACTIVATION_DONE | EVT_CHANNEL_CLOSED |
                                                   EVT_JSON_QUEUED | EVT_AUDIO_QUEUED | EVT_STOP,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(100));

        if (bits & EVT_STOP) {
            break;
        }

        // ---- 连接 ----
        if (bits & EVT_CONNECT) {
            bool ok = tryOpenChannel();
            if (ok) {
                startRecording();
            }
        }

        // ---- 用户停止聆听 ----
        if (bits & EVT_USER_STOP) {
            stopRecordingAndWait();
            if (protocol_ && protocol_->IsAudioChannelOpened()) {
                protocol_->SendStopListening();
            }
            setState(ChatState::Idle, "已停止聆听");
        }

        // ---- 打断 ----
        if (bits & EVT_ABORT) {
            if (protocol_) {
                protocol_->SendAbortSpeaking();
            }
            stopPlaybackAndWait(true);
            setState(ChatState::Idle, "已打断");
        }

        // ---- 播报结束（tts stop 后 chat_play 自然退出）----
        if (bits & EVT_PLAY_DONE) {
            if (state() == ChatState::Speaking) {
                setState(ChatState::Idle, "点击屏幕或按侧键继续对话");
            }
        }

        // ---- 通道断开 ----
        if (bits & EVT_CHANNEL_CLOSED) {
            stopRecordingAndWait();
            stopPlaybackAndWait(false);
            setState(ChatState::Idle, "连接已断开", "disconnected");
        }

        // ---- 激活完成 ----
        if (bits & EVT_ACTIVATION_DONE) {
            Settings ws_settings("websocket", false);
            if (!ws_settings.GetString("url").empty()) {
                setState(ChatState::Idle, "激活完成，点击屏幕开始对话");
            } else if (snapshot_.state == ChatState::Activating) {
                setState(ChatState::Idle, "激活失败，请重试", "activation failed");
            }
        }

        // ---- JSON 事件 ----
        while (true) {
            JsonEvent ev;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (json_queue_.empty()) {
                    break;
                }
                ev = std::move(json_queue_.front());
                json_queue_.pop_front();
            }
            handleJsonEvent(ev);
        }

        // ---- 心跳 / 超时 ----
        uint64_t now = esp_timer_get_time() / 1000;
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            if (now - last_ping_ms >= kPingIntervalMs) {
                last_ping_ms = now;
                // esp-ml307 WebSocket 无公开 ping 时跳过（保活由 TCP keepalive 兜底）
            }
            if (now - last_timeout_check_ms >= kTimeoutCheckMs) {
                last_timeout_check_ms = now;
                if (protocol_->IsTimeout()) {
                    ESP_LOGW(TAG, "Channel timeout, closing");
                    closeChannel();
                    setState(ChatState::Idle, "连接超时", "timeout");
                }
            }
        }
    }

    // 收尾
    closeChannel();
    stopRecordingAndWait();
    stopPlaybackAndWait(false);
    net_task_ = nullptr;
    vTaskDelete(nullptr);
}

bool ChatEngine::tryOpenChannel()
{
    // 通道已开（自动模式 tts 结束后）直接进聆听
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        setState(ChatState::Listening, "聆听中…");
        return true;
    }

    protocol_ = std::make_unique<AiWebsocketProtocol>(framework::aichat::GetAiNetwork());

    // 协议回调（WS 任务线程上下文）
    protocol_->OnIncomingJson([this](const JsonDocument& doc) {
        JsonEvent ev;
        ev.type    = doc["type"] | "";
        ev.state   = doc["state"] | "";
        ev.text    = doc["text"] | "";
        ev.emotion = doc["emotion"] | "";
        ev.command = doc["command"] | "";
        if (doc["payload"].is<JsonObject>()) {
            std::string p;
            serializeJson(doc["payload"], p);
            ev.payload = std::move(p);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            json_queue_.push_back(std::move(ev));
        }
        if (events_) {
            xEventGroupSetBits(events_, EVT_JSON_QUEUED);
        }
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (audio_queue_.size() >= kAudioQueueMax) {
                audio_queue_.pop_front();  // 丢最旧
            }
            audio_queue_.push_back(std::move(packet));
        }
        if (events_) {
            xEventGroupSetBits(events_, EVT_AUDIO_QUEUED);
        }
    });
    protocol_->OnAudioChannelOpened([this]() {
        server_sample_rate_    = protocol_->server_sample_rate();
        server_frame_duration_ = protocol_->server_frame_duration();
        ESP_LOGI(TAG, "Server audio: %d Hz / %d ms", server_sample_rate_, server_frame_duration_);
    });
    protocol_->OnNetworkError([this](const std::string& msg) {
        ESP_LOGW(TAG, "Network error: %s", msg.c_str());
    });
    protocol_->OnDisconnected([this]() {
        if (events_) {
            xEventGroupSetBits(events_, EVT_CHANNEL_CLOSED);
        }
    });

    if (!protocol_->OpenAudioChannel()) {
        closeChannel();
        setState(ChatState::Idle, "连接服务器失败，请重试", "connect failed");
        return false;
    }
    setState(ChatState::Listening, "聆听中…");
    return true;
}

void ChatEngine::closeChannel()
{
    if (protocol_) {
        protocol_->CloseAudioChannel();
        protocol_.reset();
    }
    server_sample_rate_   = 16000;
    server_frame_duration_ = 60;
}

// ---------------------------------------------------------------- JSON 分发

void ChatEngine::handleJsonEvent(const JsonEvent& ev)
{
    ESP_LOGI(TAG, "json: type=%s state=%s text=%.40s", ev.type.c_str(), ev.state.c_str(), ev.text.c_str());
    if (ev.type == "tts") {
        handleTts(ev);
    } else if (ev.type == "listen") {
        handleListen(ev);
    } else if (ev.type == "stt") {
        setState(state(), ev.text);
    } else if (ev.type == "llm") {
        setEmotion(ev.emotion);
    } else if (ev.type == "mcp") {
        // TODO(P2)：ai_mcp JSON-RPC 工具分派
        ESP_LOGI(TAG, "mcp message ignored (P2): %.80s", ev.payload.c_str());
    } else if (ev.type == "system") {
        handleSystem(ev);
    } else if (ev.type == "alert") {
        handleAlert(ev);
    }
}

void ChatEngine::handleTts(const JsonEvent& ev)
{
    ChatState s = state();
    if (ev.state == "start") {
        if (s == ChatState::Listening || s == ChatState::Idle) {
            stopRecordingAndWait();
            startPlayback();
        }
    } else if (ev.state == "sentence_start") {
        setState(ChatState::Speaking, ev.text);
    } else if (ev.state == "stop") {
        // 播完剩余缓冲后由 EVT_PLAY_DONE 收尾
        if (play_task_) {
            play_drain_ = true;
        }
    }
}

void ChatEngine::handleListen(const JsonEvent& ev)
{
    // 服务器 VAD 判停（auto 模式）：停止录音，等待 tts
    if (ev.state == "stop" && state() == ChatState::Listening) {
        stopRecordingAndWait();
        setState(ChatState::Idle, "识别中…");
    }
}

void ChatEngine::handleSystem(const JsonEvent& ev)
{
    if (ev.command == "reboot") {
        setState(ChatState::Idle, "收到重启指令");
        // 延迟 1s 重启（让 UI 刷新）
        vTaskDelay(pdMS_TO_TICKS(1000));
        GetHAL().reboot();
    }
}

void ChatEngine::handleAlert(const JsonEvent& ev)
{
    setState(snapshot_.state, ev.text.empty() ? "注意" : ev.text);
}

// ---------------------------------------------------------------- chat_rec

void ChatEngine::startRecording()
{
    if (rec_task_) {
        return;
    }
    rec_stop_ = false;
    // 栈 24KB 放 PSRAM：Opus 编码（CELT）+ esp_codec_dev_read 读路径栈需求大，
    // 8KB/16KB 内部 RAM 栈均实测栈溢出（对齐原 VoiceCube 工程经验）
    if (xTaskCreateWithCaps(chatRecTask, "chat_rec", 24 * 1024, this, 3, &rec_task_,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        rec_task_ = nullptr;
        setState(ChatState::Idle, "录音任务创建失败", "task create failed");
    }
}

void ChatEngine::chatRecTask(void* arg)
{
    static_cast<ChatEngine*>(arg)->recLoop();
}

void ChatEngine::recLoop()
{
    auto& hal = GetHAL();

    framework::AudioMutex::get().acquireRecord(portMAX_DELAY);
    if (audio_encoder_init(server_sample_rate_, 1, server_frame_duration_) != ESP_OK) {
        framework::AudioMutex::get().releaseRecord();
        rec_task_ = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    const size_t frame_samples = audio_encoder_frame_samples();   // 60ms @16k = 960
    const size_t dst_chunk      = server_sample_rate_ / 1000 * kRecChunkMs;

    std::vector<int16_t> resampled(dst_chunk);
    std::vector<int16_t> frame_pcm(frame_samples);
    std::vector<uint8_t> opus_out(400);
    size_t frame_fill = 0;

    ESP_LOGI(TAG, "recording start (%d Hz / %d ms)", server_sample_rate_, server_frame_duration_);

    while (!rec_stop_) {
        std::vector<int16_t> chunk;
        hal.audioRecord(chunk, kRecChunkMs, 30.0f);
        if (chunk.empty()) {
            continue;
        }

        size_t dst_len = ai_resample_linear(chunk.data(), chunk.size(), 44100,
                                            server_sample_rate_, resampled.data(), resampled.size());

        size_t i = 0;
        while (i < dst_len && !rec_stop_) {
            size_t need = frame_samples - frame_fill;
            size_t take = (dst_len - i < need) ? (dst_len - i) : need;
            memcpy(&frame_pcm[frame_fill], &resampled[i], take * sizeof(int16_t));
            frame_fill += take;
            i += take;

            if (frame_fill == frame_samples) {
                size_t out_len = 0;
                if (audio_encode_frame(frame_pcm.data(), frame_samples, opus_out.data(), opus_out.size(),
                                       &out_len) == ESP_OK &&
                    out_len > 0) {
                    auto packet = std::make_unique<AudioStreamPacket>();
                    packet->sample_rate     = server_sample_rate_;
                    packet->frame_duration  = server_frame_duration_;
                    packet->timestamp       = (uint32_t)(esp_timer_get_time() / 1000);
                    packet->payload.assign(opus_out.begin(), opus_out.begin() + out_len);
                    if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                        ESP_LOGW(TAG, "send audio failed, stop recording");
                        rec_stop_ = true;
                        break;
                    }
                }
                frame_fill = 0;
            }
        }
    }

    audio_encoder_deinit();
    framework::AudioMutex::get().releaseRecord();

    ESP_LOGI(TAG, "recording done");
    rec_task_ = nullptr;
    if (events_) {
        xEventGroupSetBits(events_, EVT_REC_DONE);
    }
    vTaskDelete(nullptr);
}

void ChatEngine::stopRecordingAndWait()
{
    rec_stop_ = true;
    if (rec_task_ && rec_task_ != xTaskGetCurrentTaskHandle()) {
        uint32_t waited = 0;
        while (rec_task_ && waited < 2000) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
    }
    rec_stop_ = false;
}

// ---------------------------------------------------------------- chat_play

void ChatEngine::startPlayback()
{
    if (play_task_) {
        return;
    }
    play_stop_  = false;
    play_drain_ = false;
    play_buffer_.clear();
    // 解码同样走 CELT（栈需求大），与录音任务一致放 PSRAM
    if (xTaskCreateWithCaps(chatPlayTask, "chat_play", 24 * 1024, this, 3, &play_task_,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        play_task_ = nullptr;
        setState(ChatState::Idle, "播放任务创建失败", "task create failed");
    }
}

void ChatEngine::chatPlayTask(void* arg)
{
    static_cast<ChatEngine*>(arg)->playLoop();
}

void ChatEngine::playLoop()
{
    auto& hal = GetHAL();

    framework::AudioMutex::get().acquirePlay(portMAX_DELAY);
    if (audio_decoder_init(server_sample_rate_, 1, server_frame_duration_) != ESP_OK) {
        framework::AudioMutex::get().releasePlay();
        play_task_ = nullptr;
        if (events_) {
            xEventGroupSetBits(events_, EVT_PLAY_DONE);
        }
        vTaskDelete(nullptr);
        return;
    }

    const size_t dec_samples = audio_decoder_frame_samples();
    std::vector<int16_t> pcm(dec_samples > 0 ? dec_samples : 960);
    std::vector<int16_t> resampled(kPlayAccumulateSamples);

    ESP_LOGI(TAG, "playback start (%d Hz / %d ms)", server_sample_rate_, server_frame_duration_);

    while (!play_stop_) {
        std::unique_ptr<AudioStreamPacket> packet;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!audio_queue_.empty()) {
                packet = std::move(audio_queue_.front());
                audio_queue_.pop_front();
            }
        }
        if (!packet) {
            // 队列空：drain 完成则退出；否则等待
            if (play_drain_) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        size_t out_samples = 0;
        if (audio_decode_frame(packet->payload.data(), packet->payload.size(),
                               pcm.data(), pcm.size(), &out_samples) != ESP_OK) {
            continue;
        }

        // 服务器采样率 → 44.1k 硬件播放
        size_t n = ai_resample_linear(pcm.data(), out_samples, server_sample_rate_, 44100,
                                      resampled.data(), resampled.size());
        play_buffer_.insert(play_buffer_.end(), resampled.begin(), resampled.begin() + n);

        // 攒够起播阈值后按块播放（audioPlay 为整体替换语义，不能逐帧调用）
        if (play_buffer_.size() >= kPlayAccumulateSamples) {
            while (hal.getAudioBusy() && !play_stop_) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (play_stop_) {
                break;
            }
            size_t take = (play_buffer_.size() > kPlayChunkSamples) ? kPlayChunkSamples : play_buffer_.size();
            std::vector<int16_t> chunk(play_buffer_.begin(), play_buffer_.begin() + take);
            hal.audioPlay(chunk, true);
            play_buffer_.erase(play_buffer_.begin(), play_buffer_.begin() + take);
        }
    }

    // 收尾：把剩余缓冲播完（非 abort 时）
    if (!play_stop_ && !play_buffer_.empty()) {
        while (hal.getAudioBusy()) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        hal.audioPlay(play_buffer_, true);
    }
    play_buffer_.clear();

    audio_decoder_deinit();
    framework::AudioMutex::get().releasePlay();

    ESP_LOGI(TAG, "playback done");
    play_task_ = nullptr;
    if (events_) {
        xEventGroupSetBits(events_, EVT_PLAY_DONE);
    }
    vTaskDelete(nullptr);
}

void ChatEngine::stopPlaybackAndWait(bool abort)
{
    play_stop_ = true;
    if (abort) {
        // 立即打断当前播放（audioPlay 形参为非 const 左值引用，需具名变量）
        std::vector<int16_t> empty;
        GetHAL().audioPlay(empty, true);
    }
    if (play_task_ && play_task_ != xTaskGetCurrentTaskHandle()) {
        uint32_t waited = 0;
        while (play_task_ && waited < 2000) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
    }
    play_stop_  = false;
    play_drain_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        audio_queue_.clear();
    }
}

// ---------------------------------------------------------------- activation

void ChatEngine::activationTask(void* arg)
{
    static_cast<ChatEngine*>(arg)->activationLoop();
}

void ChatEngine::activationLoop()
{
    constexpr int kMaxRetry    = 10;
    constexpr int kRetryDelayMs = 10 * 1000;

    framework::aichat::AiOta ota;
    int retry = 0;

    while (running_ && retry < kMaxRetry) {
        esp_err_t err = ota.CheckVersion();
        if (err != ESP_OK) {
            retry++;
            if (retry >= kMaxRetry) {
                break;
            }
            setState(ChatState::Activating, "激活失败，正在重试…");
            vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
            continue;
        }

        if (ota.HasActivationCode()) {
            // 需要激活码：全屏展示，等用户网页绑定后重试
            snapshot_.activation_code = ota.GetActivationCode();
            setState(ChatState::Activating,
                     "请在 xiaozhi.me 网页端输入激活码绑定设备");
            vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
            continue;
        }

        // websocket 配置已写入（或未返回配置），结束
        break;
    }

    snapshot_.activation_code.clear();
    if (events_) {
        xEventGroupSetBits(events_, EVT_ACTIVATION_DONE);
    }
    vTaskDelete(nullptr);
}

}  // namespace app_ai_chat
