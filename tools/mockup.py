#!/usr/bin/env python3
"""Render layouts on the launcher's 53x20 grid: 8x16 glyphs at scale 3."""

import os, unicodedata

COLS, ROWS = 53, 20

# Glyph lookup and widths follow glyph.h; leftover combining marks take no cells.
MARKS = {"\u25b2": "triangle", "\u25a0": "square", "\u25cb": "circle", "\u00d7": "cross"}
# Low CP437 symbols plus > for breadcrumbs and . for ellipses.
VGA_EXTRA = {"\u25b8": 0x10, "\u2191": 0x18, "\u2193": 0x19, "\u2190": 0x1B, "\u2192": 0x1A,
             "\u2665": 0x03, "\u2022": 0x07, "\u203a": 0x3E, "\u2026": 0x2E}
def vga(ch):
    """The VGA font's glyph for ch, or None."""
    if ch in VGA_EXTRA:
        return VGA_EXTRA[ch]
    if " " <= ch <= "~":
        return ord(ch)
    if ord(ch) < 0x80:
        return None
    try:
        return ch.encode("cp437")[0]
    except UnicodeEncodeError:
        return None

def _unifont():
    """Character to width in cells, from tools/unifont-mockup.hex."""
    path = os.path.join(os.path.dirname(__file__), "unifont-mockup.hex")
    if not os.path.exists(path):
        return {}
    return {chr(int(cp, 16)): len(bits) // 32
            for cp, bits in (l.strip().split(":") for l in open(path) if not l.startswith("#"))}
UNIFONT = _unifont()

def glyph(ch):
    """("none", None), ("mark", shape), ("vga", code) or ("unifont", ch)."""
    if unicodedata.combining(ch):
        return ("none", None)
    if ch in MARKS:
        return ("mark", MARKS[ch])
    v = vga(ch)
    if v is not None:
        return ("vga", v)
    if ch in UNIFONT:
        return ("unifont", ch)
    base = unicodedata.normalize("NFD", ch)[0]
    if base != ch and glyph(base)[0] in ("vga", "unifont"):
        return glyph(base)
    return ("vga", ord("?"))

def cw(ch):
    kind, g = glyph(ch)
    return 0 if kind == "none" else UNIFONT[g] if kind == "unifont" else 1
def width(s):
    return sum(cw(c) for c in s)

# Match catalog composition of decomposed filenames.
def nfc(s):
    return unicodedata.normalize("NFC", s)
def fit(s, w):
    """The longest start of s that fits in w columns, never half a glyph."""
    out, n = "", 0
    for c in s:
        if n + cw(c) > w:
            break
        out, n = out + c, n + cw(c)
    return out

def screen(title, lines):
    out = [f"    {title}", "    " + "." * COLS + "  <- %d columns" % COLS]
    for i, l in enumerate(lines):
        if width(l) > COLS:
            raise SystemExit(f"{title}: row {i} is {width(l)} cols, max {COLS}:\n{l}")
        out.append("    " + l + " " * (COLS - width(l)) + "|")
    for _ in range(ROWS - len(lines)):
        out.append("    " + " " * COLS + "|")
    out.append("    " + "'" * COLS + "  <- %d rows" % ROWS)
    return "\n".join(out)

def row(left, right):
    pad = COLS - width(left) - width(right)
    return left + " " * max(pad, 1) + right

def item(sel, label, right=""):
    return row(("  \u25b8 " if sel else "    ") + label, (right + "  ") if right else "")

RULE = " " + "\u2550" * (COLS - 2)

# Match draw_frame: status yields to the breadcrumb, keeping the clock.
STATUS = "VOL 50%  BRI 70%  BAT 87%  23:59 "
CLOCK = "23:59 "
def header(crumb, status=STATUS):
    room = COLS - width(status) - 3  # status includes the right margin
    if width(crumb) > room:
        status = CLOCK
        room = COLS - width(status) - 3
    return row(" " + fit(crumb, room), status)

# Game-list headers show only the clock beside the breadcrumb.
def crumb(name):
    return header("PortareOS  \u203a  " + name, CLOCK)
