#!/usr/bin/env python3
"""生成 AppAiChat 启动器图标（200x200 RGB565，对话气泡）

用法：python tools/gen_icon_ai_chat.py
输出：main/assets/images/icon_ai_chat.c（需在 assets.h 声明 icon_ai_chat）
"""
import os
import struct

SIZE = 200

# 颜色（RGB）
BG = (0x00, 0x00, 0x00)          # 黑底
BUBBLE = (0x7F, 0xD0, 0xFF)      # 浅蓝气泡
DOT = (0xFF, 0xFF, 0xFF)         # 白点


def rgb565(r, g, b):
    return ((r * 31 // 255) << 11) | ((g * 63 // 255) << 5) | (b * 31 // 255)


def sd_round_rect(x, y, cx, cy, hw, hh, radius):
    """有符号距离：到圆角矩形边界（<0 在内）"""
    dx = abs(x - cx) - (hw - radius)
    dy = abs(y - cy) - (hh - radius)
    ax, ay = max(dx, 0.0), max(dy, 0.0)
    return min(max(dx, dy), 0.0) + (ax * ax + ay * ay) ** 0.5 - radius


def in_triangle(px, py, a, b, c):
    """点在三角形内（重心法）"""
    def sign(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
    d1 = sign((px, py), a, b)
    d2 = sign((px, py), b, c)
    d3 = sign((px, py), c, a)
    has_neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
    has_pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
    return not (has_neg and has_pos)


def pixel_color(x, y):
    # 气泡主体（圆角矩形 + 左下尾巴）
    body = sd_round_rect(x, y, 100, 95, 82, 62, 36)
    tail = in_triangle(x, y, (55, 150), (100, 150), (75, 195))
    if body < 0 or (tail and x < 108 and y > 140):
        color = BUBBLE
        # 三个白色圆点
        for (dx, dy, r) in [(75, 95, 10), (100, 95, 10), (125, 95, 10)]:
            if (x - dx) ** 2 + (y - dy) ** 2 <= r * r:
                color = DOT
        return color
    return BG


def main():
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            r, g, b = pixel_color(x, y)
            row.append(rgb565(r, g, b))
        rows.append(row)

    out_path = os.path.join(os.path.dirname(__file__), "..", "main", "assets", "images", "icon_ai_chat.c")
    out_path = os.path.normpath(out_path)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("""#ifdef __has_include
#if __has_include("lvgl.h")
#ifndef LV_LVGL_H_INCLUDE_SIMPLE
#define LV_LVGL_H_INCLUDE_SIMPLE
#endif
#endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

LV_ATTRIBUTE_MEM_ALIGN
const uint8_t icon_ai_chat_map[] = {
""")
        for y in range(SIZE):
            f.write("  " + ", ".join(f"0x{v >> 8:02x}, 0x{v & 0xff:02x}" for v in rows[y]) + ",\n")
        f.write("""};

const lv_image_dsc_t icon_ai_chat = {
    .header.cf    = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w     = %d,
    .header.h     = %d,
    .data_size    = %d * %d * 2,
    .data         = icon_ai_chat_map,
};
""" % (SIZE, SIZE, SIZE, SIZE))
    print(f"生成完成: {out_path}")


if __name__ == "__main__":
    main()
