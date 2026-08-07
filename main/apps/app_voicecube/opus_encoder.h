#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Opus encoder
 */
esp_err_t audio_encoder_init(uint32_t sample_rate, uint8_t channels, uint32_t frame_ms);

/**
 * @brief Encode PCM data to Opus packet
 * @note When OPUS_LIB_AVAILABLE is not defined, this is a stub that returns ESP_OK
 *       with output_len=0 (dropping audio).
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

#ifdef __cplusplus
}
#endif