THIN = " " + "\u2500" * (COLS - 2)

SYS = ["GameCube", "Nintendo 64", "PlayStation",
       "PlayStation 2", "PlayStation Portable", "SNES",
       "Mega Drive", "Game Boy Advance", "Dreamcast", "Arcade"]

systems = [header("PortareOS"), RULE, ""]
systems += [item(i == 0, n) for i, n in enumerate(SYS)]
systems += [""] * (ROWS - 2 - len(systems))
systems += [THIN, row(" A SELECT   X SETTINGS", "")]

GAMES = ["Tekken 2", "Tekken 3", "Tomb Raider", "Tony Hawk's Pro Skater 2",
         "Vagrant Story", "Wipeout XL", "Xenogears"]
games = [crumb("PlayStation"), RULE, ""]
games += [item(n == "Tekken 3", n) for n in GAMES]
games += [""] * (ROWS - 2 - len(games))
games += [THIN, row(" A LAUNCH   B BACK", "8 / 112 ")]

SET = [("WiFi", "Hofmann-5G"), ("SSH", "on"), ("Bluetooth", "WH-1000XM4"),
       ("USB mode", "network"), ("Button style", "Retroid"),
       ("Consoles", "2 changed"),
       ("Color", "grey"), ("Color profile", "stock"), ("Charging LED", "on"),
       ("Timezone", "Europe/Zurich"), ("Diagnostics", ""),
       ("About", "v0.2.6 2026-09-26"), ("Power", "")]
# Settings fill rows 2..14; the three-row button diagram also uses the upper rule.
settings = [header("Settings"), RULE]
settings += [item(i == 4, n, v) for i, (n, v) in enumerate(SET)]
settings += ["         X          X confirm",
             "      Y     A       A back",
             "         B          X settings"]
settings += [THIN, row(" A CHANGE   B BACK", "\u2191\u2193 MOVE ")]

# Wrap console descriptions at 45 columns, matching wrap_puts.
CONSOLES = [("SNES", "PRMPT on"), ("NES", "PRMPT off"),
            ("PlayStation", "PRMPT off"),
            ("Game Boy", "PRMPT off"),
            ("Game Boy Color", "PRMPT off"), ("Game Boy Advance", "PRMPT off"),
            ("Genesis", "PRMPT off")]
def consoles_screen(sel):
    lines = [header("Settings  \u203a  Consoles"), RULE, ""]
    lines += [item(i == sel, n, v) for i, (n, v) in enumerate(CONSOLES)]
    lines += [THIN, "    Experimental."]
    lines += ["    PRMPT adds a pre-emptive frame to reduce",
              "    input lag."]
    lines += [""] * (ROWS - 2 - len(lines))
    lines += [THIN, row(" A CHANGE   B BACK", "\u2191\u2193 MOVE ")]
    return lines

consoles = consoles_screen(0)

DIAG = [("PortScope", "")]
diagnostics = [header("Settings  \u203a  Diagnostics"), RULE, ""]
diagnostics += [item(i == 0, n, v) for i, (n, v) in enumerate(DIAG)]
diagnostics += [""] * (ROWS - 2 - len(diagnostics))
diagnostics += [THIN, row(" A OPEN   B BACK", "\u2191\u2193 MOVE ")]

BT = [("WH-1000XM4", "connected"), ("DualSense Edge", "paired"),
      ("Bose QC35", "paired"), ("JBL Flip 5", "new")]
bluetooth = [header("Settings  \u203a  Bluetooth"), RULE, "",
             item(False, "Bluetooth", "on"),
             item(False, "Auto-connect paired devices", "yes"),
             THIN,
             row("  DEVICES", "4 found  ")]
bluetooth += [item(i == 0, n, v) for i, (n, v) in enumerate(BT)]
bluetooth += [""] * 6
bluetooth += [THIN, row(" A DISCONNECT   B BACK   Y SCAN", "")]

