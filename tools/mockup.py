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

# The header as draw_frame draws it: the status at the right edge, the
# crumb cut to end two columns short of it. On the device the games
# screen's crumb is the system's name alone.
STATUS = "VOL 50%  BRI 70%  BAT 87%  23:59 "
CLOCK = "23:59 "
def header(crumb, status=STATUS):
    room = COLS - 1 - len(status) - 2 - 1
    return row(" " + crumb[:room], status)

# A list of games is a step down from the Systems screen, and its header
# says so: the crumb, and the clock alone at the right. Volume, brightness
# and battery stay on the Systems screen; a game list is not the place to
# read them, and a count of the rows is in the footer already.
def crumb(name):
    return header("PortareOS  \u203a  " + name, CLOCK)
THIN = " " + "\u2500" * (COLS - 2)

SYS = [("GameCube", 24), ("Nintendo 64", 9), ("PlayStation", 112),
       ("PlayStation 2", 2), ("PlayStation Portable", 31), ("SNES", 204),
       ("Mega Drive", 88), ("Game Boy Advance", 140), ("Dreamcast", 6),
       ("Arcade", 47)]

systems = [header("PortareOS"), RULE, ""]
systems += [item(i == 0, n, str(c)) for i, (n, c) in enumerate(SYS)]
systems += [""] * (ROWS - 2 - len(systems))
systems += [THIN, row(" A SELECT   X SETTINGS", "")]

GAMES = ["Tekken 2", "Tekken 3", "Tomb Raider", "Tony Hawk's Pro Skater 2",
         "Vagrant Story", "Wipeout XL", "Xenogears"]
games = [crumb("PlayStation"), RULE, ""]
games += [item(n == "Tekken 3", n) for n in GAMES]
games += [""] * (ROWS - 2 - len(games))
games += [THIN, row(" A LAUNCH   B BACK", "8 / 112 ")]

SET = [("Wi-Fi", "Hofmann-5G"), ("SSH", "on"), ("Bluetooth", "WH-1000XM4"),
       ("USB mode", "network"), ("Button style", "Retroid"),
       ("Consoles", "2 changed"),
       ("Color", "grey"), ("Color profile", "stock"), ("Charging LED", "on"),
       ("Time zone", "Europe/Zurich"), ("About", "v0.2.6 2026-09-26"),
       ("Power", "")]
# Twelve settings leave two rows between the rules; the button diagram needs
# three and takes the upper rule's row, every other setting uses one or two.
settings = [row(" Settings", ""), RULE, ""]
settings += [item(i == 4, n, v) for i, (n, v) in enumerate(SET)]
settings += ["         X          X confirm",
             "      Y     A       A back",
             "         B          X settings"]
settings += [THIN, row(" A CHANGE   B BACK", "\u2191\u2193 MOVE ")]

# Settings > Consoles: the pre-emptive frame per console, and what it
# means, wrapped at 45 columns as wrap_puts does it on the device.
CONSOLES = [("SNES", "PRMPT on"), ("NES", "PRMPT off"),
            ("PlayStation", "PRMPT off"),
            ("Game Boy", "PRMPT off"),
            ("Game Boy Color", "PRMPT off"), ("Game Boy Advance", "PRMPT off"),
            ("Genesis", "PRMPT off")]
def consoles_screen(sel):
    lines = [row(" Settings  \u203a  Consoles", ""), RULE, ""]
    lines += [item(i == sel, n, v) for i, (n, v) in enumerate(CONSOLES)]
    lines += [THIN, "    Experimental."]
    lines += ["    PRMPT adds a pre-emptive frame to reduce",
              "    input lag."]
    lines += [""] * (ROWS - 2 - len(lines))
    lines += [THIN, row(" A CHANGE   B BACK", "\u2191\u2193 MOVE ")]
    return lines

consoles = consoles_screen(0)

# Two toggles and the devices in one list: switching it on, letting known
# headphones come back, and picking them when they have not.
BT = [("WH-1000XM4", "connected"), ("DualSense Edge", "paired"),
      ("Bose QC35", "paired"), ("JBL Flip 5", "new")]
