/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 公共 Opus 编解码封装（阶段三：小智 AI 对话）
 *
 * 由 main/apps/app_voicecube/opus_encoder.c 上移而来，保留 audio_encoder_*
 * 符号兼容（AppVoiceCube 仅改 include 路径）；新增 audio_decoder_* 对称 API。
 * 底层为 components/opus 完整库（encoder + decoder 均已编译）。
 */

/* ------------------------------ Encoder ------------------------------ */

/**
 * @brief Initialize the Opus encoder
 */
esp_err_t audio_encoder_init(uint32_t sample_rate, uint8_t channels, uint32_t frame_ms);

/**
 * @brief Encode PCM data to Opus packet
 */
esp_err_t audio_encode_frame(const int16_t *pcm, size_t pcm_samples,
                             uint8_t *output, size_t output_size,
                             size_t *output_len);

/**
 * @brief Reset the encoder state (for new session)
 */
void audio_encoder_reset(void);

/**
 * @brief Deinitialize and free Opus encoder
 */
void audio_encoder_deinit(void);

/**
 * @brief Get the expected PCM samples per frame
 */
size_t audio_encoder_frame_samples(void);

/* ------------------------------ Decoder ------------------------------ */

/**
 * @brief Initialize the Opus decoder
 */
esp_err_t audio_decoder_init(uint32_t sample_rate, uint8_t channels, uint32_t frame_ms);

/**
 * @brief Decode Opus packet to PCM data
 * @note output 容量至少 audio_decoder_frame_samples() * 2 字节
 */
esp_err_t audio_decode_frame(const uint8_t *input, size_t input_len,
                             int16_t *output, size_t output_capacity_samples,
                             size_t *output_samples);

/**
 * @brief Reset the decoder state (for new session / abort)
 */
void audio_decoder_reset(void);

/**
 * @brief Deinitialize and free Opus decoder
 */
void audio_decoder_deinit(void);

/**
 * @brief Get the expected PCM samples per frame
 */
size_t audio_decoder_frame_samples(void);

#ifdef __cplusplus
}
#endif
