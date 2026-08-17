/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ai_resample.h"

/* 线性插值重采样：输出点 = 输入位置 * (in_rate / out_rate) */
size_t ai_resample_linear(const int16_t* in, size_t in_len,
                          uint32_t in_rate, uint32_t out_rate,
                          int16_t* out, size_t out_cap)
{
    if (in == nullptr || out == nullptr || in_len == 0 || in_rate == 0 || out_rate == 0) {
        return 0;
    }

    // 同采样率直接拷贝
    if (in_rate == out_rate) {
        size_t n = (in_len < out_cap) ? in_len : out_cap;
        for (size_t i = 0; i < n; ++i) {
            out[i] = in[i];
        }
        return n;
    }

    const double step = (double)in_rate / (double)out_rate;
    size_t out_idx    = 0;
    for (double phase = 0.0; phase < in_len - 1 && out_idx < out_cap; phase += step, ++out_idx) {
        size_t i0    = static_cast<size_t>(phase);
        float frac   = static_cast<float>(phase - i0);
        float sample = in[i0] * (1.0f - frac) + in[i0 + 1] * frac;
        out[out_idx] = static_cast<int16_t>(sample);
    }
    return out_idx;
}