# ---- on-screen keyboard -------------------------------------------------
#
# Ten five-column keys fill 50 columns. Action keys share that width;
# brackets mark the selection.

LOWER = ["1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"]
UPPER = ["1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.@"]
# Both layers share digits; the symbol layer leaves two empty cells.
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
    if len(shown) > inner:
        shown = "‹" + shown[-(inner - 1):]   # show the text tail
    return ["  ┌" + "─" * (FIELD_W - 2) + "┐",
            "  │ " + shown.ljust(FIELD_W - 4) + " │",
            "  └" + "─" * (FIELD_W - 2) + "┘"]

RETROID_HINTS = " A TYPE  B DELETE  X SPACE  L1 SHIFT  START JOIN"
# mockup_png.py draws these characters as outlines, matching term.h.
SHAPE_HINTS   = " × TYPE  ○ DELETE  ▲ SPACE  L1 SHIFT  START JOIN"

def kb_screen(ssid, text, layer, sel=None, bottom_sel=None, hidden=False,
              note="SELECT: show or hide password", labels=None,
              hints=RETROID_HINTS):
    n = len(text)
    count = f"{n} / 63" if n >= 8 else f"{n} / 63  at least 8"
    # A 32-byte SSID needs its own line below the status header.
    out = [header("Settings  ›  WiFi"),
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
                     hints=SHAPE_HINTS)
kb_syms  = kb_screen("FRITZ!Box 7520 JI", "hunter2Hunter!", SYMS, bottom_sel="DONE",
                     hidden=True, labels={"#+=": "abc"})

# Failed joins preserve input and show the error below the keys.
joining = kb_screen("FRITZ!Box 7520 JI", "hunter2Hunter!", LOWER, sel=(1, 2),
                    note="Password rejected. Check and retry.")

# Reserve space below tools for gamelist.xml descriptions and exit instructions.
TOOLS = ["Remove ._ Files", "Start RetroArch (64-bit)"]
tools = [header("Tools"), RULE, "",
         row("  TOOLS", "2 found  "), ""]
tools += [item(n == "Remove ._ Files", n) for n in TOOLS]
tools += [""] * (ROWS - 2 - 4 - 1 - len(tools))
tools += [THIN,
          "    Deletes the ._ files macOS leaves beside",
          "    every ROM on an SD card. Returns on its own.", "", ""]
tools += [THIN, " B RUN   A BACK"]

launching = [header("PortareOS"), RULE] + [""] * 4
launching += ["           Tekken 3 (USA)", "",
              "           Starting..."]
launching += [""] * 9

# ---- quick access, recent, favourites, long titles ----------------------
QUICK = ["Recently played", "Favourites"]
systems_quick = [header("PortareOS"), RULE, "",
                 row("  QUICK ACCESS", "")]
systems_quick += [item(i == 0, n) for i, n in enumerate(QUICK)]
systems_quick += [THIN, row("  SYSTEMS", "")]
systems_quick += [item(False, n) for n in SYS]
systems_quick += [THIN, row(" A SELECT   X SETTINGS", "")]

# Unselected titles use ellipses. Selected titles pause 1 s, scroll a column
# every 150 ms, pause 1.5 s at the end, then reset.
TITLE_W = COLS - 4 - 2    # marker and indent, then the right margin
def cut(t):
    return t if width(t) <= TITLE_W else fit(t, TITLE_W - 3) + "..."
def scrolled(t, shift):
    """Start at the character containing column shift, keeping wide glyphs whole."""
    col = 0
    for i, c in enumerate(t):
        if col + cw(c) > shift:
            return fit(t[i:], TITLE_W)
        col += cw(c)
    return ""
def marquee_end(t):
    """First character boundary at or past overflow, leaving the title end visible."""
    over, col = width(t) - TITLE_W, 0
    for c in t:
        if col >= over:
            break
        col += cw(c)
    return max(col, 0)
def game(sel, title, shift=0):
    title = nfc(title)
    return item(sel, scrolled(title, shift) if sel else cut(title))

