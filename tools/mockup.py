#!/usr/bin/env python3
"""Renders the launcher screens at the real grid so layout can be judged
before any of it is written. 53x20 is 8x16 glyphs at 3x on a 1280x960 panel."""

COLS, ROWS = 53, 20

def screen(title, lines):
    out = [f"    {title}", "    " + "." * COLS + "  <- %d columns" % COLS]
    for i, l in enumerate(lines):
        if len(l) > COLS:
            raise SystemExit(f"{title}: row {i} is {len(l)} cols, max {COLS}:\n{l}")
        out.append("    " + l.ljust(COLS) + "|")
    for _ in range(ROWS - len(lines)):
        out.append("    " + " " * COLS + "|")
    out.append("    " + "'" * COLS + "  <- %d rows" % ROWS)
    return "\n".join(out)

def row(left, right):
    pad = COLS - len(left) - len(right)
    return left + " " * max(pad, 1) + right

def item(sel, label, right=""):
    return row(("  \u25b8 " if sel else "    ") + label, (right + "  ") if right else "")

RULE = " " + "\u2550" * (COLS - 2)
THIN = " " + "\u2500" * (COLS - 2)

SYS = [("GameCube", 24), ("Nintendo 64", 9), ("PlayStation", 112),
       ("PlayStation 2", 2), ("PlayStation Portable", 31), ("SNES", 204),
       ("Mega Drive", 88), ("Game Boy Advance", 140), ("Dreamcast", 6),
       ("Arcade", 47)]

systems = [row(" PortareOS", "23:59  BAT 87% "), RULE, "",
           row("  SYSTEMS", "12 found  "), ""]
systems += [item(i == 0, n, str(c)) for i, (n, c) in enumerate(SYS)]
systems += ["", THIN, row(" A SELECT   Y SETTINGS", "\u2191\u2193 MOVE ")]

GAMES = ["Tekken 2", "Tekken 3", "Tomb Raider", "Tony Hawk's Pro Skater 2",
         "Vagrant Story", "Wipeout XL", "Xenogears"]
games = [row(" PortareOS  \u203a  PlayStation", "112 games "), RULE, ""]
games += [item(n == "Tekken 3", n) for n in GAMES]
games += [""] * 4
games += [row("    swanstation", "8 of 112  "), "", THIN,
          row(" A LAUNCH   B BACK", "\u2191\u2193 MOVE ")]

SET = [("Wi-Fi", "Hofmann-5G"), ("Bluetooth", "off"),
       ("Brightness", "70%"), ("USB gadget mode", "off")]
settings = [row(" PortareOS  \u203a  Settings", ""), RULE, ""]
settings += [item(i == 0, n, v) for i, (n, v) in enumerate(SET)]
settings += ["", THIN, "",
             "    Everything here is a stub in the first cut.",
             "    The list renders and moves, nothing applies yet."]
settings += [""] * 5
settings += [THIN, row(" A CHANGE   B BACK", "\u2191\u2193 MOVE ")]

launching = [row(" PortareOS", "23:59  BAT 87% "), RULE] + [""] * 4
launching += ["           Tekken 3 (USA)", "",
              "           swanstation", "",
              "           handing over the display\u2026"]
launching += [""] * 7

if __name__ == "__main__":
    for t, s in (("SYSTEMS", systems), ("GAMES", games),
                 ("SETTINGS (stub)", settings), ("LAUNCHING", launching)):
        print(screen(t, s))
        print()
