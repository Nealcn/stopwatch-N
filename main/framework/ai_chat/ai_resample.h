/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通用线性插值重采样（阶段三：小智 AI 对话）
 *
 * 泛化自 app_voicecube.cpp 的 resample_44100_to_16000()。
 * 无抗混叠滤波（语音场景可接受，已知限制）。
 *
 * 覆盖场景：
 *  - 上行：44.1kHz → 16kHz（录音 → 服务器）
 *  - 下行：16/24/48kHz → 44.1kHz（服务器 TTS → 硬件 codec）
 *
 * @param in      输入 PCM
 * @param in_len  输入样本数
 * @param in_rate 输入采样率
 * @param out_rate 输出采样率
 * @param out     输出缓冲（容量 out_cap）
 * @param out_cap 输出缓冲样本容量
 * @return 实际输出样本数
 */
size_t ai_resample_linear(const int16_t* in, size_t in_len,
                          uint32_t in_rate, uint32_t out_rate,
                          int16_t* out, size_t out_cap);

#ifdef __cplusplus
}
#endif