# The footer shares the favourite action and position count, leaving 14 list rows.
def games_footer(fav, pos, total):
    return row(" A LAUNCH   B BACK   Y " + ("REMOVE" if fav else "FAVOURITE"),
               "%d / %d " % (pos, total))
SNES = [("Chrono Trigger (USA)", False),
        ("Donkey Kong Country 2 - Diddy's Kong Quest (USA) (En,Fr)", False),
        ("EarthBound (USA)", False),
        ("Final Fantasy III (USA) (Rev 1)", True),
        ("Kirby Super Star (USA)", False),
        ("Legend of Zelda, The - A Link to the Past (USA)", True),
        ("Mega Man X (USA) (Rev 1)", False),
        ("Secret of Mana (USA)", False),
        ("Star Fox (USA)", False),
        ("Super Castlevania IV (USA)", False),
        ("Super Mario World (USA)", True),
        ("Super Metroid (Japan, USA) (En,Ja)", False),
        ("Teenage Mutant Ninja Turtles IV - Turtles in Time (USA)", False),
        ("Yoshi's Island - Super Mario World 2 (USA) (Rev 1)", False)]
def games_screen(selected, shift=0):
    lines = [crumb("Super Nintendo"), RULE, ""]
    lines += [game(t == selected, t, shift=shift) for t, _ in SNES]
    n = 1 + [t for t, _ in SNES].index(selected)
    lines += [""] * (ROWS - 2 - len(lines))
    return lines + [THIN, games_footer(dict(SNES)[selected], n, 204)]

games_long = games_screen("Teenage Mutant Ninja Turtles IV - Turtles in Time (USA)", shift=9)
games_fav = games_screen("Super Mario World (USA)")

# Ten successful launches, newest first, grouped by local day with short system names.
RECENT = [("TODAY", [("Super Mario World", "SNES"), ("Tekken 3", "PS1")]),
          ("YESTERDAY", [("Yoshi's Island - Super Mario World 2", "SNES"),
                         ("Metroid (USA)", "NES")]),
          ("MONDAY", [("Soulcalibur II", "GC"), ("Pop'n Music Portable", "PSP")]),
          ("SUNDAY", [("Super Metroid", "SNES"), ("Wipeout XL", "PS1")]),
          ("SATURDAY", [("Mega Man X", "SNES"), ("Xenogears", "PS1")])]
def across(sel, title, right):
    title = nfc(title)
    w = COLS - 4 - width(right) - 3
    return item(sel, title if width(title) <= w else fit(title, w - 3) + "...", right)
recent = [crumb("Recently played"), RULE, ""]
for day, games_of_day in RECENT:
    recent.append(" " + day)
    recent += [across(t == "Super Mario World", t, sy) for t, sy in games_of_day]
recent += [""] * (ROWS - 2 - len(recent))
recent += [THIN, row(" A LAUNCH   B BACK   Y REMOVE", "")]

# Y removes the selected favourite; missing paths are pruned when the list opens.
FAVS = [("Final Fantasy III (USA) (Rev 1)", "SNES"),
        ("Legend of Zelda, The - A Link to the Past (USA)", "SNES"),
        ("Super Mario World (USA)", "SNES"), ("Tekken 3", "PS1"),
        ("Castlevania - Symphony of the Night", "PS1"),
        ("Soulcalibur II", "GC"), ("Mario Kart 64", "N64")]
favourites = [crumb("Favourites"), RULE, ""]
favourites += [across(i == 2, t, sy) for i, (t, sy) in enumerate(FAVS)]
favourites += [""] * (ROWS - 2 - len(favourites))
favourites += [THIN, games_footer(True, 3, 7)]

