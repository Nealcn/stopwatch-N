/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_voicecube.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <framework/ble_voice/ble_voice.h>
#include <framework/touch_pad/touch_pad.h>
#include <framework/audio_mutex/audio_mutex.h>
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "opus_encoder.h"

// 板级音频常量（VoiceCube 从 xiaozhi 固件移植，原定义在板级头文件；
// 编码器输入为 16k 重采样后的数据：16k * 60ms = 960 samples, mono）
#define BOARD_AUDIO_SAMPLE_RATE   16000
#define BOARD_AUDIO_CHANNELS      1
#define BOARD_AUDIO_FRAME_MS      60
#define BOARD_AUDIO_FRAME_SAMPLES 960

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

namespace {

constexpr const char* _tag        = "VoiceCube";
constexpr int _record_chunk_ms    = 100;   // 每次 audioRecord 块长（44.1kHz）
constexpr int _src_chunk_samples  = 4410;  // 44.1k * 100ms
constexpr int _dst_chunk_samples  = 1600;  // 16k * 100ms
constexpr uint8_t _flag_start     = 0x01;
constexpr uint8_t _flag_end       = 0x02;

/* 44.1kHz → 16kHz 线性插值重采样（输出点 = 输入位置 * 2.75625） */
size_t resample_44100_to_16000(const int16_t* in, size_t in_len, int16_t* out, size_t out_cap)
{
    constexpr double step = 44100.0 / 16000.0;
    size_t out_idx        = 0;
    for (double phase = 0.0; phase < in_len - 1 && out_idx < out_cap; phase += step, ++out_idx) {
        size_t i0     = static_cast<size_t>(phase);
        float frac    = static_cast<float>(phase - i0);
        float sample  = in[i0] * (1.0f - frac) + in[i0 + 1] * frac;
        out[out_idx]  = static_cast<int16_t>(sample);
    }
    return out_idx;
}

}  // namespace

AppVoiceCube::AppVoiceCube()
{
    setAppInfo().name = "VoiceCube";
    setAppInfo().icon = (void*)&icon_voicecube;
}

void AppVoiceCube::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppVoiceCube::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    // BLE 服务：启动广播 + 注册下行回调
    auto& ble = framework::BleVoice::get();
    ble.setConnectCallback([this](bool connected) { mclog::tagInfo(_tag, "ble connected: {}", connected); });
    ble.setControlCallback([this](const std::string& json) { _on_control(json); });
    ble.start();

    // 触摸板手势 → mouse 帧
    framework::TouchPad::get().setEventCallback([this](const framework::TouchPadEvent& ev) {
        if (ev.type == framework::TouchPadEvent::Move) {
            _send_mouse(ev.dx, ev.dy, 0, "move");
        } else if (ev.type == framework::TouchPadEvent::ButtonDown) {
            _send_mouse(0, 0, ev.button, "down");
        } else {
            _send_mouse(0, 0, ev.button, "up");
        }
    });

    LvglLockGuard lock;
    lv_obj_t* screen = lv_screen_active();

    _status_label = lv_label_create(screen);
    lv_obj_align(_status_label, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_text_font(_status_label, &lv_font_maple_mono_medium_24, 0);
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0x9AA5B5), 0);

    _preview_label = lv_label_create(screen);
    lv_obj_align(_preview_label, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_font(_preview_label, &lv_font_maple_mono_medium_28, 0);
    lv_obj_set_style_text_color(_preview_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(_preview_label, 380);
    lv_obj_set_style_text_align(_preview_label, LV_TEXT_ALIGN_CENTER, 0);

    _hint_label = lv_label_create(screen);
    lv_obj_align(_hint_label, LV_ALIGN_CENTER, 0, 85);
    lv_obj_set_style_text_font(_hint_label, &lv_font_maple_mono_medium_24, 0);
    lv_obj_set_style_text_color(_hint_label, lv_color_hex(0x6B7686), 0);

    _confirm_button = std::make_unique<Button>(screen);
    _confirm_button->align(LV_ALIGN_BOTTOM_MID, -110, -60);
    _confirm_button->label().setText("确认 ✓");
    _confirm_button->onClick().connect([this]() {
        // 确认粘贴：通知桌面端 Ctrl+V
        framework::BleVoice::get().sendStateJson("{\"event\":\"paste_request\"}");
        _feedback_until_ms = GetHAL().millis() + 1500;
        _set_state(State::Pasting);
    });

    _cancel_button = std::make_unique<Button>(screen);
    _cancel_button->align(LV_ALIGN_BOTTOM_MID, 110, -60);
    _cancel_button->label().setText("取消 ✗");
    _cancel_button->onClick().connect([this]() {
        _preview_text.clear();
        _set_state(State::Idle);
    });

    _set_state(State::Idle);
}

void AppVoiceCube::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        _stop_record();
        close();
        return;
    }

    // 触摸板（录音中暂停，避免误触）
    if (_state != State::Rec) {
        framework::TouchPad::get().update();
    }

    // 按住侧键说话（hold-to-talk）
    bool holding = GetHAL().btnB.isHolding();
    if (holding && _state != State::Rec) {
        _start_record();
    } else if (!holding && _state == State::Rec) {
        _stop_record();
    }

    // 录音任务自然结束（如任务异常退出）
    if (_state == State::Rec && _record_task == nullptr) {
        _set_state(State::Asr);
    }

    // Pasting 反馈超时回 Idle
    if (_state == State::Pasting && GetHAL().millis() >= _feedback_until_ms) {
        _set_state(State::Idle);
    }

    _update_labels();
}

