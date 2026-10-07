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

def unifont():
    """The Unifont glyphs the mockups use, from tools/unifont-mockup.hex
    (tools/mkjafont.py): character to (width in pixels, 16 rows)."""
    out = {}
    for line in open(os.path.join(os.path.dirname(__file__), "unifont-mockup.hex")):
        if line.startswith("#"):
            continue
        cp, bits = line.strip().split(":")
        step = len(bits) // 16
        out[chr(int(cp, 16))] = (step * 4, [int(bits[i:i + step], 16) for i in range(0, len(bits), step)])
    return out
UNI = unifont()

def ja26():
    """The 26 x 26, 16-level glyphs the launcher draws kana and kanji with,
    those the mockups use (tools/mkja26.py): character to 26 rows."""
    out = {}
    path = os.path.join(os.path.dirname(__file__), "ja26-mockup.hex")
    for line in open(path):
        if line.startswith("#"):
            continue
        cp, h = line.strip().split(":")
        b = bytes.fromhex(h)
        out[chr(int(cp, 16))] = [[(b[y * 13 + x // 2] >> (0 if x & 1 else 4)) & 15
                                  for x in range(26)] for y in range(26)]
    return out
JA26 = ja26()

# term.c's draw_ja26, in the same integer steps: bilinear from 26 to 48,
# then the cell colour blended over black.
_I0, _W1 = [], []
for _o in range(48):
    _f = (2 * _o + 1) * 26 * 128 // 48 - 128
    _i = _f // 256 if _f >= 0 else -1
    _I0.append(_i)
    _W1.append(_f - _i * 256)

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

# src/pix.c, line for line, so a mockup has the device's pixels.
def _put(px, x, y, c):
    if 0 <= x < W and 0 <= y < H:
        px[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes(c)

def pix_disc(px, cx, cy, r, c):
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy < r * r:
                _put(px, cx + dx, cy + dy, c)

def pix_ring(px, cx, cy, r, t, c):
    ri = max(r - t, 0)
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            d2 = dx * dx + dy * dy
            if ri * ri <= d2 < r * r:
                _put(px, cx + dx, cy + dy, c)

def pix_line(px, x0, y0, x1, y1, t, c):
    half = t // 2
    vx, vy = x1 - x0, y1 - y0
    len2, lim2 = vx * vx + vy * vy, t * t
    for y in range(min(y0, y1) - half - 1, max(y0, y1) + half + 2):
        for x in range(min(x0, x1) - half - 1, max(x0, x1) + half + 2):
            qx, qy = 2 * x + 1 - 2 * x0, 2 * y + 1 - 2 * y0
            tn = qx * vx + qy * vy
            if len2 == 0 or tn <= 0:
                dx, dy = qx, qy
            elif tn >= 2 * len2:
                dx, dy = qx - 2 * vx, qy - 2 * vy
            else:
                cr = qx * vy - qy * vx
                if cr * cr <= lim2 * len2:
                    _put(px, x, y, c)
                continue
            if dx * dx + dy * dy <= lim2:
                _put(px, x, y, c)

# term.c's draw_mark: the four characters that stand for the shape marks,
# drawn as two-pixel outlines on the capitals' centre line.

def mark(px, col, row, shape, c):
    cx = OX + col * 8 * SCALE + 8 * SCALE // 2
    cy = OY + row * 16 * SCALE + 7 * SCALE
    s, t = 11 * SCALE // 3, (2 * SCALE + 2) // 3
    h = s * 4 // 5
    if shape == "circle":
        pix_ring(px, cx, cy, s, t, c)
    elif shape == "square":
        for a, b in (((cx - h, cy - h), (cx + h, cy - h)), ((cx + h, cy - h), (cx + h, cy + h)),
                     ((cx + h, cy + h), (cx - h, cy + h)), ((cx - h, cy + h), (cx - h, cy - h))):
            pix_line(px, *a, *b, t, c)
    elif shape == "triangle":
        a, b, d = (cx, cy - s), (cx - s, cy + s * 3 // 4), (cx + s, cy + s * 3 // 4)
        pix_line(px, *a, *b, t, c); pix_line(px, *b, *d, t, c); pix_line(px, *d, *a, t, c)
    else:
        pix_line(px, cx - h, cy - h, cx + h, cy + h, t, c)
        pix_line(px, cx - h, cy + h, cx + h, cy - h, t, c)

def shapes_draw(px, shapes):
    """The stick rings and their dots, as portscope.c places them."""
    cell_w, cell_h = 8 * SCALE, 16 * SCALE
    for g in shapes.get("rings", []):
        cx = OX + int(g["col"] * cell_w)
        cy = OY + int(g["row"] * cell_h) + cell_h // 2
        r = g["rows"] * cell_h // 2 - 8
        travel = r - 12
        pix_ring(px, cx, cy, r, 2, BRIGHT if g["clicked"] else DIM)
        rnd = lambda v: int(v * travel + (-0.5 if v < 0 else 0.5))
        pix_disc(px, cx + rnd(g["x"]), cy + rnd(g["y"]), 8, BRIGHT)

def render(lines, path, glyphs, spans=None, shapes=None):
    """spans: ([(row, c0, c1)] bright, [(row, c0, c1)] dim): cells coloured
    by state rather than by their row, for the pad diagram. shapes: the
    stick rings, drawn over the text afterwards, see shapes_draw. A shape
    mark character is drawn as its outline, in the colour its cell has."""
    px = bytearray(W * H * 3)
    forced = {}
    if spans:
        for r, c0, c1 in spans[1]:
            for c in range(c0, c1 + 1):
                forced[(r, c)] = DIM
        for r, c0, c1 in spans[0]:
            for c in range(c0, c1 + 1):
                forced[(r, c)] = BRIGHT
    def put_ja26(col, row, ch, color):
        g = JA26[ch]
        L = lambda y, x: g[y][x] if 0 <= y < 26 and 0 <= x < 26 else 0
        x0, y0 = OX + col * 8 * SCALE, OY + row * 16 * SCALE
        for oy in range(48):
            y, wy = _I0[oy], _W1[oy]
            for ox in range(48):
                x, wx = _I0[ox], _W1[ox]
                top = L(y, x) * (256 - wx) + L(y, x + 1) * wx
                bot = L(y + 1, x) * (256 - wx) + L(y + 1, x + 1) * wx
                a = (top * (256 - wy) + bot * wy) // 3855
                if a:
                    base = ((y0 + oy) * W + x0 + ox) * 3
                    px[base:base + 3] = bytes(b + (c - b) * a // 255
                                              for b, c in zip(BG, color))
    def put_unifont(col, row, ch, color):
        wpx, rows = UNI[ch]
        x0, y0 = OX + col * 8 * SCALE, OY + row * 16 * SCALE
        for yy, bits in enumerate(rows):
            for xx in range(wpx):
                if bits & (1 << (wpx - 1 - xx)):
                    for sy in range(SCALE):
                        base = ((y0 + yy * SCALE + sy) * W + x0 + xx * SCALE) * 3
                        for sx in range(SCALE):
                            px[base + sx * 3:base + sx * 3 + 3] = bytes(color)
    def put(col, row, g, color):
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
        c = 0
        for ch in line:
            kind, g = mockup.glyph(ch)
            w = mockup.cw(ch)
            if c + w > COLS:
                break
            if ch != " " and kind != "none":
                color = MID if rule else BRIGHT if selected else TEXT
                color = forced.get((r, c), color)
                if kind == "mark":
                    mark(px, c, r, g, color)
                elif kind == "unifont" and w == 2 and g in JA26:
                    put_ja26(c, r, g, color)
                elif kind == "unifont":
                    put_unifont(c, r, g, color)
                else:
                    put(c, r, g, color)
            c += w
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
    for name, lines in (("ja-systems", mockup.ja_systems), ("ja-games", mockup.ja_games),
                        ("ja-recently-played", mockup.ja_recent),
                        ("ja-settings", mockup.ja_settings),
                        ("ja-language-region", mockup.ja_language),
                        ("language-region", mockup.en_language),
                        ("games-any-script", mockup.games_any_script)):
        render(lines, os.path.join(out, name + ".png"), glyphs)