# ---- PortScope input diagnostics ---------------------------------------
#
# Held controls are bright; button labels follow the selected style.
# SELECT switches virtual/raw input; RATE measures the displayed device.
def pad_screen(shapes=False, raw=False):
    top, left, right, bottom = ("\u25b2", "\u25a0", "\u25cb", "\u00d7") if shapes else ("X", "Y", "A", "B")
    L = [row(" PortScope" + (", raw" if raw else ""), "RATE 200 Hz "), RULE]
    L.append(row("  L1", "R1  "))
    L.append(row("  L2  0.00", "0.67  R2  "))
    # Stick clicks share the shoulder-button row.
    L.append(row("  L3", "R3  "))
    L.append(" " * 10 + "\u2191" + " " * 29 + top)
    L.append(" " * 7 + "\u2190  \u00b7  \u2192" + " " * 23 + left + "     " + right)
    L.append(" " * 10 + "\u2193" + " " * 29 + bottom)
    L.append("")
    # Reserve five rows for pixel rings and dots from pad_shapes.
    L += [""] * 5
    L.append(" " * 5 + "X +0.02 Y -0.01" + " " * 18 + "X -0.41 Y -0.83")
    L.append(" " * 12 + "SELECT     HOME     START")
    L.append(" " * 19 + "M1       M2")
    # Use footer space for recent events; Home + START exits.
    L.append(" Last input")
    L.append(" ABS_RY    -27210")
    L.append(" ABS_Z      21580")
    return L

gamepad = pad_screen()
gamepad_shapes = pad_screen(shapes=True)

# Held and idle cell spans in pad_screen.
def pad_spans(shapes=False):
    bright, dim = [], []
    dim += [(2, 2, 4)]                      # L1
    bright += [(2, 49, 51)]                 # R1
    dim += [(3, 2, 4), (3, 6, 10)]          # L2 and its 0.00
    bright += [(3, 43, 47), (3, 49, 51)]    # 0.67 and R2
    dim += [(4, 2, 4), (4, 49, 51)]         # L3, R3
    dim += [(5, 10, 11), (6, 7, 8), (7, 10, 11)]   # up, left, down
    bright += [(6, 13, 14)]                 # right
    dim += [(5, 40, 41), (6, 37, 38), (7, 40, 41)]   # top, left, bottom face
    bright += [(6, 43, 44)]                 # A, or the circle
    dim += [(15, 12, 18), (15, 23, 27), (15, 32, 37), (16, 19, 21), (16, 28, 30)]
    return bright, dim

# Pixel stick rings and dots, centered in rows 9..13.
def pad_shapes(shapes=False):
    return dict(rings=[dict(row=11, col=9.5, rows=5, x=0.02, y=-0.01, clicked=False),
                       dict(row=11, col=41.5, rows=5, x=-0.41, y=-0.83, clicked=False)])

# ---- Japanese ----------------------------------------------------------
#
# system.language selects menus and console names; titles render in either
# language. Wide glyphs use ja26 with a Unifont fallback; see FONTS.md.
JA_STATUS = "\u97f3\u91cf 50%  \u660e\u308b\u3055 70%  \u96fb\u6c60 87%  23:59 "
def ja_header(crumb):
    return header(crumb, JA_STATUS)
def ja_crumb(name):
    return header("PortareOS  \u203a  " + name, CLOCK)

JA_QUICK = ["\u6700\u8fd1\u904a\u3093\u3060\u30b2\u30fc\u30e0",
            "\u304a\u6c17\u306b\u5165\u308a"]
JA_SYS = ["\u30cb\u30f3\u30c6\u30f3\u30c9\u30fc \u30b2\u30fc\u30e0\u30ad\u30e5\u30fc\u30d6",
          "NINTENDO64",
          "\u30d7\u30ec\u30a4\u30b9\u30c6\u30fc\u30b7\u30e7\u30f3",
          "\u30d7\u30ec\u30a4\u30b9\u30c6\u30fc\u30b7\u30e7\u30f32",
          "\u30d7\u30ec\u30a4\u30b9\u30c6\u30fc\u30b7\u30e7\u30f3\u30fb\u30dd\u30fc\u30bf\u30d6\u30eb",
          "\u30b9\u30fc\u30d1\u30fc\u30d5\u30a1\u30df\u30b3\u30f3",
          "\u30e1\u30ac\u30c9\u30e9\u30a4\u30d6",
          "\u30b2\u30fc\u30e0\u30dc\u30fc\u30a4\u30a2\u30c9\u30d0\u30f3\u30b9",
          "\u30c9\u30ea\u30fc\u30e0\u30ad\u30e3\u30b9\u30c8",
          "\u30a2\u30fc\u30b1\u30fc\u30c9"]
