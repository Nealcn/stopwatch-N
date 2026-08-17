# -*- coding: utf-8 -*-
"""
生成 200x200 RGB565 LVGL 图标 C 数组，替换 icon_pomodoro.c / icon_voicecube.c 的像素数组。
风格：黑底 + 深色调几何图案（与 icon_stopwatch / icon_lucky_wheel 一致）。
用法：
    python gen_icons.py            # 生成并替换两个图标 .c 文件
    python gen_icons.py --preview  # 额外输出 preview_pomodoro.bmp / preview_voicecube.bmp
"""
import os
import re
import struct
import sys

SIZE = 200


def rgb(r, g, b):
    r5 = (r * 31 + 127) // 255
    g6 = (g * 63 + 127) // 255
    b5 = (b * 31 + 127) // 255
    return (r5 << 11) | (g6 << 5) | b5


BLACK = 0x0000


def fill_circle(px, cx, cy, r, color):
    for y in range(max(0, cy - r), min(SIZE, cy + r + 1)):
        for x in range(max(0, cx - r), min(SIZE, cx + r + 1)):
            dx, dy = x - cx, y - cy
            if dx * dx + dy * dy <= r * r:
                px[y][x] = color


def fill_round_rect(px, x0, y0, x1, y1, rad, color):
    """圆角矩形 SDF：qx/qy 为到内部矩形边的距离，圆角处按角距离判定"""
    rad = min(rad, (x1 - x0) // 2, (y1 - y0) // 2)
    for y in range(max(0, y0), min(SIZE, y1 + 1)):
        for x in range(max(0, x0), min(SIZE, x1 + 1)):
            qx = max(x0 + rad - x, x - (x1 - rad), 0)
            qy = max(y0 + rad - y, y - (y1 - rad), 0)
            if qx * qx + qy * qy <= rad * rad:
                px[y][x] = color


def fill_arc(px, cx, cy, r, thickness, color, ymin=None):
    """环形圆弧（y >= ymin 时画，用于 U 形支架）"""
    for y in range(max(0, cy - r - thickness), min(SIZE, cy + r + thickness + 1)):
        if ymin is not None and y < ymin:
            continue
        for x in range(max(0, cx - r - thickness), min(SIZE, cx + r + thickness + 1)):
            d = abs(((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 - r)
            if d <= thickness / 2:
                px[y][x] = color


def draw_pomodoro():
    px = [[BLACK] * SIZE for _ in range(SIZE)]
    body = rgb(185, 36, 26)     # 深红
    body_dark = rgb(140, 24, 16)  # 更暗红（描边）
    glow = rgb(215, 70, 45)     # 高光
    leaf = rgb(30, 118, 48)     # 深绿
    leaf_dark = rgb(22, 90, 36)

    fill_circle(px, 100, 112, 60, body)
    fill_arc(px, 100, 112, 58, 5, body_dark)          # 暗描边
    fill_circle(px, 78, 84, 22, glow)                 # 右上高光
    # 果柄
    fill_round_rect(px, 93, 44, 107, 62, 5, leaf_dark)
    # 叶片（左右各一大一小）
    fill_circle(px, 66, 62, 22, leaf)
    fill_circle(px, 48, 84, 14, leaf)
    fill_circle(px, 134, 62, 22, leaf)
    fill_circle(px, 152, 84, 14, leaf)
    return px


def draw_voicecube():
    px = [[BLACK] * SIZE for _ in range(SIZE)]
    main = rgb(40, 128, 164)    # 深青蓝
    glow = rgb(62, 152, 188)    # 亮青蓝（内高光）
    # 话筒胶囊
    fill_round_rect(px, 72, 20, 128, 118, 28, main)
    fill_round_rect(px, 82, 30, 118, 108, 18, glow)
    # U 形支架（下半圆弧）
    fill_arc(px, 100, 128, 40, 10, main, ymin=128)
    # 底座圆点
    fill_circle(px, 100, 166, 9, main)
    return px


def to_c_array(px):
    """RGB565 小端字节序，每行 16 个 0xXX, 与现有文件格式一致"""
    flat = [v for row in px for v in row]
    assert len(flat) == SIZE * SIZE, f"pixel count {len(flat)}"
    lines = []
    for i in range(0, len(flat), 16):
        chunk = flat[i : i + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in struct.pack("<%dH" % len(chunk), *chunk)) + ",")
    return "\n".join(lines) + "\n"


def replace_map(path, px):
    """只替换 map 数组体，保留文件其余结构"""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    pat = re.compile(r"(const uint8_t \w+_map\[\] = \{\n)(.*?)(\n\};\n)", re.S)
    m = pat.search(text)
    assert m, f"map array not found in {path}"
    new_text = text[: m.start(2)] + to_c_array(px).rstrip("\n") + text[m.end(2) :]
    with open(path, "w", encoding="utf-8") as f:
        f.write(new_text)
    # 自检：0x 出现次数 = 80000
    cnt = re.findall(r"0x[0-9A-F]{2}", new_text)
    assert len(cnt) == SIZE * SIZE * 2, f"byte count {len(cnt)}"
    print(f"OK  {path}  ({len(cnt)} bytes)")


def write_bmp(path, px):
    """24 位 BMP 预览（黑底，底行优先，每行 4 字节对齐）"""
    row_bytes = (SIZE * 3 + 3) & ~3
    img_size = row_bytes * SIZE
    data = bytearray()
    for y in range(SIZE - 1, -1, -1):
        row = bytearray()
        for x in range(SIZE):
            v = px[y][x]
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            row += bytes((b, g, r))
        row += b"\x00" * (row_bytes - SIZE * 3)
        data += row
    header = bytearray()
    header += b"BM"
    header += struct.pack("<IHHI", 54 + img_size, 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, SIZE, SIZE, 1, 24, 0, img_size, 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(header + data)
    print(f"OK  {path}")


def main():
    root = os.path.dirname(os.path.abspath(__file__))
    img_dir = os.path.join(root, "main", "assets", "images")
    preview = "--preview" in sys.argv

    pomodoro = draw_pomodoro()
    replace_map(os.path.join(img_dir, "icon_pomodoro.c"), pomodoro)
    if preview:
        write_bmp(os.path.join(root, "preview_pomodoro.bmp"), pomodoro)

    voicecube = draw_voicecube()
    replace_map(os.path.join(img_dir, "icon_voicecube.c"), voicecube)
    if preview:
        write_bmp(os.path.join(root, "preview_voicecube.bmp"), voicecube)


if __name__ == "__main__":
    main()