void AppVoiceCube::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _stop_record();

    // 退出模式：停止广播省电（NimBLE 栈常驻）
    framework::BleVoice::get().stopAdvertising();

    _key_manager.reset();

    LvglLockGuard lock;
    _confirm_button.reset();
    _cancel_button.reset();
    if (_status_label != nullptr) {
        lv_obj_delete(_status_label);
        _status_label = nullptr;
    }
    if (_preview_label != nullptr) {
        lv_obj_delete(_preview_label);
        _preview_label = nullptr;
    }
    if (_hint_label != nullptr) {
        lv_obj_delete(_hint_label);
        _hint_label = nullptr;
    }
}

// ---------------- 录音任务 ----------------

void AppVoiceCube::_start_record()
{
    if (_record_task != nullptr) {
        return;
    }
    _record_stop = false;
    _seq         = 0;
    ++_session_id;
    _set_state(State::Rec);

    if (xTaskCreate(_record_task_entry, "vc_record", 8192, this, 3, &_record_task) != pdPASS) {
        _record_task = nullptr;
        mclog::tagError(_tag, "record task create failed");
        _set_state(State::Idle);
    }
}

void AppVoiceCube::_stop_record()
{
    if (_record_task != nullptr) {
        _record_stop = true;
        // 等待任务自行退出（任务在块边界检查停止标志）
        for (int i = 0; i < 50 && _record_task != nullptr; ++i) {
            GetHAL().delay(20);
        }
        if (_record_task != nullptr) {
            vTaskDelete(_record_task);
            _record_task = nullptr;
        }
    }
}