ja_systems = [ja_header("PortareOS"), RULE, "",
              row("  \u30af\u30a4\u30c3\u30af\u30a2\u30af\u30bb\u30b9", "")]
ja_systems += [item(i == 0, n) for i, n in enumerate(JA_QUICK)]
ja_systems += [THIN, row("  \u30b2\u30fc\u30e0\u6a5f", "")]
ja_systems += [item(False, n) for n in JA_SYS]
ja_systems += [THIN, row(" A \u6c7a\u5b9a   X \u8a2d\u5b9a", "")]

# Japanese filenames sort by code point, without readings. The selected long
# title scrolls by columns while keeping glyphs whole.
JA_SFC = sorted([
    "\u30af\u30ed\u30ce\u30fb\u30c8\u30ea\u30ac\u30fc",
    "\u30b9\u30fc\u30d1\u30fc\u30c9\u30f3\u30ad\u30fc\u30b3\u30f3\u30b02 \u30c7\u30a3\u30af\u30b7\u30fc&\u30c7\u30a3\u30c7\u30a3\u30fc",
    "MOTHER2 \u30ae\u30fc\u30b0\u306e\u9006\u8972",
    "\u30d5\u30a1\u30a4\u30ca\u30eb\u30d5\u30a1\u30f3\u30bf\u30b8\u30fc\u2163",
    "\u661f\u306e\u30ab\u30fc\u30d3\u30a3 \u30b9\u30fc\u30d1\u30fc\u30c7\u30e9\u30c3\u30af\u30b9",
    "\u30bc\u30eb\u30c0\u306e\u4f1d\u8aac \u795e\u3005\u306e\u30c8\u30e9\u30a4\u30d5\u30a9\u30fc\u30b9",
    "\u30ed\u30c3\u30af\u30de\u30f3X",
    "\u8056\u5263\u4f1d\u8aac2",
    "\u30b9\u30bf\u30fc\u30d5\u30a9\u30c3\u30af\u30b9",
    "\u60aa\u9b54\u57ce\u30c9\u30e9\u30ad\u30e5\u30e9",
    "\u30b9\u30fc\u30d1\u30fc\u30de\u30ea\u30aa\u30ef\u30fc\u30eb\u30c9",
    "\u30b9\u30fc\u30d1\u30fc\u30e1\u30c8\u30ed\u30a4\u30c9",
    "\u30b9\u30fc\u30d1\u30fc\u30ed\u30dc\u30c3\u30c8\u5927\u6226\u5916\u4f1d \u9b54\u88c5\u6a5f\u795e THE LORD OF ELEMENTAL",
    "\u30b9\u30fc\u30d1\u30fc\u30de\u30ea\u30aa \u30e8\u30c3\u30b7\u30fc\u30a2\u30a4\u30e9\u30f3\u30c9"])
JA_LONG = [t for t in JA_SFC if t.endswith("ELEMENTAL")][0]
def ja_footer(fav, pos, total):
    return row(" A \u8d77\u52d5   B \u623b\u308b   Y " +
               ("\u89e3\u9664" if fav else "\u304a\u6c17\u306b\u5165\u308a"),
               "%d / %d " % (pos, total))
def ja_games_screen(selected):
    shift = marquee_end(selected)
    lines = [ja_crumb("\u30b9\u30fc\u30d1\u30fc\u30d5\u30a1\u30df\u30b3\u30f3"), RULE, ""]
    lines += [game(t == selected, t, shift=shift) for t in JA_SFC]
    lines += [""] * (ROWS - 2 - len(lines))
    return lines + [THIN, ja_footer(False, 1 + JA_SFC.index(selected), 204)]
ja_games = ja_games_screen(JA_LONG)

