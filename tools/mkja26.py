#!/usr/bin/env python3
"""Generates ja26.bin: the kana and kanji drawn the way the PS2 drew its
system font (rom0:FONTM, see docs/japanese.md): 26 x 26 pixels, 16 grey
levels, scaled by the launcher with bilinear filtering.

    python3 tools/mkja26.py NotoSansCJK-Medium.ttc out/ja26.bin

Needs Pillow and fontTools. The font is Noto Sans CJK JP Medium, under the
SIL Open Font License 1.1. The set is the two-cell glyphs of src/unifont.c
that the font has; anything else stays Unifont's.

Format, little-endian:
    "PLJA26\\0\\1"    magic and version
    u32 n            glyphs
    u32 size         26
    u32 cp[n]        code points, sorted
    u8  bits[n][338] 26 rows of 13 bytes, two pixels a byte, left pixel in
                     the high nibble, 0 empty to 15 full
"""
import os, re, struct, sys
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTCollection

SIZE, EM = 26, 24
BASELINE = 1 + round(EM * 0.88)      # the ideographic em box, 880 / -120

src = os.path.join(os.path.dirname(__file__), "..", "src", "unifont.c")
keys = re.search(r"uni_key\[UNI_N\] = \{(.*?)\};", open(src).read(), re.S).group(1)
wide = [int(k, 16) & 0x7FFFFFFF for k in re.findall(r"0x([0-9a-f]{8})", keys)
        if int(k, 16) & 0x80000000]

path, out = sys.argv[1], sys.argv[2]
ttc = TTCollection(path)
index = next(i for i, f in enumerate(ttc.fonts)
             if "JP" in f["name"].getDebugName(1))
cmap = ttc.fonts[index].getBestCmap()
font = ImageFont.truetype(path, EM, index=index)

cps, bits = [], []
for cp in wide:
    if cp not in cmap:
        continue
    im = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(im).text((1, BASELINE), chr(cp), font=font, fill=255, anchor="ls")
    row = []
    for y in range(SIZE):
        levels = [round(im.getpixel((x, y)) * 15 / 255) for x in range(SIZE)]
        row += [levels[x] << 4 | levels[x + 1] for x in range(0, SIZE, 2)]
    cps.append(cp)
    bits.append(bytes(row))

with open(out, "wb") as f:
    f.write(b"PLJA26\0\1" + struct.pack("<II", len(cps), SIZE))
    f.write(struct.pack(f"<{len(cps)}I", *cps))
    for b in bits:
        f.write(b)
print(f"wrote {out}: {len(cps)} of {len(wide)} two-cell glyphs, "
      f"{os.path.getsize(out) // 1024} KB, from {font.getname()}")
