# portarelauncher

A launcher for PortareOS that owns the panel through KMS. No compositor, no
GPU, no images. Black background, text only.

Design context and the reasoning behind replacing sway and EmulationStation is
in portare-ch/portareos#222.

## What it does

Lists consoles, lists games, launches one through `runemu.sh`, and offers a
settings menu. Nothing else.

## Status

Runs on the device. It finds the systems that have games, lists them, lists
the games, and launches one.

Tools, the last row of the systems list, runs the scripts in the modules
folder - the file manager, the gamepad tester, PortMaster - named and
described from the folder's gamelist.xml.

Settings: Wi-Fi, Bluetooth, USB gadget mode, button style, colour, time
zone, About and Power all work. Power restarts or switches off after a
second press, with the panel switched off first. The time zone is a region, then a city, each city
shown with the time it is there now. About shows the version, commit, build date, device and IP
address, and holds Update, which asks GitHub what the chosen channel -
nightly or release - has for this device, downloads it over Wi-Fi with a
progress bar, and offers a restart once portareos-update has checked and
staged it. Wi-Fi lists saved profiles and what is in range, connects to a saved
one, and joins a new one through an on-screen keyboard;
Bluetooth powers the adapter, scans, pairs, connects and has the
auto-connect toggle. Brightness and volume are shown in the header rather
than set here, because the volume keys already set them.

The keyboard reaches all 95 printable ASCII characters, because a WPA
passphrase may use any of them. A join that fails leaves nothing saved -
NetworkManager writes the profile before it knows the password was right,
so a wrong one is deleted rather than left in the list looking joinable.
Enterprise (802.1X) networks need a username as well and are not supported.

`docs/mockup.txt` is the layout at the real grid and `tools/mockup.py`
regenerates it — the mockup is a program rather than a picture so it cannot
drift from the dimensions it claims.

