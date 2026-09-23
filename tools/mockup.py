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

SET = [("Wi-Fi", "Hofmann-5G"), ("Bluetooth", "WH-1000XM4"),
       ("USB gadget mode", "network"), ("Button style", "Retroid"),
       ("Colour", "grey")]
settings = [row(" Settings", ""), RULE, ""]
settings += [item(i == 1, n, v) for i, (n, v) in enumerate(SET)]
settings += ["", THIN, "",
             "    Headphones, controllers. Open to",
             "    scan, connect, set auto-connect."]
settings += [""] * 5
settings += [THIN, row(" A CHANGE   B BACK", "\u2191\u2193 MOVE ")]

# Two toggles and the devices in one list: switching it on, letting known
# headphones come back, and picking them when they have not.
BT = [("WH-1000XM4", "connected"), ("DualSense Edge", "paired"),
      ("Bose QC35", "paired"), ("JBL Flip 5", "new")]
bluetooth = [row(" Settings  \u203a  Bluetooth", ""), RULE, "",
             item(False, "Bluetooth", "on"),
             item(False, "Auto-connect known devices", "yes"),
             THIN,
             row("  DEVICES", "4 found  ")]
bluetooth += [item(i == 0, n, v) for i, (n, v) in enumerate(BT)]
bluetooth += [""] * 6
bluetooth += [THIN, row(" A DISCONNECT   B BACK   Y SCAN", "")]

# ---- on-screen keyboard -------------------------------------------------
#
# A Wi-Fi password is 8 to 63 printable ASCII characters and anything goes,
# so the keyboard has to reach every one of them from a d-pad. Ten keys a
# row in five-column cells: 50 columns, which is the widest grid that still
# leaves a margin, and a cell 120 px wide on the panel - a target, not a
# character. The selected key is bracketed, the way a DOS form marked focus;
# there is no inverse video on a monochrome ramp and there does not need to
# be.
#
# The bottom row is the same ten cells, spent unevenly: the keys you reach
# for without looking (space, delete, done) are the wide ones.

LOWER = ["1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"]
UPPER = ["1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.@"]
# The digits stay on top in every layer, so they are always one press up
# from wherever you are. That leaves 30 cells for the 28 printable symbols
# that are not already on the letter layer (- _ . @ are, because they turn
# up in passwords more than the rest). Printable ASCII, every one of it.
SYMS  = ["1234567890", "!\"#$%&'()*", "+,/:;<=>?[", "\\]^`{|}~\u00a0\u00a0"]
BOTTOM = [("SHIFT", 2), ("#+=", 2), ("SPACE", 3), ("DEL", 1), ("DONE", 2)]
CELL = 5
KB_LEFT = " "

def cell(label, width, sel):
    w = CELL * width
    if sel:
        return "[" + label.center(w - 2) + "]"
    return label.center(w)

def keyrow(chars, sel_col=None):
    return KB_LEFT + "".join(cell(c.strip(" ") or " ", 1, i == sel_col)
                             for i, c in enumerate(chars))

def bottomrow(sel_label=None, labels=None):
    labels = labels or {}
    return KB_LEFT + "".join(cell(labels.get(k, k), w, k == sel_label)
                             for k, w in BOTTOM)

def keyboard(layer, sel=None, bottom_sel=None, labels=None):
    rows = [keyrow(r, sel[1] if sel and sel[0] == i else None)
            for i, r in enumerate(layer)]
    return rows + [bottomrow(bottom_sel, labels)]

FIELD_W = COLS - 4          # inside the box: margin, border, border, margin

def field(text, hidden=False, cursor=True):
    shown = "•" * len(text) if hidden else text
    shown += "_" if cursor else ""
    inner = FIELD_W - 4
    if len(shown) > inner:                 # 63 characters do not fit: show
        shown = "‹" + shown[-(inner - 1):]   # the end being typed
    return ["  ┌" + "─" * (FIELD_W - 2) + "┐",
            "  │ " + shown.ljust(FIELD_W - 4) + " │",
            "  └" + "─" * (FIELD_W - 2) + "┘"]

RETROID_HINTS = " B TYPE  A DELETE  Y SPACE  X SHIFT  START JOIN"
# The PS printing, approximated out of CP437 as the launcher draws it.
PS_HINTS      = " X TYPE  ○ DELETE  ■ SPACE  ▲ SHIFT  START JOIN"

def kb_screen(ssid, text, layer, sel=None, bottom_sel=None, hidden=False,
              note="SELECT shows or hides the password", labels=None,
              hints=RETROID_HINTS):
    n = len(text)
    count = f"{n} / 63" if n >= 8 else f"{n} / 63  at least 8"
    # The network gets its own line: the real header carries VOL, BRI and
    # BAT beside the clock, and has no room left for a 32-byte SSID.
    out = [row(" Settings  ›  Wi-Fi", "VOL 50%  BRI 70%  BAT 87%  23:59 "),
           RULE, "  NETWORK  " + ssid, row("  PASSWORD", count + "  ")]
    out += field(text, hidden)
    out += [""]
    out += keyboard(layer, sel, bottom_sel, labels)
    out += ["", "    " + note if note else ""]
    out += [""] * (ROWS - 2 - len(out))
    out += [THIN, hints]
    return out

kb_lower = kb_screen("FRITZ!Box 7520 JI", "hunt", LOWER, sel=(1, 2))
kb_upper = kb_screen("FRITZ!Box 7520 JI", "hunter2Hunt", UPPER, sel=(2, 6),
                     hints=PS_HINTS)
kb_syms  = kb_screen("FRITZ!Box 7520 JI", "hunter2Hunter!", SYMS, bottom_sel="DONE",
                     hidden=True, labels={"#+=": "abc"})

# A failed join keeps the keyboard up with the text intact - the likeliest
# fix is one wrong character - and says why on the line under the keys.
joining = kb_screen("FRITZ!Box 7520 JI", "hunter2Hunter!", LOWER, sel=(1, 2),
                    note="Wrong password, or the network refused it.")

launching = [row(" PortareOS", "23:59  BAT 87% "), RULE] + [""] * 4
launching += ["           Tekken 3 (USA)", "",
              "           swanstation", "",
              "           handing over the display\u2026"]
launching += [""] * 7

if __name__ == "__main__":
    for t, s in (("SYSTEMS", systems), ("GAMES", games),
                 ("SETTINGS", settings), ("BLUETOOTH", bluetooth),
                 ("KEYBOARD - letters", kb_lower),
                 ("KEYBOARD - shift, PS button style", kb_upper),
                 ("KEYBOARD - symbols, password hidden, DONE selected", kb_syms),
                 ("KEYBOARD - join failed", joining),
                 ("LAUNCHING", launching)):
        print(screen(t, s))
        print()
