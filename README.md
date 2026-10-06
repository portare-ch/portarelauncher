# portarelauncher

A lightweight, text-only launcher for [PortareOS](https://github.com/portare-ch/portareos).
Written in C, it draws directly to the display through KMS using libc and libdrm.

- Browse consoles and games, favourites and recently played games.
- Launch games through `runemu.sh` and tools such as PortMaster.
- Manage Wi-Fi, Bluetooth, display settings, power and system updates.

## Build

On Linux, install a C compiler, make, pkg-config and the libdrm development
headers, then run:

```sh
make
```

To build for the device on an Apple Silicon Mac using Apple's `container`:

```sh
sh tools/build.sh
```

The launcher runs as the PortareOS UI service and requires access to the
display and input devices.

## Controls

| Action | Retroid (default) | Shape marks |
|---|---|---|
| Confirm / launch | A | Cross |
| Back | B | Circle |
| Settings | X or START | Triangle or START |
| Toggle favourite | Y | Square |
| Return to consoles | Home | Home |
| Quit the running game or tool | Home + START | Home + START |

Button style can be changed in Settings.

## Development

Run the host tests without libdrm or device hardware:

```sh
make test
make test SAN=1  # AddressSanitizer and UBSan
```

CI builds and runs both test modes on arm64. Version tags (`v*`) publish a
source archive and SHA-256 checksum through the release workflow.

Layout notes and mockups are in [docs/](docs/). Regenerate mockups with
`make mockup`.

## License

[GPL-2.0](LICENSE). The embedded font comes from Linux's
`lib/fonts/font_8x16.c`; its provenance is recorded in
[tools/mkfont.py](tools/mkfont.py).