# The days as Japanese says them: today, yesterday, then the weekday.
JA_RECENT = [("\u4eca\u65e5", [("\u30b9\u30fc\u30d1\u30fc\u30de\u30ea\u30aa\u30ef\u30fc\u30eb\u30c9", "SFC"),
                         ("\u9244\u62f33", "PS")]),
             ("\u6628\u65e5", [("\u30b9\u30fc\u30d1\u30fc\u30de\u30ea\u30aa \u30e8\u30c3\u30b7\u30fc\u30a2\u30a4\u30e9\u30f3\u30c9", "SFC"),
                         ("\u30e1\u30c8\u30ed\u30a4\u30c9", "FC")]),
             ("\u6708\u66dc\u65e5", [("\u30bd\u30a6\u30eb\u30ad\u30e3\u30ea\u30d0\u30fcII", "GC"),
                           ("\u30dd\u30c3\u30d7\u30f3\u30df\u30e5\u30fc\u30b8\u30c3\u30af \u30dd\u30fc\u30bf\u30d6\u30eb", "PSP")]),
             ("\u65e5\u66dc\u65e5", [("\u30b9\u30fc\u30d1\u30fc\u30e1\u30c8\u30ed\u30a4\u30c9", "SFC"),
                           ("\u30ef\u30a4\u30d7\u30a2\u30a6\u30c8XL", "PS")]),
             ("\u571f\u66dc\u65e5", [("\u30ed\u30c3\u30af\u30de\u30f3X", "SFC"),
                           ("\u30bc\u30ce\u30ae\u30a2\u30b9", "PS")])]
ja_recent = [ja_crumb("\u6700\u8fd1\u904a\u3093\u3060\u30b2\u30fc\u30e0"), RULE, ""]
for day, games_of_day in JA_RECENT:
    ja_recent.append(" " + day)
    ja_recent += [across(i == 0 and day == "\u4eca\u65e5", t, sy)
                  for i, (t, sy) in enumerate(games_of_day)]
ja_recent += [""] * (ROWS - 2 - len(ja_recent))
ja_recent += [THIN, row(" A \u8d77\u52d5   B \u623b\u308b   Y \u89e3\u9664", "")]

# Language & time shares one Settings row and shows the language in its own script.
JA_SET = [("WiFi", "Hofmann-5G"), ("SSH", "\u30aa\u30f3"), ("Bluetooth", "WH-1000XM4"),
          ("USB\u30e2\u30fc\u30c9", "\u30cd\u30c3\u30c8\u30ef\u30fc\u30af"),
          ("\u30dc\u30bf\u30f3\u8868\u8a18", "Retroid"),
          ("\u30b2\u30fc\u30e0\u6a5f", "\u5909\u66f4 2\u4ef6"),
          ("\u914d\u8272", "\u30b0\u30ec\u30fc"),
          ("\u30ab\u30e9\u30fc\u30d7\u30ed\u30d5\u30a1\u30a4\u30eb", "\u6a19\u6e96"),
          ("\u5145\u96fbLED", "\u30aa\u30f3"),
          ("\u8a00\u8a9e\u3068\u30bf\u30a4\u30e0\u30be\u30fc\u30f3", "\u65e5\u672c\u8a9e"),
          ("\u8a3a\u65ad", ""), ("\u60c5\u5831", "v0.6.0 2026-10-07"), ("\u96fb\u6e90", "")]
ja_settings = [ja_header("\u8a2d\u5b9a"), RULE]
ja_settings += [item(i == 9, n, v) for i, (n, v) in enumerate(JA_SET)]
ja_settings += [THIN, "    \u8a00\u8a9e\u3068\u30bf\u30a4\u30e0\u30be\u30fc\u30f3\u3092\u8a2d\u5b9a\u3057\u307e\u3059\u3002", ""]
ja_settings += [THIN, row(" A \u6c7a\u5b9a   B \u623b\u308b", "")]

