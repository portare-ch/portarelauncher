#!/usr/bin/env python3
"""The gamepad tester, as a picture: a Linamp face around a black LCD that
shows the pad, drawn at the panel's 1280x960 with the launcher's 8x16 font.
Same idea as tools/mockup_png.py - a program rather than a picture, so the
layout cannot drift from the dimensions it claims - and the same pure
Python, no PIL, no ImageMagick.

    python3 tools/mockup_gamepad.py      -> docs/images/gamepad-tester.png

The palette is PORTAMP's (portamp.lua in portareos): the body blue with a
light and a dark bevel, the LCD black with a pale rim, green for what is
lit and a dark green for what is not. The sample state is one hand on the
pad: A down, D-pad right, R1 held, the right stick pushed up and left, L2
two thirds in.
"""
import os, re, struct, zlib

W, H = 1280, 960
BODY, BODY_HI, BODY_LO = (0x46, 0x51, 0x9A), (0x7B, 0x86, 0xC9), (0x26, 0x2D, 0x5E)
LCD, LCD_RIM = (0x0B, 0x0F, 0x0B), (0x9A, 0xA3, 0xD8)
GREEN, UNLIT, GLOW = (0x3C, 0xF0, 0x3C), (0x0E, 0x26, 0x0E), (0x1C, 0x6A, 0x1C)
WHITE, DIMTXT = (0xE8, 0xEA, 0xF6), (0x73, 0x7C, 0xB8)

px = bytearray(W * H * 3)

def put(x, y, c):
    if 0 <= x < W and 0 <= y < H:
        i = (y * W + x) * 3
        px[i:i + 3] = bytes(c)

def rect(x, y, w, h, c):
    row = bytes(c) * w
    for yy in range(y, y + h):
        if 0 <= yy < H:
            i = (yy * W + x) * 3
            px[i:i + w * 3] = row

def bevel(x, y, w, h, face, hi, lo, t=3):
    rect(x, y, w, h, face)
    rect(x, y, w, t, hi); rect(x, y, t, h, hi)
    rect(x, y + h - t, w, t, lo); rect(x + w - t, y, t, h, lo)

def panel(x, y, w, h):
    """An LCD: pale rim, dark inset, black glass."""
    rect(x - 4, y - 4, w + 8, h + 8, LCD_RIM)
    rect(x - 2, y - 2, w + 4, h + 4, BODY_LO)
    rect(x, y, w, h, LCD)

def disc(cx, cy, r, c):
    for yy in range(-r, r + 1):
        half = int((r * r - yy * yy) ** 0.5)
        rect(cx - half, cy + yy, 2 * half + 1, 1, c)

def ring(cx, cy, r, t, c):
    for yy in range(-r, r + 1):
        outer = int((r * r - yy * yy) ** 0.5)
        inner2 = (r - t) * (r - t) - yy * yy
        inner = int(inner2 ** 0.5) if inner2 > 0 else -1
        if inner < 0:
            rect(cx - outer, cy + yy, 2 * outer + 1, 1, c)
        else:
            rect(cx - outer, cy + yy, outer - inner, 1, c)
            rect(cx + inner + 1, cy + yy, outer - inner, 1, c)

def rrect(x, y, w, h, r, c):
    rect(x + r, y, w - 2 * r, h, c)
    rect(x, y + r, w, h - 2 * r, c)
    for cx, cy in ((x + r, y + r), (x + w - r - 1, y + r), (x + r, y + h - r - 1), (x + w - r - 1, y + h - r - 1)):
        disc(cx, cy, r, c)

def load_font():
    src = open(os.path.join(os.path.dirname(__file__), "..", "src", "font8x16.h")).read()
    body = re.sub(r"/\*.*?\*/", "", src.split("font8x16[4096] = {")[1], flags=re.S)
    data = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", body))
    assert len(data) == 4096
    return data
FONT = load_font()
CP437 = {"·": 0xFA, "▸": 0x10, "↑": 0x18, "↓": 0x19, "←": 0x1B, "→": 0x1A}

def text(x, y, s, c, scale=2, align="left"):
    w = len(s) * 8 * scale
    if align == "center":
        x -= w // 2
    elif align == "right":
        x -= w
    for n, ch in enumerate(s):
        g = CP437.get(ch, ord(ch) if ord(ch) < 128 else 0x3F)
        for yy in range(16):
            bits = FONT[g * 16 + yy]
            for xx in range(8):
                if bits & (0x80 >> xx):
                    rect(x + (n * 8 + xx) * scale, y + yy * scale, scale, scale, c)

def lit(on):
    return GREEN if on else UNLIT

def label(x, y, s, on, scale=2, align="center"):
    text(x, y, s, GREEN if on else DIMTXT, scale, align)

def button(cx, cy, r, name, on):
    """A round face button: a glow when lit, the dark green when not."""
    if on:
        disc(cx, cy, r + 8, GLOW)
    disc(cx, cy, r, lit(on))
    text(cx, cy - 16, name, LCD if on else DIMTXT, 2, "center")

