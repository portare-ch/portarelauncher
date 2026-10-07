#!/usr/bin/env python3
"""Generates data/ja26.bin: the kana and kanji drawn the way the PS2 drew
its system font, rom0:FONTM (docs/japanese.md): 26 x 26 pixels with 16
grey levels, which the launcher scales to two cells with bilinear
filtering. Also data/ja26.NOTICE, the font's copyright and licence, which
go wherever the glyphs go, and tools/ja26-mockup.hex, the glyphs the
mockups use.

    python3 tools/mkja26.py NotoSansCJK-Medium.ttc

Needs Pillow and fontTools. The font is Noto Sans CJK JP Medium, under the
SIL Open Font License 1.1 (data/OFL-1.1.txt); Debian has it in
fonts-noto-cjk-extra. The set is the two-cell glyphs of src/unifont.c the
font has; anything else stays Unifont's.

ja26.bin, little-endian:
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

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
keys = re.search(r"uni_key\[UNI_N\] = \{(.*?)\};",
                 open(os.path.join(root, "src", "unifont.c")).read(), re.S).group(1)
wide = [int(k, 16) & 0x7FFFFFFF for k in re.findall(r"0x([0-9a-f]{8})", keys)
        if int(k, 16) & 0x80000000]

path = sys.argv[1]
ttc = TTCollection(path)
index = next(i for i, f in enumerate(ttc.fonts) if "JP" in f["name"].getDebugName(1))
names = ttc.fonts[index]["name"]
cmap = ttc.fonts[index].getBestCmap()
font = ImageFont.truetype(path, EM, index=index)

def glyph(cp):
    im = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(im).text((1, BASELINE), chr(cp), font=font, fill=255, anchor="ls")
    out = []
    for y in range(SIZE):
        levels = [round(im.getpixel((x, y)) * 15 / 255) for x in range(SIZE)]
        out += [levels[x] << 4 | levels[x + 1] for x in range(0, SIZE, 2)]
    return bytes(out)

cps = [cp for cp in wide if cp in cmap]
with open(os.path.join(root, "data", "ja26.bin"), "wb") as f:
    f.write(b"PLJA26\0\1" + struct.pack("<II", len(cps), SIZE))
    f.write(struct.pack(f"<{len(cps)}I", *cps))
    for cp in cps:
        f.write(glyph(cp))

with open(os.path.join(root, "data", "ja26.NOTICE"), "w") as f:
    f.write(f"""ja26.bin: glyphs of {names.getDebugName(4)} {names.getDebugName(5).split(';')[0]},
rasterised at 26 x 26 pixels with 16 grey levels by tools/mkja26.py in
portarelauncher. A modified version of the font, under its licence:

{names.getDebugName(0)}

{names.getDebugName(13)}
{names.getDebugName(14)}

The licence's text is OFL-1.1.txt beside this file.
""")

# The mockups' glyphs, as hex, for tools/mockup_png.py.
sys.path.insert(0, here)
import mockup
used = set()
for name in dir(mockup):
    v = getattr(mockup, name)
    for l in v if isinstance(v, list) else []:
        if isinstance(l, str):
            used |= {ord(c) for c in mockup.nfc(l)}
with open(os.path.join(here, "ja26-mockup.hex"), "w") as f:
    f.write("# The 26 x 26, 16-level glyphs the mockups use, from tools/mkja26.py;\n")
    f.write("# see data/ja26.NOTICE.\n")
    for cp in sorted(used & set(cps)):
        f.write(f"{cp:04X}:{glyph(cp).hex().upper()}\n")

print(f"wrote data/ja26.bin: {len(cps)} of {len(wide)} two-cell glyphs, "
      f"{os.path.getsize(os.path.join(root, 'data', 'ja26.bin')) // 1024} KB, from "
      f"{names.getDebugName(4)}; tools/ja26-mockup.hex: {len(used & set(cps))} glyphs")