# Name the language row in both languages so an accidental switch can be undone.
def lang_screen(ja):
    if ja:
        lines = [header("\u8a2d\u5b9a  \u203a  \u8a00\u8a9e\u3068\u30bf\u30a4\u30e0\u30be\u30fc\u30f3", CLOCK), RULE, ""]
        lines += [item(True, "\u8a00\u8a9e / Language", "\u65e5\u672c\u8a9e"),
                  item(False, "\u30bf\u30a4\u30e0\u30be\u30fc\u30f3", "Asia/Tokyo")]
        hint = " A \u5909\u66f4   B \u623b\u308b"
    else:
        lines = [header("Settings  \u203a  Language & time", CLOCK), RULE, ""]
        lines += [item(True, "Language / \u8a00\u8a9e", "English"),
                  item(False, "Timezone", "Europe/Zurich")]
        hint = " A CHANGE   B BACK"
    lines += [""] * (ROWS - 2 - len(lines))
    return lines + [THIN, row(hint, "")]
ja_language = lang_screen(True)
en_language = lang_screen(False)

# Mixed scripts under English menus: composed/decomposed Pokémon agree;
# Japanese renders normally; unsupported Hangul becomes ? per syllable.
GBC = sorted(["Dragon Warrior Monsters (USA)",
              "Legend of Zelda, The - Link's Awakening DX (USA, Europe)",
              "Poke\u0301mon - Gold Version (USA, Europe)",
              "Pok\u00e9mon - Silver Version (USA, Europe)",
              "Pok\u00e9mon Pinball (USA)",
              "Pok\u00e9mon Trading Card Game (USA)",
              "Shantae (USA)", "Wario Land 3 (World)",
              "\u30dd\u30b1\u30c3\u30c8\u30e2\u30f3\u30b9\u30bf\u30fc \u91d1",
              "\ud3ec\ucf13\ubaac\uc2a4\ud130 \uae08"], key=nfc)
GBC_SEL = [t for t in GBC if "Gold" in t][0]
games_any_script = [crumb("Game Boy Color"), RULE, ""]
games_any_script += [game(t == GBC_SEL, t) for t in GBC]
games_any_script += [""] * (ROWS - 2 - len(games_any_script))
games_any_script += [THIN, games_footer(False, 1 + GBC.index(GBC_SEL), len(GBC))]

if __name__ == "__main__":
    for t, s in (("SYSTEMS", systems), ("GAMES", games),
                 ("SETTINGS", settings), ("SETTINGS - CONSOLES", consoles),
                 ("BLUETOOTH", bluetooth),
                 ("KEYBOARD - letters", kb_lower),
                 ("KEYBOARD - shift, shape marks", kb_upper),
                 ("KEYBOARD - symbols, password hidden, DONE selected", kb_syms),
                 ("KEYBOARD - join failed", joining),
                 ("TOOLS", tools),
                 ("LAUNCHING", launching),
                 ("PROPOSED - SYSTEMS WITH QUICK ACCESS", systems_quick),
                 ("PROPOSED - GAMES, LONG TITLES", games_long),
                 ("PROPOSED - GAMES, A FAVOURITE SELECTED", games_fav),
                 ("PROPOSED - RECENTLY PLAYED", recent),
                 ("PROPOSED - FAVOURITES", favourites),
                 ("SETTINGS - DIAGNOSTICS", diagnostics),
                 ("PROPOSED - PORTSCOPE", gamepad),
                 ("PROPOSED - PORTSCOPE, SHAPE MARKS", gamepad_shapes),
                 ("PROPOSED - JAPANESE, SYSTEMS", ja_systems),
                 ("PROPOSED - JAPANESE, GAMES, A LONG TITLE SCROLLED", ja_games),
                 ("PROPOSED - JAPANESE, RECENTLY PLAYED", ja_recent),
                 ("PROPOSED - JAPANESE, SETTINGS", ja_settings),
                 ("PROPOSED - JAPANESE, LANGUAGE & TIME", ja_language),
                 ("PROPOSED - LANGUAGE & TIME", en_language),
                 ("PROPOSED - ENGLISH, TITLES IN ANY SCRIPT", games_any_script)):
        print(screen(t, s))
        print()
