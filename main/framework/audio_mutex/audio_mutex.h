/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>

/**
 * @brief 全局音频通道语义互斥锁
 *
 * 同一时刻仅一个应用可占用录音/播放通道，杜绝多模块混音冲突（文档 3.1.1）。
 *
 * 语义设计（见《源码摸底报告》5.1）：
 *  - 录音通道 = 排他：audioRecord 阻塞读与频谱读取同源，录音中禁止其他模块占用
 *  - 播放通道 = 排他但可被打断：现有 audioPlay 的 async 打断机制配合"新请求通知替代强抢锁"
 *  - 频谱读取 = 录音通道的共享读：录音空闲允许，录音中拒绝（返回旧帧）
 *
 * 实现计划（阶段二）：
 *  - 基于 FreeRTOS 互斥量（参照 hal_display.cpp 的 xGuiSemaphore 模式）
 *  - 占用者通过 HAL 新增的 getAudioBusy() 与底层实际占用状态联动
 *  - 持有者退出（app onClose）时强制释放，防资源泄漏
 */
namespace framework {

class AudioMutex {
public:
    static AudioMutex& get();

    /* 录音通道（排他） */
    bool acquireRecord(uint32_t timeoutMs = 0xFFFFFFFF);
    void releaseRecord();

    /* 播放通道（排他） */
    bool acquirePlay(uint32_t timeoutMs = 0xFFFFFFFF);
    void releasePlay();

    /* 状态查询 */
    bool isRecordBusy() const;
    bool isPlayBusy() const;

    /* 频谱共享读：录音空闲返回 true 并占用，录音中返回 false */
    bool tryAcquireSpectrum();
    void releaseSpectrum();

private:
    AudioMutex()          = default;
    ~AudioMutex()         = default;
    AudioMutex(const AudioMutex&)            = delete;
    AudioMutex& operator=(const AudioMutex&) = delete;
};

}  // namespace framework
