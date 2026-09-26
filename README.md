# portarelauncher

A launcher for PortareOS that owns the panel through KMS. No compositor, no
GPU, no images. Black background, text only.

Design context and the reasoning behind replacing sway and EmulationStation is
in portare-ch/portareos#222.

## What it does

Lists consoles, lists games, launches one through `runemu.sh`, and offers a
settings menu. Nothing else.

A game made of several files is listed once: a .cue and its tracks, a .gdi
and its tracks, an .m3u and its discs, a CloneCD .ccd and its image. Every
file a sheet names is left out, and the sheet is what launches.

## Status

Runs on the device. It finds the systems that have games, lists them, lists
the games, and launches one.

Tools, the last row of the systems list, runs the scripts in the modules
folder - the file manager, the gamepad tester, PortMaster - named and
described from the folder's gamelist.xml.

Settings: Wi-Fi, SSH, Bluetooth, USB gadget mode, button style, color, color
profile, charging LED, time zone, About and Power all work. Power restarts or switches off after a
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


`docs/mockup.txt` is the layout at the real grid and `tools/mockup.py`
regenerates it — the mockup is a program rather than a picture so it cannot
drift from the dimensions it claims.

It is PortareOS's front-end: the image starts it as the UI service, and
EmulationStation and sway are gone. What is still missing before the public
beta is tracked in portareos under the beta milestone - faster movement
through long lists, remembering the selected game, and saying when a game
exits with an error (portare-ch/portareos#258 to #260).

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

## Releases

PortareOS builds a release, not a commit. To publish one, tag `main` and push the tag:

    git tag v0.2.0 && git push origin v0.2.0

`.github/workflows/release.yml` then builds and tests the tag as CI does. It publishes `portarelauncher-0.2.0.tar.gz` and its `.sha256`, and the launcher shows the tag under Settings > About. In PortareOS, the bump sets `PKG_VERSION` to the version and `PKG_SHA256` to the value from the `.sha256` file.

## Controls

The four face buttons are assigned by role, and the button style setting
decides which button has which role. Retroid, the default: A confirms, B
goes back, X opens settings. PS: cross confirms, circle goes back, triangle
opens settings. L1 is the keyboard's shift in both. The diagram
under the setting shows where the confirming button sits, and every hint
line names the buttons of the style in use.

| role | Retroid | PS | does | on the keyboard |
|---|---|---|---|---|
| confirm | A, right | cross, bottom | confirm, launch | type the focused key |
| back | B, bottom | circle, right | back | delete; leave when empty |
| settings | X, top | triangle, top | settings | space |
| L1 | | | — | shift: once for a letter, twice to lock |
| START | | | settings | join |
| SELECT | | | — | show or hide the password |
| Home + START | | | quit the running game, film or tool | — |

Home + START is the way out of everything the launcher starts, whatever
its own controls are. The launcher watches for it while the game has the
panel - asking the kernel for those two buttons only, so play does not wake
it - gives the program 1.5 s to leave on its own, as RetroArch does, then
sends it SIGTERM, then SIGKILL. Never to runemu.sh itself: its cleanup after
the emulator, fan and GPU and CPU settings, has to run.

The color ramp is chosen in Settings, where each choice is previewed as
the four levels it uses.

The color profile: `stock` leaves the display controller's color blocks off;
`Gamma 2.2` and `sRGB` load `/usr/config/color/gamma22.profile` or
`srgb.profile`, a 3x3 matrix and a 1024-entry gamma table each, into the
CRTC's CTM and GAMMA_LUT properties (color.c parses, kms.c writes). Both
correct to sRGB primaries and a D65 white and differ in the tone curve; gamma
2.2 is the one CRT-era games were drawn on. The controller keeps them across every later modeset, so the
correction holds for whatever runs after the launcher has dropped master:
every emulator, mpv, the launcher itself. It is applied before the first
frame at start-up when `display.colorprofile` names one, and switched
live from Settings. The profile file is the image's; the launcher only
carries it to the hardware.

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
  build, so a small tag scanner is enough.

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

## Licence and attribution

GPL-2.0, and not by preference - by obligation. `src/font8x16.h` is generated
from `lib/fonts/font_8x16.c` in Linux, which carries
`SPDX-License-Identifier: GPL-2.0`. Embedding that font data makes this
GPL-2.0 too. PortareOS is GPL-derived anyway, so nothing is lost, but the
licence is inherited rather than chosen and `tools/mkfont.py` records where
the bytes came from.

