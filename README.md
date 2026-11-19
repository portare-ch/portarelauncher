# portarelauncher

A lightweight, text-only launcher for [PortareOS](https://github.com/portare-ch/portareos).
Written in C, it draws directly to the display through KMS using libc and libdrm.

- Browse consoles and games, favourites and recently played games.
- Recognize PS1, PS2, PSP and Neo Geo CD CHD titles, and Xbox ISO and CHD titles, offline, preserving filenames and launch paths.
- Launch games through `runemu.sh` and tools such as PortMaster.
- Manage Wi-Fi, Bluetooth, display settings, power and system updates.
- Check every button and stick with PortScope, under Settings > Diagnostics.
- In English or Japanese, under Settings > Language & time. Japanese titles
  and supported accented characters display in either language.

## Build

On Linux, install a C compiler, make, CMake, pkg-config and the libdrm development
headers, then run:

```sh
make
```

This builds `portarelauncher`, `portscope` and `ps-chd-id`.

To build for the device on an Apple Silicon Mac using Apple's `container`:

```sh
sh tools/build.sh
```

The launcher runs as the PortareOS UI service and requires access to the
display and input devices.

### Other Linux systems (ARM64 or x86-64)

The launcher builds and runs on x86-64 as it does on ARM64. It uses the first
graphics card that drives a display, and reads each controller's stick range,
so a pad that reports 0 to 255 moves the menu like the device's own.

Outside PortareOS, give it a directory that holds PortareOS's layout and
build with `ROOT` set to it:

```sh
make ROOT="$HOME/.local/share/portarelauncher"
```

Under that directory the launcher expects:

| Path | Content |
|---|---|
| `usr/config/emulationstation/es_systems.cfg` | The systems, their game folders and extensions |
| `usr/bin/runemu.sh` | Starts a game: `runemu.sh <game> -P<system> --core=<core> --emulator=<emulator>` |
| `usr/bin/portscope`, `usr/bin/ps-chd-id` | From this build |
| `usr/share/portarelauncher/` | The title tables and `ja26.bin` from `data/` |
| `storage/.config/system/configs/system.cfg` | Settings (created empty) |
| `storage/.config/portarelauncher/`, `storage/.cache/portarelauncher/` | Lists and the title cache |

A system's `<command>` in `es_systems.cfg` names the program to run in place
of `usr/bin/runemu.sh`. Wi-Fi, updates, USB mode and sleep depend on
PortareOS's own programs.

Run the launcher as a systemd service without a controlling terminal. Each
game runs in its own process group, so terminal job control can suspend an
emulator that accesses the terminal. The service needs access to the DRM and
input devices. Stop any compositor or display server using the DRM card first.

From the build directory, start a transient service with the `video` and
`input` groups and send its output to the journal.

```sh
sudo systemd-run --unit=portarelauncher --collect --uid="$(id -un)" \
  -p 'SupplementaryGroups=video input' \
  -p StandardInput=null -p StandardOutput=journal -p StandardError=journal \
  "$PWD/portarelauncher"
```

## Controls

| Action | Retroid (default) | Shape marks |
|---|---|---|
| Confirm / launch | A | Cross |
| Back | B | Circle |
| Settings (from Systems) | X or START | Triangle or START |
| Toggle favourite | Y | Square |
| Return to Systems (in the launcher) | Home | Home |
| Quit the running game or tool | Home + START | Home + START |

Button style can be changed in Settings > Button style.

Settings > Power controls the menu's screen-off timeout and the delay from
screen off to sleep. A direction or face button wakes a blanked menu; that
press does not select anything. These timeouts do not apply to games.

The timezone selected under Settings > Language & time determines daylight
saving changes automatically, using the system's timezone database.

## Development

Run the host tests without libdrm or device hardware:

```sh
make test
make test SAN=1  # AddressSanitizer and UBSan
make test-chd    # Disc title helper; compressed fixtures also use chdman if installed
make test-chd-san
```

`make test-chd` and title-table regeneration require Python 3 with PyYAML;
`make test-chd-san` needs only Python 3.
Normal builds use the checked-in title tables. `make regen-disc-title-tables`
recreates them offline from the compressed upstream catalogues.

CI builds and runs both test modes on arm64 and x86-64. Version tags (`v*`) publish a
source archive and SHA-256 checksum through the release workflow.

Layout notes and mockups are in [docs/](docs/). Regenerate mockups with
`make mockup` (Python 3). These are illustrative layouts, with some differences
from the current UI noted in the documents.

[Offline disc titles](docs/ps-chd.md) documents packaging, provenance, binary size,
cache behavior and validation, including the remaining physical-device checks.

## License

[GPL-2.0](LICENSE). The embedded font comes from Linux's
`lib/fonts/font_8x16.c`; its provenance is recorded in
[tools/mkfont.py](tools/mkfont.py). Japanese glyphs use Noto Sans CJK JP with
GNU Unifont as a fallback; [FONTS.md](FONTS.md) has the notices for all three fonts.
PCSX2/Libretro title data and the helper's decoder dependencies have separate notices
in [third_party/](third_party/).
