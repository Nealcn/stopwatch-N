/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_mutex.h"
#include <mooncake_log.h>

// TODO(阶段二): 基于 FreeRTOS 互斥量实现（xSemaphoreCreateMutex，参照 hal_display.cpp）
// 当前为骨架桩实现，全部返回可获取/空闲，编译通过后由阶段二任务补齐语义。

namespace framework {

AudioMutex& AudioMutex::get()
{
    static AudioMutex _instance;
    return _instance;
}

bool AudioMutex::acquireRecord(uint32_t timeoutMs)
{
    // TODO: 互斥量 Take + HAL getAudioBusy() 联动
    mclog::tagWarn("AudioMutex", "acquireRecord: stub, not implemented yet");
    return true;
}

void AudioMutex::releaseRecord()
{
    // TODO: 互斥量 Give
}

bool AudioMutex::acquirePlay(uint32_t timeoutMs)
{
    // TODO: 互斥量 Take，可被打断语义
    mclog::tagWarn("AudioMutex", "acquirePlay: stub, not implemented yet");
    return true;
}

void AudioMutex::releasePlay()
{
    // TODO: 互斥量 Give
}

bool AudioMutex::isRecordBusy() const
{
    return false;
}

bool AudioMutex::isPlayBusy() const
{
    return false;
}

bool AudioMutex::tryAcquireSpectrum()
{
    // TODO: 录音通道共享读（录音空闲允许）
    return true;
}

void AudioMutex::releaseSpectrum()
{
    // TODO
}

}  // namespace framework
