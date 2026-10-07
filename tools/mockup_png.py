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
CP437 = {"═": 0xCD, "─": 0xC4, "▸": 0x10, "↑": 0x18, "↓": 0x19, "←": 0x1B, "→": 0x1A,
         "♥": 0x03, "›": 0x3E, "…": 0x2E, "┌": 0xDA, "┐": 0xBF, "└": 0xC0, "┘": 0xD9,
         "│": 0xB3, "·": 0xFA, "•": 0x07, "▲": 0x1E, "■": 0xFE, "○": 0x09, "×": 0x58}

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

def shapes_draw(px, shapes):
    """Geometry at the panel's resolution: rings for the sticks, thin
    outlines for the PS marks. Lines two pixels wide, nothing filled but
    the stick's dot."""
    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H:
            px[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes(c)
    def disc(cx, cy, r, c):
        for yy in range(-r, r + 1):
            half = int((r * r - yy * yy) ** 0.5)
            for xx in range(-half, half + 1):
                put(cx + xx, cy + yy, c)
    def ring(cx, cy, r, c, t=2):
        for yy in range(-r, r + 1):
            for xx in range(-r, r + 1):
                d2 = xx * xx + yy * yy
                if (r - t) * (r - t) <= d2 <= r * r:
                    put(cx + xx, cy + yy, c)
    def line(x0, y0, x1, y1, c, t=2):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            x = x0 + (x1 - x0) * i // n
            y = y0 + (y1 - y0) * i // n
            for dx in range(t):
                for dy in range(t):
                    put(x + dx, y + dy, c)
    cell_w, cell_h = 8 * SCALE, 16 * SCALE
    for g in shapes.get("rings", []):
        cx = OX + int(g["col"] * cell_w)
        cy = OY + int(g["row"] * cell_h) + cell_h // 2
        r = g["rows"] * cell_h // 2 - 6
        ring(cx, cy, r, BRIGHT if g["clicked"] else DIM)
        disc(cx + int(g["x"] * (r - 12)), cy + int(g["y"] * (r - 12)), 8, BRIGHT)
    for m in shapes.get("marks", []):
        c = BRIGHT if m["held"] else DIM
        cx = OX + m["col"] * cell_w + cell_w // 2
        cy = OY + m["row"] * cell_h + cell_h // 2
        s = 11                                  # half size, in pixels: the letters' cap height
        if m["shape"] == "circle":
            ring(cx, cy, s, c)
        elif m["shape"] == "square":
            line(cx - s, cy - s, cx + s, cy - s, c); line(cx - s, cy + s, cx + s, cy + s, c)
            line(cx - s, cy - s, cx - s, cy + s, c); line(cx + s, cy - s, cx + s, cy + s, c)
        elif m["shape"] == "triangle":
            line(cx, cy - s, cx - s, cy + s, c); line(cx, cy - s, cx + s, cy + s, c)
            line(cx - s, cy + s, cx + s, cy + s, c)
        elif m["shape"] == "cross":
            line(cx - s, cy - s, cx + s, cy + s, c); line(cx + s, cy - s, cx - s, cy + s, c)

def render(lines, path, glyphs, spans=None, shapes=None):
    """spans: ([(row, c0, c1)] bright, [(row, c0, c1)] dim): cells coloured
    by state rather than by their row, for the pad diagram. shapes: drawn
    over the text afterwards, see shapes_draw; a cell a mark sits in is
    left blank of its glyph."""
    px = bytearray(W * H * 3)
    skip = {(m["row"], m["col"]) for m in (shapes or {}).get("marks", [])}
    forced = {}
    if spans:
        for r, c0, c1 in spans[1]:
            for c in range(c0, c1 + 1):
                forced[(r, c)] = DIM
        for r, c0, c1 in spans[0]:
            for c in range(c0, c1 + 1):
                forced[(r, c)] = BRIGHT
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
            if ch == " " or (r, c) in skip:
                continue
            color = MID if rule else BRIGHT if selected else TEXT
            color = forced.get((r, c), color)
            put(c, r, ch, color)
    if shapes:
        shapes_draw(px, shapes)
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
    render(mockup.gamepad, os.path.join(out, "portscope.png"), glyphs,
           mockup.pad_spans(), mockup.pad_shapes())
    render(mockup.gamepad_shapes, os.path.join(out, "portscope-shapes.png"), glyphs,
           mockup.pad_spans(True), mockup.pad_shapes(True))
    render(mockup.diagnostics, os.path.join(out, "settings-diagnostics.png"), glyphs)
