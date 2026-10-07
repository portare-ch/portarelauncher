#!/usr/bin/env python3
"""Extracts the full-width glyphs the mockups use from the Japanese build
of GNU Unifont into tools/ja16-mockup.hex, so the mockups render kana and
kanji without the whole 4 MB font in the repository.

    python3 tools/mkjafont.py unifont_jp-18.0.01.hex

From https://unifoundry.com/pub/unifont/unifont-18.0.01/font-builds/.
Most kana and kanji of unifont_jp come from the public domain Izumi16
font (font/plane00/izmg16-plane00.hex in Unifont's source); the rest,
the ideographic full stop and the repetition mark among them, are
Unifont's own, under the SIL OFL 1.1 or the GPL 2.0 or later with the
font embedding exception.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import mockup

src = sys.argv[1]
wide = set()
for name in dir(mockup):
    v = getattr(mockup, name)
    lines = v if isinstance(v, list) else []
    for l in lines:
        if isinstance(l, str):
            wide |= {c for c in l if mockup.cw(c) == 2}

glyphs = {}
for line in open(src):
    cp, bits = line.strip().split(":")
    if chr(int(cp, 16)) in wide:
        if len(bits) != 64:
            sys.exit(f"U+{cp} is not 16x16 in {src}")
        glyphs[int(cp, 16)] = bits

missing = sorted(wide - {chr(c) for c in glyphs})
if missing:
    sys.exit("not in the font: " + " ".join(f"U+{ord(c):04X}" for c in missing))

out = os.path.join(os.path.dirname(__file__), "ja16-mockup.hex")
with open(out, "w") as f:
    f.write(f"# {len(glyphs)} glyphs from {os.path.basename(src)}, written by tools/mkjafont.py.\n")
    f.write("# Public domain Izumi16 glyphs, and Unifont's own under the SIL OFL 1.1\n")
    f.write("# or GPL 2.0+ with the font embedding exception; see tools/mkjafont.py.\n")
    for cp in sorted(glyphs):
        f.write(f"{cp:04X}:{glyphs[cp]}\n")
print("wrote", os.path.relpath(out), len(glyphs), "glyphs")
