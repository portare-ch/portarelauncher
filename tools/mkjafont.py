#!/usr/bin/env python3
"""Cuts the glyphs the mockups use out of the Japanese build of GNU
Unifont into tools/unifont-mockup.hex, so the mockups render kana and
kanji without the whole 4 MB font in the repository.

    python3 tools/mkjafont.py unifont_jp-18.0.01.hex izmg16-plane00.hex

unifont_jp-18.0.01.hex is from
https://unifoundry.com/pub/unifont/unifont-18.0.01/font-builds/, and
izmg16-plane00.hex is font/plane00/ in unifont-18.0.01.tar.gz from the
directory above: the public domain Izumi16 glyphs unifont_jp is built
with. A glyph that matches one of them is public domain; any other is
Unifont's own, used under the GPL 2.0 or later with the GNU font embedding
exception. FONTS.md has the notice.

The set is the one the launcher will carry: a character of JIS X 0213
that the VGA font does not have, with its unifont_jp glyph, 8 or 16
pixels wide. A character in a mockup that neither has is left out, and
draws as its base letter or ?.
"""
import os, sys, unicodedata
sys.path.insert(0, os.path.dirname(__file__))
import mockup

def read_hex(path):
    out = {}
    for line in open(path):
        if ":" in line and not line.startswith("#"):
            cp, bits = line.strip().split(":")
            out[int(cp, 16)] = bits
    return out

def jis_x_0213(ch):
    try:
        return len(ch.encode("euc_jis_2004")) >= 2
    except UnicodeEncodeError:
        return False

unifont, izumi = read_hex(sys.argv[1]), read_hex(sys.argv[2])

wanted = set()
for name in dir(mockup):
    v = getattr(mockup, name)
    for l in v if isinstance(v, list) else []:
        if isinstance(l, str):
            for c in mockup.nfc(l):
                if not unicodedata.combining(c) and c not in mockup.MARKS \
                        and mockup.vga(c) is None:
                    wanted.add(c)

glyphs, own, left_out = {}, [], []
for c in sorted(wanted):
    bits = unifont.get(ord(c))
    if bits and len(bits) in (32, 64) and jis_x_0213(c):
        glyphs[ord(c)] = bits
        if izumi.get(ord(c)) != bits:
            own.append(c)
    else:
        left_out.append(c)

out = os.path.join(os.path.dirname(__file__), "unifont-mockup.hex")
with open(out, "w") as f:
    f.write(f"# {len(glyphs)} glyphs from {os.path.basename(sys.argv[1])} for the mockups, "
            "written by tools/mkjafont.py.\n")
    f.write("# Public domain Izumi16 glyphs, except these, which are Unifont's own and used\n")
    f.write("# under the GPL 2.0 or later with the GNU font embedding exception (FONTS.md):\n")
    f.write("# " + " ".join(f"U+{ord(c):04X}" for c in own) + "\n")
    for cp in sorted(glyphs):
        f.write(f"{cp:04X}:{glyphs[cp]}\n")
print("wrote", os.path.relpath(out), len(glyphs), "glyphs,", len(own), "of them Unifont's own")
if left_out:
    print("left out, drawn as a base letter or ?:", " ".join(f"U+{ord(c):04X}" for c in left_out))