bluetooth = [row(" Settings  \u203a  Bluetooth", ""), RULE, "",
             item(False, "Bluetooth", "on"),
             item(False, "Auto-connect paired devices", "yes"),
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

RETROID_HINTS = " A TYPE  B DELETE  X SPACE  L1 SHIFT  START JOIN"
# The PS printing, approximated out of CP437 as the launcher draws it.
PS_HINTS      = " X TYPE  ○ DELETE  ▲ SPACE  L1 SHIFT  START JOIN"

def kb_screen(ssid, text, layer, sel=None, bottom_sel=None, hidden=False,
              note="SELECT: show or hide password", labels=None,
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
                    note="Password rejected. Check and retry.")

# Tools: whatever scripts are in the modules folder, named and described
# from its gamelist.xml. The description gets fixed room under the list,
# because it is where a tool says how to get back out of it.
TOOLS = ["File Manager", "PortMaster", "Remove ._ Files",
         "Start RetroArch (64-bit)", "Test Gamepad"]
tools = [header("Tools"), RULE, "",
         row("  TOOLS", "5 found  "), ""]
tools += [item(n == "Test Gamepad", n) for n in TOOLS]
tools += [""] * (ROWS - 2 - 4 - 1 - len(tools))
tools += [THIN,
          "    A simple SDL GUI gamepad tester to help",
          "    validate gamepad inputs. To exit, hold L1 and",
          "    press START + SELECT.", ""]
tools += [THIN, " B RUN   A BACK"]

launching = [header("PortareOS"), RULE] + [""] * 4
launching += ["           Tekken 3 (USA)", "",
              "           Starting..."]
launching += [""] * 9

# ---- proposed: quick access, recent, favourites, long titles -----------
#
# From a tester who filled several systems: a way back to a game without
# scrolling the list, and titles that do not stop at the panel's edge.
# Quick Access is a category of its own above the systems, cross-system
# shortcuts first, platforms below, a thin rule between and no blank row:
# on 20 rows whitespace is the expensive thing. Recent comes first because
# it needs no curation. The cross-system views use short system names so
# the title keeps the width the long-title work just won.
QUICK = [("Recently played", 10), ("Favourites", 7)]
systems_quick = [header("PortareOS"), RULE, "",
                 row("  QUICK ACCESS", "")]
systems_quick += [item(i == 0, n, str(c)) for i, (n, c) in enumerate(QUICK)]
systems_quick += [THIN, row("  SYSTEMS", "")]
systems_quick += [item(False, n, str(c)) for n, c in SYS]
systems_quick += [THIN, row(" A SELECT   X SETTINGS", "")]

# A title wider than the column is cut with ... while it is not selected.
# The selected row scrolls instead: still for 1 s, then one column left
# every 150 ms until the end is in view, 1.5 s there, back to the start.
# No wrap, so the ending the reader was after is not followed by what
# they already read. Only the selected row ever moves. Nothing marks a
# favourite in the list: Favourites is where favourites are.
TITLE_W = COLS - 4 - 2    # marker and indent, then the right margin
def cut(t):
    return t if len(t) <= TITLE_W else t[:TITLE_W - 3] + "..."
def scrolled(t, shift):
    return t[shift:shift + TITLE_W]
def game(sel, title, shift=0):
    return item(sel, scrolled(title, shift) if sel else cut(title))

# The footer is the favourite cue: Y FAVOURITE under a game that is not
# one, Y REMOVE under one that is. Nothing in the list itself says so.
# Its right end carries the position, "9 / 204", where the MOVE hint
# used to be: the d-pad needs no hint, and the row the count had, with
# the blanks around it, goes to the list - 14 games on screen, not 10.
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

# Ten launches, newest first, written when runemu exits with status 0,
# so a game that never started is not in it. Grouped under the day in
# small caps: TODAY, YESTERDAY, then the weekday for the last week and
# the date beyond it. A row is the title and a short system name; the
# day is the heading, not a third column. This sample's ten games fall on
# five days and fill the panel exactly; on more days the list scrolls, the
# headings with it.
RECENT = [("TODAY", [("Super Mario World", "SNES"), ("Tekken 3", "PS1")]),
          ("YESTERDAY", [("Yoshi's Island - Super Mario World 2", "SNES"),
                         ("Metroid (USA)", "NES")]),
          ("MONDAY", [("Soulcalibur II", "GC"), ("Pop'n Music Portable", "PSP")]),
          ("SUNDAY", [("Super Metroid", "SNES"), ("Wipeout XL", "PS1")]),
          ("SATURDAY", [("Mega Man X", "SNES"), ("Xenogears", "PS1")])]
def across(sel, title, right):
    w = COLS - 4 - len(right) - 3
    return item(sel, title if len(title) <= w else title[:w - 3] + "...", right)
recent = [crumb("Recently played"), RULE, ""]
for day, games_of_day in RECENT:
    recent.append(" " + day)
    recent += [across(t == "Super Mario World", t, sy) for t, sy in games_of_day]
recent += [""] * (ROWS - 2 - len(recent))
recent += [THIN, row(" A LAUNCH   B BACK   Y REMOVE", "")]

# One file, a game path per line, pruned of paths that no longer exist
# when it is read. Y toggles the selected game wherever a game is listed;
# in here the same key removes. Hints name keys, A B X Y, nothing else,
# and no glyph stands for a favourite anywhere.
FAVS = [("Final Fantasy III (USA) (Rev 1)", "SNES"),
        ("Legend of Zelda, The - A Link to the Past (USA)", "SNES"),
        ("Super Mario World (USA)", "SNES"), ("Tekken 3", "PS1"),
        ("Castlevania - Symphony of the Night", "PS1"),
        ("Soulcalibur II", "GC"), ("Mario Kart 64", "N64")]
favourites = [crumb("Favourites"), RULE, ""]
favourites += [across(i == 2, t, sy) for i, (t, sy) in enumerate(FAVS)]
favourites += [""] * (ROWS - 2 - len(favourites))
favourites += [THIN, games_footer(True, 3, 7)]

# ---- proposed: the gamepad tester -----------------------------------------
#
# Tools > Test Gamepad, as one more launcher screen rather than a program
# that looks like something else: the same grid, font and palette, nothing
# decorative. The one thing a menu never needs and this does is a diagram
# of the pad, drawn in line glyphs. A control is bright while it is held
# and dim while it is not, so the tester takes whatever palette the launcher
# has. The face buttons follow the button style setting: the Retroid letters
# or the PS marks the launcher already approximates out of CP437.
#
# Sample state: A down, D-pad right, R1 held, R2 two thirds in, the right
# stick up and left. One layer at a time: the virtual pad games see by
# default, the MCU's raw device when started with --raw; the footer says
# which, in three words, so nobody wonders what they are looking at.
def pad_screen(ps=False, raw=False):
    top, left, right, bottom = ("\u25b2", "\u25a0", "\u25cb", "\u00d7") if ps else ("X", "Y", "A", "B")
    L = [row(" Gamepad tester", "200 Hz "), RULE]
    L.append(row("  L1", "R1  "))
    L.append(row("  L2  0.00", "0.67  R2  "))
    L.append("")
    L.append(" " * 10 + "\u2191" + " " * 29 + top)
    L.append(" " * 7 + "\u2190  \u00b7  \u2192" + " " * 23 + left + "     " + right)
    L.append(" " * 10 + "\u2193" + " " * 29 + bottom)
    L.append("")
    box = ["\u250c\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510",
           "\u2502       \u2502", "\u2502       \u2502", "\u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518"]
    lstick = [box[0], "\u2502   \u00b7   \u2502", box[2], box[3]]
    rstick = [box[0], "\u2502 \u2022     \u2502", box[2], box[3]]
    for i in range(4):
        L.append(" " * 5 + lstick[i] + " " * 24 + rstick[i])
    L.append(" " * 5 + "X +0.02 Y -0.01" + " " * 18 + "X -0.41 Y -0.83")
    L.append("")
    L.append(" " * 12 + "SELECT     HOME     START")
    L.append(" " * 19 + "M1       M2")
    L.append(row(" Last   ABS_RY -27210   ABS_Z 21580", ""))
    L.append(THIN)
    L.append(row(" HOME + START  BACK", ("MCU, raw " if raw else "as games see it ")))
    return L

gamepad = pad_screen()
gamepad_ps = pad_screen(ps=True)

# Which cells are bright (held) and which dim (idle); the rest is text.
# Rows and columns of pad_screen above; one list serves both variants.
def pad_spans(ps=False):
    bright, dim = [], []
    dim += [(2, 2, 4)]                      # L1
    bright += [(2, 49, 51)]                 # R1
    dim += [(3, 2, 4), (3, 6, 10)]          # L2 and its 0.00
    bright += [(3, 43, 47), (3, 49, 51)]    # 0.67 and R2
    dim += [(5, 10, 11), (6, 7, 8), (7, 10, 11)]   # up, left, down
    bright += [(6, 13, 14)]                 # right
    dim += [(5, 40, 41), (6, 37, 38), (7, 40, 41)]   # top, left, bottom face
    bright += [(6, 43, 44)]                 # A, or the circle
    dim += [(r, 5, 14) for r in range(9, 13)] + [(r, 38, 47) for r in range(9, 13)]
    bright += [(10, 9, 10), (10, 40, 41)]   # the two dots
    dim += [(15, 12, 18), (15, 23, 27), (15, 32, 37), (16, 19, 21), (16, 28, 30)]
    return bright, dim

if __name__ == "__main__":
    for t, s in (("SYSTEMS", systems), ("GAMES", games),
                 ("SETTINGS", settings), ("SETTINGS - CONSOLES", consoles),
                 ("BLUETOOTH", bluetooth),
                 ("KEYBOARD - letters", kb_lower),
                 ("KEYBOARD - shift, PS button style", kb_upper),
                 ("KEYBOARD - symbols, password hidden, DONE selected", kb_syms),
                 ("KEYBOARD - join failed", joining),
                 ("TOOLS", tools),
                 ("LAUNCHING", launching),
                 ("PROPOSED - SYSTEMS WITH QUICK ACCESS", systems_quick),
                 ("PROPOSED - GAMES, LONG TITLES", games_long),
                 ("PROPOSED - GAMES, A FAVOURITE SELECTED", games_fav),
                 ("PROPOSED - RECENTLY PLAYED", recent),
                 ("PROPOSED - FAVOURITES", favourites),
                 ("PROPOSED - GAMEPAD TESTER", gamepad),
                 ("PROPOSED - GAMEPAD TESTER, PS BUTTON STYLE", gamepad_ps)):
        print(screen(t, s))
        print()