def pill(x, y, w, h, name, on, scale=2):
    if on:
        rrect(x - 6, y - 6, w + 12, h + 12, 12, GLOW)
    rrect(x, y, w, h, h // 2, lit(on))
    text(x + w // 2, y + h // 2 - 8 * scale, name, LCD if on else DIMTXT, scale, "center")

def dpad(cx, cy, pressed):
    arm, thick = 70, 44
    for d, (dx, dy) in {"up": (0, -1), "down": (0, 1), "left": (-1, 0), "right": (1, 0)}.items():
        on = d == pressed
        if dx:
            x = cx + (dx * (arm + thick // 2)) - arm // 2 - (thick // 2 if dx < 0 else -thick // 2)
            x = cx + thick // 2 if dx > 0 else cx - thick // 2 - arm
            if on:
                rect(x - 6, cy - thick // 2 - 6, arm + 12, thick + 12, GLOW)
            rect(x, cy - thick // 2, arm, thick, lit(on))
        else:
            y = cy + thick // 2 if dy > 0 else cy - thick // 2 - arm
            if on:
                rect(cx - thick // 2 - 6, y - 6, thick + 12, arm + 12, GLOW)
            rect(cx - thick // 2, y, thick, arm, lit(on))
    rect(cx - thick // 2, cy - thick // 2, thick, thick, UNLIT if pressed is None else lit(True) if False else UNLIT)
    disc(cx, cy, 8, LCD)

def stick(cx, cy, r, name, x, y, clicked):
    """The travel as a ring, the position as a dot, the values beneath."""
    ring(cx, cy, r, 3, GREEN if clicked else DIMTXT)
    ring(cx, cy, r // 2, 1, UNLIT)
    rect(cx - r, cy, 2 * r + 1, 1, UNLIT); rect(cx, cy - r, 1, 2 * r + 1, UNLIT)
    dx, dy = int(x * (r - 10)), int(y * (r - 10))
    if abs(x) > 0.02 or abs(y) > 0.02:
        disc(cx + dx, cy + dy, 16, GLOW)
    disc(cx + dx, cy + dy, 10, GREEN)
    text(cx, cy + r + 10, name, GREEN if clicked else DIMTXT, 2, "center")
    text(cx, cy + r + 42, "X %+.2f  Y %+.2f" % (x, y), GREEN, 2, "center")

def trigger(x, y, w, h, name, value, digital_on):
    """L2/R2: a bar that fills with the pull, the value beside it."""
    rrect(x, y, w, h, 8, UNLIT)
    fill = int((w - 8) * value)
    if fill > 0:
        rrect(x + 4, y + 4, max(fill, 16), h - 8, 6, GREEN)
    text(x + w // 2, y + h // 2 - 16, name, LCD if value > 0.5 else DIMTXT, 2, "center")
    text(x + w // 2, y + h + 10, "%.2f" % value, GREEN if value > 0 else DIMTXT, 2, "center")

# ---- the face --------------------------------------------------------------
bevel(0, 0, W, H, BODY, BODY_HI, BODY_LO, 6)

# name plate with rails, the Linamp way
rect(40, 44, W - 80, 4, BODY_LO); rect(40, 48, W - 80, 2, BODY_HI)
bevel(W // 2 - 260, 20, 520, 52, BODY_LO, BODY_LO, BODY_HI, 2)
text(W // 2, 30, "GAMEPAD TESTER", WHITE, 3, "center")
text(W - 60, 64, "AYN Odin2 Gamepad", DIMTXT, 2, "right")
text(60, 64, "PortareOS", DIMTXT, 2, "left")

# the LCD with the pad on it
PX, PY, PW, PH = 48, 110, W - 96, 740
panel(PX, PY, PW, PH)

# shoulders along the top of the glass
pill(PX + 60, PY + 36, 190, 56, "L1", False)
pill(PX + PW - 250, PY + 36, 190, 56, "R1", True)
trigger(PX + 60, PY + 118, 190, 40, "L2", 0.66, False)
trigger(PX + PW - 250, PY + 118, 190, 40, "R2", 0.00, False)

# left: d-pad above the left stick; right: face buttons above the right stick
dpad(PX + 300, PY + 280, "right")
stick(PX + 300, PY + 510, 86, "L3", 0.03, -0.02, False)

fx, fy, fr, gap = PX + PW - 300, PY + 280, 38, 96
button(fx, fy - gap, fr, "X", False)
button(fx - gap, fy, fr, "Y", False)
button(fx + gap, fy, fr, "A", True)
button(fx, fy + gap, fr, "B", False)
stick(fx, PY + 510, 86, "R3", -0.41, -0.83, False)

# the middle: select, home, start; the paddles as two bars at the bottom edge
mx = PX + PW // 2
pill(mx - 180, PY + 470, 100, 44, "SELECT", False)
pill(mx - 50, PY + 470, 100, 44, "HOME", False)
pill(mx + 80, PY + 470, 100, 44, "START", False)
pill(mx - 90, PY + 550, 80, 36, "M1", False)
pill(mx + 10, PY + 550, 80, 36, "M2", False)

# the last event, in the LCD's bottom line
rect(PX, PY + PH - 60, PW, 1, UNLIT)
text(PX + 24, PY + PH - 44, "BTN_SOUTH 1   ABS_RY -27210   ABS_Z 21580", GREEN, 2, "left")
text(PX + PW - 24, PY + PH - 44, "4 devices  200 Hz", DIMTXT, 2, "right")

# the body's own line below the glass
text(W // 2, PY + PH + 40, "every button and stick as the system sees it  ·  HOME + START to leave", DIMTXT, 2, "center")

def png(path):
    raw = b"".join(b"\x00" + bytes(px[y * W * 3:(y + 1) * W * 3]) for y in range(H))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__), "..", "docs", "images")
    os.makedirs(out, exist_ok=True)
    png(os.path.join(out, "gamepad-tester.png"))
    print("wrote docs/images/gamepad-tester.png")
