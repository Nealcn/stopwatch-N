/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// VoiceCube 桌面模式音频参数（与 Nealcn/VoiceCube 固件一致：
// 16kHz mono / 60ms Opus 帧，桌面端 ASR 按此采样率消费）
#pragma once

#define BOARD_AUDIO_SAMPLE_RATE 16000  // 16 kHz for ASR
#define BOARD_AUDIO_BITS        16     // 16-bit PCM
#define BOARD_AUDIO_CHANNELS    1      // Mono
#define BOARD_AUDIO_FRAME_MS    60     // 60 ms per Opus frame
#define BOARD_AUDIO_FRAME_SAMPLES (BOARD_AUDIO_SAMPLE_RATE * BOARD_AUDIO_FRAME_MS / 1000)  // 960 samples
