#!/usr/bin/env python3
"""把 TTF 转成 TFT_eSPI 抗锯齿字库（.vlw 格式的 C 数组）。

用法：python3 tools/make_vlw.py   （需要 pip install pillow）
输出：include/fonts/<名称>.h

数字统一成等宽（tabular），这样时钟和数值跳动时文字不会左右晃。
"""
import os
import struct
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TTF = os.path.join(ROOT, "tools", "fonts", "Nunito-VariableWght.ttf")
OUT = os.path.join(ROOT, "include", "fonts")

# (C 数组名, 像素大小, 字重)
FONTS = [
    ("Nunito12", 12, 700),
    ("Nunito14", 14, 650),
    ("Nunito26", 26, 700),
]

CHARS = [chr(c) for c in range(0x21, 0x7F)]
# 额外字符：原字形太小，按倍数放大后与小写字母中线对齐
EXTRA = {"\u221e": 1.5}  # ∞（长期有效的到期时间）


def render(size, weight):
    font = ImageFont.truetype(TTF, size)
    font.set_variation_by_axes([weight])

    digit_adv = max(round(font.getlength(d)) for d in "0123456789")
    glyphs = []
    for ch in CHARS:
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = max(1, x1 - x0), max(1, y1 - y0)
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        tight = img.getbbox()
        adv = round(font.getlength(ch))
        if tight:
            img = img.crop(tight)
            x0 += tight[0]
            y0 += tight[1]
        else:
            img = Image.new("L", (0, 0))
        dx = x0
        if ch.isdigit():
            dx += (digit_adv - adv) // 2
            adv = digit_adv
        glyphs.append((ord(ch), img, adv, -y0, dx))

    xheight = -font.getbbox("x", anchor="ls")[1]
    for ch, scale in EXTRA.items():
        big = ImageFont.truetype(TTF, round(size * scale))
        big.set_variation_by_axes([weight])
        x0, y0, x1, y1 = big.getbbox(ch, anchor="ls")
        img = Image.new("L", (x1 - x0, y1 - y0), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=big, fill=255, anchor="ls")
        img = img.crop(img.getbbox())
        dy = round(xheight / 2 + img.height / 2)
        glyphs.append((ord(ch), img, img.width + 1, dy, 0))

    ascent = -font.getbbox("d", anchor="ls")[1]
    descent = font.getbbox("p", anchor="ls")[3]

    data = struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent)
    for code, img, adv, dy, dx in glyphs:
        data += struct.pack(">7i", code, img.height, img.width, adv, dy, dx, 0)
    for _, img, *_ in glyphs:
        data += img.tobytes()
    return data


def write_header(name, data):
    lines = []
    for i in range(0, len(data), 24):
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in data[i:i + 24]) + ",")
    body = "\n".join(lines)
    with open(os.path.join(OUT, f"{name}.h"), "w") as f:
        f.write(f"// 由 tools/make_vlw.py 生成，字体 Nunito（SIL Open Font License）\n#pragma once\n"
                f"#include <pgmspace.h>\n\nconst uint8_t {name}[] PROGMEM = {{\n{body}\n}};\n")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, size, weight in FONTS:
        data = render(size, weight)
        write_header(name, data)
        print(f"{name}: {len(data)} bytes")
