# -*- coding: utf-8 -*-
"""
生成全量中文字体：GB2312 一级 3755 字（lv_font_cn_full_24.c）+ 二级 3008 字（lv_font_cn_full_2_24.c）。

用途：AI 对话字幕（TTS 文本是服务器动态生成的任意中文，342 字小字体必然方块）。
- RLE 压缩（lv_font_conv 新版默认）控制 flash 体积
- 6763 字 --range 命令行超 Windows 32KB 限制（WinError 206）→ 拆两个字体，
  full_1.fallback = full_2 串联（LVGL 支持多级 fallback 递归）
用法：LV_FONT_CONV=<lv_font_conv.js> python gen_cn_fonts_full.py
"""
import os
import subprocess
import shutil

ROOT = os.path.dirname(os.path.abspath(__file__))
FONT_SRC = os.path.join(
    ROOT, "components", "lvgl", "scripts", "built_in_font", "SourceHanSansSC-Normal.otf"
)
FONT_DIR = os.path.join(ROOT, "main", "assets", "fonts")


def find_node():
    return shutil.which("node") or shutil.which("node.exe") or "node"


def find_conv():
    env = os.environ.get("LV_FONT_CONV")
    if env and os.path.exists(env):
        return env
    candidates = [
        os.path.join(ROOT, "node_modules", "lv_font_conv", "lv_font_conv.js"),
        "/tmp/node_modules/lv_font_conv/lv_font_conv.js",
        os.path.expanduser("~/node_modules/lv_font_conv/lv_font_conv.js"),
    ]
    for p in candidates:
        if os.path.exists(p):
            return p
    raise SystemExit("未找到 lv_font_conv.js（npm install lv_font_conv 后设置 LV_FONT_CONV）")


def build_chars(row_start, row_end, extra=""):
    """GB2312 区段字符集：row_start..row_end（区号 0xB0 起）+ extra 附加字符"""
    chars = set(extra)
    for row in range(row_start, row_end):
        for col in range(0xA1, 0xFF):
            if row == 0xD7 and col > 0xF9:
                continue
            try:
                ch = bytes([row, col]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            chars.add(ch)
    return chars


def build_range(chars):
    ranges = []
    cur = None
    for cp in sorted(chars):
        if cur is None:
            cur = [cp, cp]
        elif ord(cp) == ord(cur[1]) + 1:
            cur[1] = cp
        else:
            ranges.append(cur)
            cur = [cp, cp]
    if cur:
        ranges.append(cur)
    return ",".join(
        "0x%x-0x%x" % (ord(a), ord(b)) if a != b else "0x%x" % ord(a) for a, b in ranges
    )


def gen_font(out_name, font_sym, range_str, extra_comment=""):
    out = os.path.join(FONT_DIR, out_name)
    node = find_node()
    conv = find_conv()
    cmd = [
        node, conv,
        "--bpp", "2", "--size", "24",  # 默认 RLE 压缩
        "--font", FONT_SRC,
        "--range", range_str,
        "--format", "lvgl",
        "-o", out,
    ]
    print(f"> lv_font_conv {out_name}{extra_comment}")
    subprocess.run(cmd, check=True, cwd=ROOT)

    text = open(out, encoding="utf-8").read()
    old = "const lv_font_t %s = {" % font_sym
    new = 'const lv_font_t %s __attribute__((section(".dram0.data"))) = {' % font_sym
    assert old in text, f"{old} not found in {out}"
    text = text.replace(old, new)
    open(out, "w", encoding="utf-8").write(text)
    print(f"OK  {out}")


def main():
    # 公共附加字符：ASCII 可打印 + 常用标点
    extra = "".join(chr(cp) for cp in range(0x20, 0x7F))
    extra += "，。！？；：、（）《》〈〉「」『』…·—～％°℃×÷＋－＝【】"

    chars1 = build_chars(0xB0, 0xD8, extra)  # 一级 3755 + ASCII + 标点
    chars2 = build_chars(0xD8, 0xF8)          # 二级 3008（GB2312 二级区 56-87）
    print(f"level1: {len(chars1)} chars, level2: {len(chars2)} chars")

    gen_font("lv_font_cn_full_24.c", "lv_font_cn_full_24", build_range(chars1))
    gen_font("lv_font_cn_full_2_24.c", "lv_font_cn_full_2_24", build_range(chars2), " (level 2)")


if __name__ == "__main__":
    main()
