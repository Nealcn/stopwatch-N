/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * @note 由 main/apps/app_voicecube/opus_encoder.c 上移（阶段三），
 *       保留 audio_encoder_* 符号兼容，新增 audio_decoder_*。
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

#include "opus.h"
#include "ai_opus.h"

static const char *TAG = "ai_opus";

#define OPUS_BITRATE 28000

/* ------------------------------ Encoder ------------------------------ */

static OpusEncoder *s_encoder     = NULL;
static void *s_prealloc           = NULL;  // 启动早期预分配的编码器内存（内部 RAM）
static uint32_t s_enc_sample_rate = 0;
static uint8_t s_enc_channels     = 0;
static uint32_t s_enc_frame_ms    = 0;
static size_t s_enc_frame_samples = 0;

esp_err_t audio_encoder_prealloc(void)
{
    if (s_prealloc) {
        return ESP_OK;
    }
    int enc_size = opus_encoder_get_size(1);
    s_prealloc   = heap_caps_malloc(enc_size, MALLOC_CAP_INTERNAL);
    if (!s_prealloc) {
        ESP_LOGE(TAG, "prealloc failed: free=%u largest=%u",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "preallocated %d bytes at %p", enc_size, s_prealloc);
    return ESP_OK;
}

esp_err_t audio_encoder_init(uint32_t sample_rate, uint8_t channels, uint32_t frame_ms)
{
    ESP_RETURN_ON_FALSE(sample_rate > 0, ESP_ERR_INVALID_ARG, TAG, "invalid sample rate");
    ESP_RETURN_ON_FALSE(channels == 1, ESP_ERR_INVALID_ARG, TAG, "only mono supported");
    ESP_RETURN_ON_FALSE(s_encoder == NULL, ESP_ERR_INVALID_STATE, TAG, "encoder already initialized");

    s_enc_sample_rate = sample_rate;
    s_enc_channels = channels;
    s_enc_frame_ms = frame_ms;
    s_enc_frame_samples = (sample_rate * frame_ms) / 1000;

    int enc_size = opus_encoder_get_size(channels);
    // 优先使用启动早期预分配的内存（内部 RAM，避免运行时碎片不足）
    OpusEncoder *enc = (OpusEncoder *)s_prealloc;
    if (enc) {
        ESP_LOGI(TAG, "Using preallocated internal RAM at %p", enc);
    } else {
        ESP_LOGI(TAG, "Allocating %d bytes from internal RAM...", enc_size);
        enc = (OpusEncoder *)heap_caps_calloc(1, enc_size, MALLOC_CAP_INTERNAL);
    }
    if (!enc) {
        // 编码器必须放内部 RAM：PSRAM 上的 Opus 编码器密集读写会触发
        // CPU cache 等待卡死 → 硬件看门狗复位（reason=7，实测）
        ESP_LOGE(TAG, "internal RAM alloc failed: free=%u largest=%u",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return ESP_ERR_NO_MEM;
    }
    s_prealloc = NULL;  // 内存已移交编码器

    int opus_err = opus_encoder_init(enc, sample_rate, channels, OPUS_APPLICATION_AUDIO);
    if (opus_err != OPUS_OK) {
        free(enc);
        ESP_LOGE(TAG, "opus_encoder_init failed: %d", opus_err);
        return ESP_ERR_NO_MEM;
    }
    s_encoder = enc;

    opus_encoder_ctl(s_encoder, OPUS_SET_BITRATE(OPUS_BITRATE));
    opus_encoder_ctl(s_encoder, OPUS_SET_COMPLEXITY(3));
    opus_encoder_ctl(s_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(s_encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(s_encoder, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(s_encoder, OPUS_SET_PACKET_LOSS_PERC(0));

    ESP_LOGI(TAG, "Opus encoder initialized: %u Hz, %u ch, %u ms, %d bps",
             sample_rate, channels, frame_ms, OPUS_BITRATE);
    return ESP_OK;
}

esp_err_t audio_encode_frame(const int16_t *pcm, size_t pcm_samples,
                             uint8_t *output, size_t output_size,
                             size_t *output_len)
{
    ESP_RETURN_ON_FALSE(s_encoder != NULL, ESP_ERR_INVALID_STATE, TAG, "encoder not initialized");
    ESP_RETURN_ON_FALSE(pcm != NULL, ESP_ERR_INVALID_ARG, TAG, "pcm is NULL");
    ESP_RETURN_ON_FALSE(output != NULL, ESP_ERR_INVALID_ARG, TAG, "output is NULL");

    opus_int32 encoded = opus_encode(s_encoder, pcm, (int)pcm_samples, output, (opus_int32)output_size);
    if (encoded < 0) {
        ESP_LOGE(TAG, "opus_encode failed: %d", (int)encoded);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *output_len = (size_t)encoded;
    return ESP_OK;
}

void audio_encoder_reset(void)
{
    if (s_encoder) {
        opus_encoder_ctl(s_encoder, OPUS_RESET_STATE);
    }
}

void audio_encoder_deinit(void)
{
    if (s_encoder) {
        // 编码器内存来自启动早期预分配（43KB 内部 RAM）。不能 opus_encoder_destroy()
        // （会 free 内存）：释放后第二次 init 重新分配时内部 RAM 碎片化
        // （最大连续块 < 43KB）→ alloc failed → 录音无声。归还预分配池复用，
        // 下次 init 对同一块内存重新 opus_encoder_init() 即可。
        s_prealloc = (void *)s_encoder;
        s_encoder  = NULL;
    }
    s_enc_sample_rate = 0;
    s_enc_channels = 0;
    s_enc_frame_ms = 0;
    s_enc_frame_samples = 0;
    ESP_LOGI(TAG, "Opus encoder deinitialized (memory returned to prealloc pool)");
}

size_t audio_encoder_frame_samples(void)
{
    return s_enc_frame_samples;
}

/* ------------------------------ Decoder ------------------------------ */

static OpusDecoder *s_decoder     = NULL;
static void *s_dec_prealloc       = NULL;  // 启动早期预分配的解码器内存（内部 RAM）
static uint32_t s_dec_sample_rate = 0;
static uint8_t s_dec_channels     = 0;
static uint32_t s_dec_frame_ms    = 0;
static size_t s_dec_frame_samples = 0;

esp_err_t audio_decoder_prealloc(void)
{
    if (s_dec_prealloc) {
        return ESP_OK;
    }
    int dec_size = opus_decoder_get_size(1);
    s_dec_prealloc = heap_caps_malloc(dec_size, MALLOC_CAP_INTERNAL);
    if (!s_dec_prealloc) {
        ESP_LOGE(TAG, "decoder prealloc failed: free=%u largest=%u",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "decoder preallocated %d bytes at %p", dec_size, s_dec_prealloc);
    return ESP_OK;
}

esp_err_t audio_decoder_init(uint32_t sample_rate, uint8_t channels, uint32_t frame_ms)
{
    ESP_RETURN_ON_FALSE(sample_rate > 0, ESP_ERR_INVALID_ARG, TAG, "invalid sample rate");
    ESP_RETURN_ON_FALSE(channels == 1, ESP_ERR_INVALID_ARG, TAG, "only mono supported");
    ESP_RETURN_ON_FALSE(s_decoder == NULL, ESP_ERR_INVALID_STATE, TAG, "decoder already initialized");

    s_dec_sample_rate = sample_rate;
    s_dec_channels = channels;
    s_dec_frame_ms = frame_ms;
    s_dec_frame_samples = (sample_rate * frame_ms) / 1000;

    int dec_size = opus_decoder_get_size(channels);
    // 优先用启动早期预分配的内部 RAM（避免运行时碎片不足和 PSRAM 上的 opus 风险）
    OpusDecoder *dec = (OpusDecoder *)s_dec_prealloc;
    if (dec) {
        ESP_LOGI(TAG, "using preallocated decoder memory at %p", dec);
    } else {
        dec = (OpusDecoder *)heap_caps_calloc(1, dec_size, MALLOC_CAP_INTERNAL);
        if (!dec) {
            ESP_LOGW(TAG, "internal alloc failed, trying PSRAM...");
            dec = (OpusDecoder *)heap_caps_calloc(1, dec_size, MALLOC_CAP_SPIRAM);
        }
    }
    if (!dec) {
        ESP_LOGE(TAG, "all memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    s_dec_prealloc = NULL;  // 内存已移交解码器

    int opus_err = opus_decoder_init(dec, sample_rate, channels);
    if (opus_err != OPUS_OK) {
        free(dec);
        ESP_LOGE(TAG, "opus_decoder_init failed: %d", opus_err);
        return ESP_ERR_NO_MEM;
    }
    s_decoder = dec;

    ESP_LOGI(TAG, "Opus decoder initialized: %u Hz, %u ch, %u ms",
             sample_rate, channels, frame_ms);
    return ESP_OK;
}

esp_err_t audio_decode_frame(const uint8_t *input, size_t input_len,
                             int16_t *output, size_t output_capacity_samples,
                             size_t *output_samples)
{
    ESP_RETURN_ON_FALSE(s_decoder != NULL, ESP_ERR_INVALID_STATE, TAG, "decoder not initialized");
    ESP_RETURN_ON_FALSE(input != NULL, ESP_ERR_INVALID_ARG, TAG, "input is NULL");
    ESP_RETURN_ON_FALSE(output != NULL, ESP_ERR_INVALID_ARG, TAG, "output is NULL");

    opus_int32 decoded = opus_decode(s_decoder, input, (opus_int32)input_len,
                                     output, (opus_int32)output_capacity_samples, 0);
    if (decoded < 0) {
        ESP_LOGE(TAG, "opus_decode failed: %d", (int)decoded);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *output_samples = (size_t)decoded;
    return ESP_OK;
}

void audio_decoder_reset(void)
{
    if (s_decoder) {
        opus_decoder_ctl(s_decoder, OPUS_RESET_STATE);
    }
}

void audio_decoder_deinit(void)
{
    if (s_decoder) {
        // 与 encoder 一致：解码器内存来自启动早期预分配（内部 RAM），不能
        // opus_decoder_destroy()（会 free）——释放后第二次 init 重新分配时内部
        // RAM 碎片化（最大连续块 < 预分配大小）→ alloc failed → 第二轮播放
        // 无声/失败（fallback PSRAM 有 cache 卡死风险）。归还预分配池复用，
        // 下次 init 对同一块内存重新 opus_decoder_init() 即可。
        if (esp_ptr_external_ram(s_decoder)) {
            // 预分配失败时的 PSRAM fallback：不能进预分配池（内部 RAM 语义），
            // 正常释放
            opus_decoder_destroy(s_decoder);
        } else {
            s_dec_prealloc = (void *)s_decoder;
        }
        s_decoder = NULL;
    }
    s_dec_sample_rate = 0;
    s_dec_channels = 0;
    s_dec_frame_ms = 0;
    s_dec_frame_samples = 0;
    ESP_LOGI(TAG, "Opus decoder deinitialized (memory returned to prealloc pool)");
}

size_t audio_decoder_frame_samples(void)
{
    return s_dec_frame_samples;
}