It is PortareOS's front-end: the image starts it as the UI service, and
EmulationStation and sway are gone. What is still missing before the public
beta is tracked in portareos under the beta milestone - multi-file games
listed once, faster movement through long lists (portare-ch/portareos#255
to #260). Nothing has been measured about idle power yet.

## Building

Needs libdrm headers, which the device does not ship, so it is built in a
container:

    sh tools/build.sh

Debian bookworm is glibc 2.36 and the device is 2.41; glibc is forward
compatible, so a binary built there runs here. Native arm64 under Apple's
`container`, so this is a compile rather than a cross-compile.

## Tests

    make test          # every module that can run off the device
    make test SAN=1    # the same under AddressSanitizer and UBSan

Host programs in `tests/`, one per module, and they need no libdrm, so they
run on a laptop as well as on the device's architecture. What they cover is
what has no screen: the catalogue scan, `system.cfg` reads and writes, the
Tools folder, the Wi-Fi and Bluetooth parsers, running a command with a
ceiling, and the keyboard - including a search that proves every printable
character can be typed. Wi-Fi and Bluetooth are tested against recorded
`nmcli` and `bluetoothctl` output replayed by `tests/fake_proc.c`, so no
radio is involved. Drawing, KMS and input are not covered here; that is the
on-device checklist in portare-ch/portareos#238.

CI (`.github/workflows/ci.yml`) builds with warnings as errors on arm64 and
runs both, on every push to main and every pull request.

## Controls

Bindings are by position and never move. The bottom button confirms and the
right one goes back, on any pad. The button style setting changes what the UI
calls them — `B`/`A` on a Retroid, cross and circle on a PS pad — and nothing
else.

| position | does | on the keyboard |
|---|---|---|
| bottom | confirm, launch | type the focused key |
| right | back | delete; leave when empty |
| left | settings | space |
| top | — | shift: once for a letter, twice to lock |
| START | settings | join |
| SELECT | — | show or hide the password |

The colour ramp is chosen in Settings, where each choice is previewed as
the four levels it uses.

## The grid

The panel is 1280x960 over 91x68 mm, which is 14.07 px/mm. An 8x16 bitmap
font at 3x gives:

| font | cell | grid | char width | cap height |
|---|---|---|---|---|
| 8x16 at 2x | 16x32 | 80x30 | 1.14 mm | 1.42 mm |
| **8x16 at 3x** | **24x48** | **53x20** | **1.71 mm** | **2.13 mm** |
| 8x16 at 4x | 32x64 | 40x15 | 2.27 mm | 2.84 mm |

Phone body text is about 2.0 mm of cap height, read at arm's length. 80x30 is
the authentic DOS grid and it is squint-territory on a 5.5" panel; 3x lands on
the size people actually read. Integer scale only, so there is no
intermediate.

## Language: C

Chosen, with the alternatives written down so the choice can be argued with.

**What the program actually does** is mode-setting ioctls, evdev reads, a
memcpy into a mapped buffer, and `fork`/`exec`. That is C's subject matter,
and every reference implementation for KMS is C.

**It has to idle at nearly zero.** The screen is static almost all the time.
A runtime that wakes up on its own — a GC, a scheduler, a timer thread —
spends battery to do nothing, which is the one thing this program must not do.
That rules out Go regardless of its other merits.

**It ships inside a LibreELEC-derived distro** whose packages are a
`package.mk` and a Makefile. A C program drops in with no new build
infrastructure at all.

### Rust

The real alternative, and the argument against it is not about the language.

PortareOS builds Rust already, in a separate CI stage that exists for
`scx-scheds`. Putting the front-end behind that stage couples "can the device
show a menu" to "did the Rust stage build". For the one component whose
failure mode is a black screen and no way in, that coupling is worth avoiding.

If the launcher grows past what C should be trusted with — networking, a
scraper, anything parsing untrusted input — that trade changes and this should
be revisited.

### Zig

`zig cc` is an excellent cross-compiler for C and is worth using for local
iteration. Writing the launcher *in* Zig would add another toolchain to the
distro build for no benefit the program needs.

## Dependencies

libc and libdrm. That is the whole list.

- **libdrm** rather than raw ioctls. It is already on the device
  (`libdrm.so.2`) and already a package in the distro, so it costs nothing,
  and hand-marshalling mode-setting structs is exactly where subtle bugs
  live.
- **No GBM, EGL, Vulkan or Mesa.** A text screen on a black field is a dumb
  buffer and a memcpy. The GPU never leaves idle while the menu is up.
- **No libevdev.** Reading `struct input_event` from `/dev/input/event*` is a
  `read()`.
- **No XML library.** `es_systems.cfg` and `gamelist.xml` are generated by the
  build, so a small tag scanner is enough. This is a trade, not a free lunch:
  it is not a real parser, and game names carrying entity escapes need
  handling explicitly.

## Energy

The panel is OLED, so a black pixel is an emitter that is switched off. Text
on black draws a fraction of what a themed UI draws, and the saving is
physical rather than notional.

Two rules follow, and they are the design rather than an optimisation:

1. **Repaint on change only.** Page flip when something moves, then block on
   input. The CRTC keeps scanning the same buffer for free. A menu that
   redraws a static screen 120 times a second is the one expensive thing it
   could do.
2. **Light as few pixels as possible.** Selection is a marker and a brighter
   glyph rather than a full-width inverse bar — a bar lights about 50 cells to
   say what one caret says.

## Handing over the display

The launcher is DRM master. To start a game it drops master, runs
`runemu.sh`, and takes master back when the child exits:

```c
drmDropMaster(fd);          /* before exec  */
/* emulator holds the panel */
drmSetMaster(fd);           /* child exited */
drmModeSetCrtc(...);        /* our buffer is no longer scanned out */
```

This is the part that gets simpler by doing it ourselves. Today `runemu.sh`
stops sway, sleeps two seconds waiting for DRM master to be released, and
restarts the front-end afterwards. None of that is needed when the front-end
never goes away.

## Licence and attribution

GPL-2.0, and not by preference - by obligation. `src/font8x16.h` is generated
from `lib/fonts/font_8x16.c` in Linux, which carries
`SPDX-License-Identifier: GPL-2.0`. Embedding that font data makes this
GPL-2.0 too. PortareOS is GPL-derived anyway, so nothing is lost, but the
licence is inherited rather than chosen and `tools/mkfont.py` records where
the bytes came from.

### The face button symbols

The PS style draws its marks out of CP437, the IBM VGA character set from
1981: `0x1E` for the triangle, `0xFE` for the square, `0x09` for the circle,
and a plain letter X for the cross. They are approximations rather than the
real symbols, and the distinction matters.

Sony holds trademarks on the PlayStation face-button symbol set. Trademark
protects a mark used as an indicator of origin - the question is whether use
suggests that Sony made this or endorsed it. Generic geometric shapes, in
monochrome, out of a character set that predates the PlayStation by thirteen
years, used to say which physical button to press on somebody else's
controller, are descriptive rather than source-identifying. Basic shapes carry
no copyright either, having no originality to protect.

The names in the menu - "Retroid" and "PS" - are the more visible trademark
use, and naming a product to describe what a setting is for is ordinary
nominative use. Every emulator frontend does the same thing. "PS" rather than
"Sony" is an abbreviation rather than a company name, which is a slightly
smaller surface; it does not really change the analysis.

What would change the answer: using Sony's stylised, coloured glyphs rather
than generic shapes, putting them on packaging or marketing, or anything that
implies endorsement. None of which this does.

This is reasoning, not legal advice.
