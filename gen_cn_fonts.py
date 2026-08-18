# -*- coding: utf-8 -*-
"""
重新生成中文字体（lv_font_cn_24.c / lv_font_cn_26.c）。

策略：
1. 从 main/**/*.cpp|.h 的字符串字面量提取 CJK 字符（不扫注释）
2. 并集：现有字体头注释里的 --range 清单 ∪ 新提取字符（不丢字）
3. npx lv_font_conv 生成（思源黑体 SourceHanSansSC-Normal.otf，bpp 2，--format lvgl）
4. 后处理：lv_font_t 定义加 __attribute__((section(".dram0.data")))
   （否则运行时写 font->fallback 会 Cache error panic）

用法：python gen_cn_fonts.py
"""
import glob
import os
import re
import shutil
import subprocess

ROOT = os.path.dirname(os.path.abspath(__file__))
FONT_SRC = os.path.join(
    ROOT, "components", "lvgl", "scripts", "built_in_font", "SourceHanSansSC-Normal.otf"
)
FONT_DIR = os.path.join(ROOT, "main", "assets", "fonts")
EXIST_24 = os.path.join(FONT_DIR, "lv_font_cn_24.c")

LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
CJK = re.compile(r"[一-鿿＀-￯　-〿·…]")


def find_node():
    """探测 node 可执行文件（原 Windows 硬编码路径弃用）"""
    return shutil.which("node") or shutil.which("node.exe") or "node"


def find_conv():
    """探测 lv_font_conv.js，优先级：LV_FONT_CONV 环境变量 > 项目 node_modules > /tmp"""
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
    raise SystemExit(
        "未找到 lv_font_conv.js：npm install lv_font_conv 后设置 LV_FONT_CONV 指向其 lv_font_conv.js"
    )


def parse_range_list(text):
    """解析 lv_font_conv 的 --range 清单（'0x20-0x7f,0x4e00,...'）→ 字符集合"""
    chars = set()
    m = re.search(r"--range ([0-9a-fx,\.\- ]+) --format", text)
    assert m, "existing font range not found"
    for item in m.group(1).split(","):
        item = item.strip()
        if "-" in item:
            a, b = item.split("-")
            for cp in range(int(a, 16), int(b, 16) + 1):
                chars.add(chr(cp))
        else:
            chars.add(chr(int(item, 16)))
    return chars


def extract_literal_cjk():
    chars = set()
    for pat in ("main/**/*.cpp", "main/**/*.h", "main/**/*.c"):
        for fn in glob.glob(os.path.join(ROOT, pat), recursive=True):
            try:
                txt = open(fn, encoding="utf-8").read()
            except Exception:
                continue
            for m in LIT.finditer(txt):
                chars.update(CJK.findall(m.group(1)))
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
    parts = []
    for a, b in ranges:
        parts.append("0x%x-0x%x" % (ord(a), ord(b)) if a != b else "0x%x" % ord(a))
    return ",".join(parts)


def gen_font(size, out_name, range_str):
    out = os.path.join(FONT_DIR, out_name)
    # 直接调用 lv_font_conv（node 运行），避免 npx/PATH 问题；路径经 find_node/find_conv 探测
    node = find_node()
    conv = find_conv()
    cmd = [
        node, conv,
        "--no-compress", "--bpp", "2", "--size", str(size),
        "--font", FONT_SRC,
        "--range", range_str,
        "--format", "lvgl",
        "-o", out,
    ]
    print("> lv_font_conv --size", size)
    subprocess.run(cmd, check=True, cwd=ROOT)

    # 后处理：lv_font_t 放入可写 RAM 段（写 fallback 需要）
    text = open(out, encoding="utf-8").read()
    sym = "lv_font_cn_%d" % size
    old = "const lv_font_t %s = {" % sym
    new = 'const lv_font_t %s __attribute__((section(".dram0.data"))) = {' % sym
    assert old in text, f"{old} not found in {out}"
    text = text.replace(old, new)
    open(out, "w", encoding="utf-8").write(text)
    print(f"OK  {out}  (size {size}, {len(chars)} chars)")


if __name__ == "__main__":
    existing = parse_range_list(open(EXIST_24, encoding="utf-8").read())
    fresh = extract_literal_cjk()
    chars = existing | fresh
    range_str = build_range(chars)
    print(f"existing {len(existing)} + new {len(fresh - existing)} = {len(chars)} chars")
    gen_font(24, "lv_font_cn_24.c", range_str)
    gen_font(26, "lv_font_cn_26.c", range_str)
