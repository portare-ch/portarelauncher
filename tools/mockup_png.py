#!/usr/bin/env python3
"""Render the proposed screens of docs/mockup.txt as images, from the same
lists tools/mockup.py prints and the font the launcher draws with
(src/font8x16.h), at SCALE 3 on the 1280x960 panel in the amber palette.
One PNG per screen in docs/images/, so a layout can be judged as it
would look rather than imagined from text. Pure Python: no ImageMagick,
no PIL.

    python3 tools/mockup_png.py
"""
import os, re, struct, sys, zlib
sys.path.insert(0, os.path.dirname(__file__))
import mockup

W, H, SCALE = 1280, 960, 3
COLS, ROWS = mockup.COLS, mockup.ROWS
OX, OY = (W - COLS * 8 * SCALE) // 2, (H - ROWS * 16 * SCALE) // 2
# color.c, "amber": background, dim, mid, text, bright
BG, DIM, MID, TEXT, BRIGHT = (0, 0, 0), (0x66, 0x3d, 0), (0xb3, 0x74, 0), (0xff, 0xb0, 0), (0xff, 0xd9, 0x8a)
CP437 = {"═": 0xCD, "─": 0xC4, "▸": 0x10, "↑": 0x18, "↓": 0x19,
         "♥": 0x03, "›": 0x3E, "…": 0x2E}

def font():
    src = open(os.path.join(os.path.dirname(__file__), "..", "src", "font8x16.h")).read()
    body = re.sub(r"/\*.*?\*/", "", src.split("font8x16[4096] = {")[1], flags=re.S)
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
    assert len(data) == 4096
    return data

def png(path, px):
    raw = b"".join(b"\x00" + bytes(px[y * W * 3:(y + 1) * W * 3]) for y in range(H))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def render(lines, path, glyphs):
    px = bytearray(W * H * 3)
    def put(col, row, ch, color):
        g = CP437.get(ch, ord(ch) if ord(ch) < 128 else 0x3F)
        x0, y0 = OX + col * 8 * SCALE, OY + row * 16 * SCALE
        for yy in range(16):
            bits = glyphs[g * 16 + yy]
            for xx in range(8):
                if bits & (0x80 >> xx):
                    for sy in range(SCALE):
                        base = ((y0 + yy * SCALE + sy) * W + x0 + xx * SCALE) * 3
                        for sx in range(SCALE):
                            px[base + sx * 3:base + sx * 3 + 3] = bytes(color)
    for r, line in enumerate(lines[:ROWS]):
        rule = line.strip() and set(line.strip()) <= {"═", "─"}
        selected = "▸" in line
        for c, ch in enumerate(line[:COLS]):
            if ch == " ":
                continue
            color = MID if rule else BRIGHT if selected else TEXT
            put(c, r, ch, color)
    png(path, px)
    print("wrote", os.path.relpath(path))

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__), "..", "docs", "images")
    os.makedirs(out, exist_ok=True)
    glyphs = font()
    for name, lines in (("systems-quick-access", mockup.systems_quick),
                        ("games-long-titles", mockup.games_long),
                        ("games-favourite-selected", mockup.games_fav),
                        ("recently-played", mockup.recent),
                        ("favourites", mockup.favourites)):
        render(lines, os.path.join(out, name + ".png"), glyphs)