void AppVoiceCube::_record_task_entry(void* arg)
{
    auto* self = static_cast<AppVoiceCube*>(arg);

    // 音频通道互斥：录音排他（频谱/其他录音请求被挡在外面）
    if (!framework::AudioMutex::get().acquireRecord(portMAX_DELAY)) {
        self->_record_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    if (audio_encoder_init(BOARD_AUDIO_SAMPLE_RATE, BOARD_AUDIO_CHANNELS, BOARD_AUDIO_FRAME_MS) != ESP_OK) {
        framework::AudioMutex::get().releaseRecord();
        self->_record_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    mclog::tagInfo(_tag, "recording start, session={}", self->_session_id);

    // 16k 重采样缓冲 + 60ms Opus 帧缓冲
    static int16_t resampled[_dst_chunk_samples];
    static int16_t frame_pcm[BOARD_AUDIO_FRAME_SAMPLES];
    static uint8_t opus_out[400];
    size_t frame_fill = 0;

    while (!self->_record_stop) {
        std::vector<int16_t> chunk;
        GetHAL().audioRecord(chunk, _record_chunk_ms, 30.0f);
        if (chunk.empty()) {
            continue;
        }

        // 44.1k → 16k 重采样
        size_t dst_len = resample_44100_to_16000(chunk.data(), chunk.size(), resampled, _dst_chunk_samples);

        // 攒满 960 样本（60ms）编码一帧
        size_t i = 0;
        while (i < dst_len && !self->_record_stop) {
            size_t need = BOARD_AUDIO_FRAME_SAMPLES - frame_fill;
            size_t take = (dst_len - i < need) ? (dst_len - i) : need;
            memcpy(&frame_pcm[frame_fill], &resampled[i], take * sizeof(int16_t));
            frame_fill += take;
            i += take;

            if (frame_fill == BOARD_AUDIO_FRAME_SAMPLES) {
                size_t out_len = 0;
                if (audio_encode_frame(frame_pcm, BOARD_AUDIO_FRAME_SAMPLES, opus_out, sizeof(opus_out), &out_len) ==
                        ESP_OK &&
                    out_len > 0) {
                    uint8_t flags = (self->_seq == 0) ? _flag_start : 0;
                    framework::BleVoice::get().sendAudioFrame(opus_out, out_len, self->_session_id, self->_seq, flags);
                    ++self->_seq;
                }
                frame_fill = 0;
            }
        }
    }

    // 发送结束帧（有内容时标记 end；空录音不发 end）
    if (self->_seq > 0) {
        framework::BleVoice::get().sendAudioFrame(nullptr, 0, self->_session_id, self->_seq, _flag_end);
        mclog::tagInfo(_tag, "recording done, session={} frames={}", self->_session_id, self->_seq);
    }

    audio_encoder_deinit();
    framework::AudioMutex::get().releaseRecord();

    self->_record_task = nullptr;
    vTaskDelete(nullptr);
}

// ---------------- 控制帧与状态 ----------------

void AppVoiceCube::_on_control(const std::string& json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        mclog::tagWarn(_tag, "bad control json: {}", json);
        return;
    }
    const char* event = doc["event"];
    if (event == nullptr) {
        return;
    }

    if (strcmp(event, "asr") == 0) {
        // 识别结果下行：预览（不自动粘贴）
        _preview_text = doc["text"] | "";
        _set_state(State::Preview);
        mclog::tagInfo(_tag, "asr result: {}", _preview_text);
    } else if (strcmp(event, "paste_result") == 0) {
        bool ok = doc["ok"] | false;
        if (ok) {
            mclog::tagInfo(_tag, "pasted");
            _feedback_until_ms = GetHAL().millis() + 1500;
            _preview_text.clear();
            _set_state(State::Pasting);
        } else {
            mclog::tagWarn(_tag, "paste failed");
        }
    }
}

void AppVoiceCube::_send_mouse(int dx, int dy, uint8_t btn, const char* action)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"event\":\"mouse\",\"dx\":%d,\"dy\":%d,\"btn\":%u,\"action\":\"%s\"}", dx, dy, btn,
             action);
    framework::BleVoice::get().sendStateJson(buf);
}

void AppVoiceCube::_set_state(State s)
{
    _state = s;
    mclog::tagInfo(_tag, "state -> {}", static_cast<int>(s));
    _update_labels();
}

void AppVoiceCube::_update_labels()
{
    LvglLockGuard lock;
    const char* status = "";
    const char* hint   = "";
    bool show_buttons  = false;

    switch (_state) {
    case State::Idle:
        if (framework::BleVoice::get().isConnected()) {
            status = "已连接桌面端";
            hint   = "按住侧键说话 · 触摸板=鼠标";
        } else {
            status = "等待连接电脑…";
            hint   = "打开电脑端程序后自动连接";
        }
        break;
    case State::Rec:
        status = "录音中… 松开侧键结束";
        break;
    case State::Asr:
        status = "识别中…";
        break;
    case State::Preview:
        status = "识别结果（未粘贴）";
        hint   = "移动光标到目标位置，点确认粘贴";
        show_buttons = true;
        break;
    case State::Pasting:
        status = "已粘贴 / 已取消";
        break;
    }

    lv_label_set_text(_status_label, status);
    lv_label_set_text(_hint_label, hint);
    lv_label_set_text(_preview_label, _preview_text.c_str());

    if (show_buttons) {
        lv_obj_clear_flag(_confirm_button->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_cancel_button->get(), LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_confirm_button->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_cancel_button->get(), LV_OBJ_FLAG_HIDDEN);
    }
}
