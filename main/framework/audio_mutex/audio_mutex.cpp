/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_mutex.h"
#include <mooncake_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// 全局音频通道语义互斥锁（阶段二实现）
//
// 语义：
//  - 录音通道（_record_sem）排他：audioRecord 阻塞读 + 频谱读取同源
//  - 播放通道（_play_sem）排他但可被打断：audioPlay 的 async 打断机制已覆盖
//  - 频谱读取 = 录音通道的共享读：录音空闲（锁可拿）才允许，录音中返回 false
//
// 用法约定：应用在 onOpen 中 acquire、onClose 中 release；
// 播放/录音结束后调用方应尽快 release，避免长占通道。

namespace framework {
namespace {

SemaphoreHandle_t _record_sem = nullptr;
SemaphoreHandle_t _play_sem   = nullptr;

void ensure_init()
{
    if (_record_sem != nullptr) {
        return;
    }
    _record_sem = xSemaphoreCreateMutex();
    _play_sem   = xSemaphoreCreateMutex();
    mclog::tagInfo("AudioMutex", "mutexes created");
}

TickType_t to_ticks(uint32_t timeoutMs)
{
    return (timeoutMs == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
}

}  // namespace

AudioMutex& AudioMutex::get()
{
    static AudioMutex _instance;
    return _instance;
}

bool AudioMutex::acquireRecord(uint32_t timeoutMs)
{
    ensure_init();
    return xSemaphoreTake(_record_sem, to_ticks(timeoutMs)) == pdTRUE;
}

void AudioMutex::releaseRecord()
{
    ensure_init();
    xSemaphoreGive(_record_sem);
}

bool AudioMutex::acquirePlay(uint32_t timeoutMs)
{
    ensure_init();
    return xSemaphoreTake(_play_sem, to_ticks(timeoutMs)) == pdTRUE;
}

void AudioMutex::releasePlay()
{
    ensure_init();
    xSemaphoreGive(_play_sem);
}

bool AudioMutex::isRecordBusy() const
{
    ensure_init();
    return uxSemaphoreGetCount(_record_sem) == 0;
}

bool AudioMutex::isPlayBusy() const
{
    ensure_init();
    return uxSemaphoreGetCount(_play_sem) == 0;
}

bool AudioMutex::tryAcquireSpectrum()
{
    ensure_init();
    // 录音通道共享读：录音占用中拿不到锁即拒绝
    return xSemaphoreTake(_record_sem, 0) == pdTRUE;
}

void AudioMutex::releaseSpectrum()
{
    ensure_init();
    xSemaphoreGive(_record_sem);
}

}  // namespace framework
