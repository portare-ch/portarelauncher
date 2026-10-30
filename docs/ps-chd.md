# PS1, PS2, PSP, Neo Geo CD and Xbox disc titles

The launcher identifies PS1 (`psx`), PS2 (`ps2`), PSP (`psp`) and Neo Geo CD
(`neogeocd` or `neocd`) `.chd` files, and Xbox (`xbox`) `.iso` and `.chd` files,
offline, regardless of filename.
Only display labels change. Launch arguments, paths, favourites, recents,
folders, playlists and other systems retain their existing identities.
Identical titles remain separate files. Source names preserve available disc
labels; a serial identifies the underlying game, not a verified dump or every
revision, translation or modification.

## Build and package

Install CMake alongside the existing build dependencies. `make` builds
`ps-chd-id` and uses the checked-in `data/ps2-titles.tsv`,
`data/psx-titles.tsv`, `data/psp-titles.tsv`, `data/neocd-titles.tsv` and `data/xbox-titles.tsv`. Normal builds and runtime need
neither Python nor YAML. No build or runtime download is performed.

The checksum-pinned PCSX2 GameIndex and Libretro/Redump DATs are retained
losslessly as `.xz` files for offline regeneration. With Python 3 and PyYAML installed,
run `make regen-disc-title-tables` to recreate the tables and collision reports.
Checksums are verified against the original, decompressed source bytes.
The PS1 and PSP DATs share the same pinned Libretro revision and collision policy.
Unused upstream libchdr CI, examples, benchmarks, tests and auxiliary docs are
omitted; its decoder sources, build files and licenses are retained.

Device packaging must install the new artifacts:

```sh
make install-disc-titles DESTDIR=/path/to/package/root PREFIX=/usr
```

This adds `/usr/bin/ps-chd-id`,
`/usr/share/portarelauncher/ps2-titles.tsv`,
`/usr/share/portarelauncher/psx-titles.tsv`,
`/usr/share/portarelauncher/psp-titles.tsv`,
`/usr/share/portarelauncher/neocd-titles.tsv`,
`/usr/share/portarelauncher/xbox-titles.tsv`, and dependency notices under
`/usr/share/portarelauncher/notices/`. Use it alongside the existing launcher
and font packaging. Source archives retain the decoder sources, compressed
catalogue inputs and licenses. The YAML, DATs and upstream build files are not
installed on the device.
The PS2 table has 12,830 entries and is 599,092 bytes in UTF-8. The PS1 table
has 10,293 entries and is 505,263 bytes. The PSP table has 3,726 entries and is
192,947 bytes. The Neo Geo CD table has 96 entries and is 4,250 bytes. The Xbox
table has 6,375 entries, up to seven per title ID, and is 223,078 bytes. See
[PCSX2 provenance](../third_party/pcsx2/NOTICE) and
[codec notices](../third_party/CHD-NOTICES.txt). PS2 data carries PCSX2's
GPL-3.0-or-later terms. PS1/PSP/Neo Geo CD data retain Libretro's CC-BY-SA-4.0 terms and
[provenance](../third_party/libretro/NOTICE), separately from the launcher code
license. Xbox data comes from the same Libretro revision and terms. Each system has one fixed title source, with no runtime provider chain.

The PS1 and PSP generators use `game.name` and `rom.serial`. They remove parenthesized
revision/version labels, collapse identical resulting names and omit serials
whose remaining names differ. Regions, languages and disc labels are retained.
The 76 ambiguous PS1 serials are recorded in `data/psx-title-collisions.json`;
16 ambiguous PSP serials are recorded in `data/psp-title-collisions.json`.
Lookup of those serials keeps the filename. Input order never selects a title.

Menu labels omit parenthesized groups made entirely of recognized language
codes, such as `(En,Fr,De,Es,It,Nl,Pl,Ru)`, before UTF-8-safe truncation. Region,
disc, edition and subtitle labels remain. This applies to the PlayStation title tables,
including favourites and recents, and does not modify source data, file paths or
the identifier cache. For example, Killzone displays as **Killzone - Liberation
(USA)**. Filename fallbacks retain their existing behavior.

The current PortareOS `libretro-database` package installs cheats and cursors
under `/usr/share/libretro-database` and explicitly omits `.rdb` scanner/title
databases. Its Libretro revision matches the PS1/PSP data pinned here. There is
therefore no packaged title catalogue to reuse at runtime. These small build-time
exports follow the identifier/table design without adding an RDB parser.
See the [PortareOS recipe](https://github.com/portare-ch/portareos/blob/next/packages/emulators/libretro/libretro-database/package.mk).

`make` uses CMake's `MinSizeRel` configuration and enables interprocedural
optimization when the toolchain supports it. Before Neo Geo CD support, measured on Linux arm64, GCC 12.2:
the helper is 217,152 bytes, or 199,064 bytes after `strip`, versus 358,584 and
330,168 bytes for the previous Release build (40% smaller when stripped).
It statically includes libchdr, miniz 3.1.2
(Deflate), LZMA SDK 26.02, Zstandard 1.5.7, dr_flac (FLAC), and libchdr's
Huffman/CD codecs. `ldd` reports only libc and the ELF loader for the helper;
the launcher still depends on libc and libdrm. No emulator libraries are linked.
These measurements are build-specific; use `wc -c`, `size`, and `ldd` on the
packaged target binaries when changing toolchains or decoder options.

The helper enables libchdr's low-memory mode: sparse hunk-map checkpoints,
lazy codec allocation, smaller Huffman tables and in-place CD decoding. Raw
sector reconstruction, subcode decoding and block CRC verification stay enabled.
On a synthetic uncompressed CHDv5 with a 4 GiB logical size and 524,288 map
entries, median peak RSS across seven runs fell from 3,360 KiB to 1,320 KiB.
This measures map overhead; memory use varies with the image and codecs.

## Identification

```sh
ps-chd-id /absolute/path/game.chd
```

Success writes one normalized identifier, such as `SLUS-20946`, followed by a
newline. Exit 0 means identification, 1 means unsupported or no identification,
and 2 means an operational/I/O failure or invalid arguments. Diagnostics go to
stderr. The launcher passes the path directly in argv.

The helper uses pinned libchdr `47d794151b2b64f6aba60304e6480f4f5a2bc24b`.
It decodes only hunks needed for ISO9660 descriptors, the root directory and
`SYSTEM.CNF`, `PSP_GAME/PARAM.SFO`, Neo Geo CD's `IPL.TXT` and boot program header, or the
Xbox `default.xbe` header and certificate.
There is no full-disc hash, extraction,
temporary ISO or byte scan
for serial-like strings. Compressed hunk maps are still read and validated at
open, but only sparse checkpoints are retained; uncompressed map entries are
read on demand.

2048-byte DVD-style units and 2448-byte CD-style units are supported. CD track
metadata (`CHT2` or legacy `CHTR`) determines the track framing. Cooked Mode 1,
raw Mode 1, Mode 2 Form 1, raw Mode 2 and mixed Mode 2 Form 1 sectors are
supported. CD offsets include per-track four-frame padding and stored index-00
pregaps; generated pregaps consume no stored frames. Raw sync/mode bytes and
Mode 2 duplicated subheaders/Form 1 flags are checked. CD-wrapped DVD images
are supported as demonstrated by the real-image check below. PS1 primary volume
sizes may include audio tracks; actual directory/config reads stay inside the
selected data track. Form 2 sectors, GD-ROM, old binary `CHCD` metadata, multi-extent/interleaved files, and
parent-dependent CHDs without a parent fall back to filenames. The filesystem must expose an ISO9660
primary volume. Searched paths are root `SYSTEM.CNF` for PS1/PS2,
`PSP_GAME/PARAM.SFO` for PSP, and root `IPL.TXT` plus its named boot program for Neo Geo CD.

The ISO reader finds `SYSTEM.CNF` through its directory entry, accepts numeric
`;version` suffixes, and parses PS2 `BOOT2` or PS1 `BOOT` case-insensitively, accepting whitespace and
CR/LF variations. `BOOT2` takes precedence; an invalid BOOT2 cannot fall through
to BOOT. Boot paths and numeric version suffixes are removed, letters
uppercased, `_` changed to `-`, and the standard numeric dot removed. The result
must match four ASCII letters, a hyphen and five digits. Conflicting boot
assignments or conflicting identified data tracks produce no identification.

For PSP, the reader finds the `PSP_GAME` directory and `PARAM.SFO` file through
their ISO directory entries. It validates the SFO header, table bounds and entry
offsets, requires `DISC_ID` to have the expected string type, then normalizes it, for example
`UCUS98646` to `UCUS-98646`. Duplicate, malformed or missing DISC_ID values produce
no identification. SFO title text is not emitted by the helper; the fixed PSP
table supplies titles through the same cached serial resolver. A disc containing
multiple console metadata layouts is treated as ambiguous. The SFO format reference
is [PPSSPP ParamSFO.cpp](https://github.com/hrydgard/ppsspp/blob/master/Core/ELF/ParamSFO.cpp).

For Neo Geo CD, `IPL.TXT` supplies up to 32 `filename,bank,hex-address` entries.
The reader selects the single root `.PRG` loaded at bank/address zero, validates
`NEO-GEO` at offset `0x100`, and reads the big-endian game ID at `0x108`.
Only the first program sector is decoded, even for large executables. Case,
whitespace, CR/LF and optional DOS EOF markers are accepted. Missing/malformed
headers, multiple boot programs, unsupported filenames and mixed console layouts
keep the filename. No arbitrary PRG scan or inference from filenames is used.
The fixed-width helper/cache ID is `NGCD-` plus five uppercase hexadecimal digits,
for example `NGCD-00241` or Fatal Fury 3's `NGCD-0069C`; this is a program identity,
not the retail catalogue serial. The table uses the pinned Libretro/Redump DAT,
with explicit mappings for publisher catalogue numbers. Region, language and
revision labels are removed because the header cannot distinguish those editions.
The shared Janshin/Treasure of the Caribbean ID is omitted and recorded in
`data/neocd-title-collisions.json`. Unknown IDs and homebrew mappings retain
filenames. This identifies the underlying game, not modifications that reuse its ID.
Format references: [IPL file](https://wiki.neogeodev.org/index.php?title=IPL_file)
and [FBNeo hardware IDs](https://github.com/finalburnneo/FBNeo/blob/4033bc427be0538d637eceed40c430c4837d0780/src/burner/neocdlist_games.h).

## Xbox

Xbox games are XDVDFS volumes. hakuX plays plain XISOs as well as DVD CHDs
(`chdman createdvd`), so `.iso` files are eligible for `xbox` only; the helper
reads any file without the CHD signature as 2048-byte sectors. The volume
descriptor is the one at sector 32 with `MICROSOFT*XBOX*MEDIA` at both ends.
Rebuilt XISOs start the game partition at the beginning of the file; full
Redump dumps of XGD1, XGD2 and XGD3 discs start it at `0x18300000`,
`0xFD90000` and `0x2080000`. A descriptor found at more than one start is
ambiguous. So is a disc that also has PlayStation metadata.

The root directory is a binary tree of entries. The reader visits every node
once rather than relying on the tree's sort order, finds `default.xbe` without
regard to case, and rejects cycles, nodes reached twice, entries that cross a
sector, a second `default.xbe`, or one marked as a directory. From the XBE it
reads the image header and the certificate. The certificate must lie inside
the headers, which must fit in the file and in 64 KiB. Format references:
[XDVDFS](https://xboxdevwiki.net/XDVDFS) and [XBE](https://xboxdevwiki.net/Xbe).

The title ID's high half is the publisher's two letters and the low half the
game number, which Redump writes in decimal: Halo's `0x4D530004` is `MS-004`.
Releases in different regions often share a title ID under different names,
such as FIFA Soccer and FIFA Football, so the identifier also carries the
certificate's region flags (North America 1, Japan 2, rest of the world 4).
The fixed-width ID is `X`, then the letter `A` plus those flags, the publisher
letters, a hyphen and the five-digit number: `XBMS-00004` for Halo in North
America, `XEEA-00091` for Burnout 3 in Europe. Certificates without one of
the three flags, or whose publisher letters are not `A` to `Z`, keep the filename.

The table generator maps Redump regions to those flags: USA, Canada and Latin
America to North America; Japan to Japan; European countries, Scandinavia and
Australia to the rest of the world; Asian releases, which were sold with
either, to both; World to all three. For each title ID and each of the seven
flag combinations it collects the releases sold in any of the disc's regions.
Their names, without region, language, revision and packaging labels such as
Collector's Edition, must agree; otherwise that combination is omitted and
recorded in `data/xbox-title-collisions.json`. 583 combinations are, mostly
localized releases sharing one region, and magazine discs. Serials with a
disc suffix, such as `XK-030-0`, are not title IDs and are skipped.

Bounds: 32 volume descriptors, 1 MiB per searched directory, 32 KiB configuration,
32 KiB IPL with at most 32 entries, 64 KiB SFO with at most 256 entries,
1 MiB per Xbox directory, 64 KiB of XBE headers,
99 tracks, 1 MiB hunks, 4,194,304 hunks, and 16 GiB logical images or plain files. Linux helper
processes have a 256 MiB address-space cap and a 15-second CPU cap. The launcher
also enforces a 10-second wall-clock deadline. CHD size and hunk bounds are checked
before allocating the decoder's hunk map. Sanitizer builds skip both process
resource limits; ASan reserves a much larger virtual address range.

## Cache and scheduling

One persistent file lives at
`/storage/.cache/portarelauncher/disc-identities`. Its version-4 format stores
paths (hex encoded for literal tabs/newlines), size, nanosecond mtime, device,
inode, nanosecond ctime, status and normalized serial. A checksum and end marker
reject damaged/truncated snapshots. Both successful and stable no-ID results are
cached; operational failures, deadlines and cancellations are not persisted.
The stat signature is checked again before accepting a completed result.
Older cache versions are discarded so previous stable no-ID results are retried
with the Neo Geo CD reader.

Cached serials are looked up in the current title table before catalog sorting.
Lookups use the file's system, so identical serial strings in different systems cannot leak
titles across systems. A table-only update changes titles after launcher
restart without reopening unchanged CHDs. Files without a usable cached title
start with the filename fallback; missing tables or helpers do not prevent browsing
or launching. Limits are 32,768 cache records, 32,768 records and 2 MiB
per title table, a 48 MiB cache file and 32,768 queued files per list opening.
Title indexes allocate only the actual row count. On a 64-bit target the five
current indexes reserve 533,120 bytes combined.

Opening a supported system queues that system's discs; Quick Access queues all five systems. One asynchronous
helper runs at a time; an immutable path queue survives catalog resorting. The existing input
poll waits on helper stdout and Linux pidfd completion, plus a single deadline.
There is no metadata polling timer or daemon. Kernels without usable pidfds
keep cached titles and filenames without starting metadata polling. The UI
checks queued stat signatures in bounded groups of 64 between input waits.

Results update catalog names, then sort while restoring selection by path.
Favourites and recents read those same catalog titles and use the same cached
resolver for absent catalog paths. Their path storage and ordering are unchanged.
Metadata work is killed/reaped before emulator/tool handoff, catalog replacement,
suspend or exit. Pending work resumes on the next list opening. Completed work
is persisted every eight results and at queue completion/cancellation with a
flushed, fsynced sibling file and atomic rename.

## Validation (2026-10-18)

`make test` and `make test SAN=1` cover cache hits, stable negatives, transient
errors, replacement, changes during identification, database-only updates,
corrupt cache recovery, cancellation/reaping, UTF-8 boundaries, title ties,
selection by path, literal path arguments, malformed/maximum-size title tables,
helper timeout and input/completion
wakeups. The existing favourites/recents suite remains in those runs.

`make test-chd` checks PS1 BOOT/PS2 BOOT2, bounded PSP PARAM.SFO parsing,
Neo Geo CD IPL/program headers, retail-to-hardware ID mappings and ambiguous IDs,
PSP metadata in flat/CD-wrapped images, mixed audio/data volume sizing,
source-specific collision handling, and synthetic ISO9660 in actual CHDv5
containers, including
DVD-style units, multiple CD framing modes, preceding audio tracks, four-frame
padding, stored/generated pregaps, legacy metadata, config variations and
malformed/bounded inputs. With `chdman` installed (Debian `mame-tools`), it also
creates compressed DVD-style and mixed-track CD fixtures with the real encoder,
including metadata across sparse-map checkpoints and rejection of a corrupt
map checksum.
`make test-chd-san` runs the decoder and the same fixtures under ASan/UBSan.
CI includes chdman and runs all these targets.

A user-owned CHD was mounted read-only under the arbitrary name `game.chd`.
The 536,590,322-byte CHDv5 image has 2448-byte units, one cooked Mode 1 track,
and CD LZMA/Deflate/FLAC codecs. The helper identified `SLUS-20834`; the packaged
table resolves it to **King of Fighters 2000 & 2001 [Disc 1 of 2]**. No source
file was renamed or written. This validates a real CD-wrapped image.

A second user-owned CHD, **The Adventures of Lomax**, was mounted read-only.
This 409,208,699-byte CHDv5 image has one raw Mode 2 data track followed by
24 audio tracks with pregaps. Its ISO primary volume includes the audio area;
filesystem reads remain bounded to the 15,784-sector data track. The helper
identified `SCUS-94906`; the PS1 table resolves it to **Adventures of Lomax, The
(USA)**. Both images were checked with the sanitized decoder.

A third user-owned CHD, **Killzone: Liberation**, was mounted read-only under
an arbitrary filename. This 391,355,681-byte CHDv5 image uses one cooked Mode 1
track with 2448-byte units and CD LZMA/Deflate/FLAC codecs. The PSP SFO reader
identified `UCUS-98646`, resolving to **Killzone - Liberation (USA)** after label
cleanup. All three real files were checked through the launcher resolver,
including persisted cache reload with no helper available, preserving their
original paths. PSP was also checked with the sanitized decoder.

A separate user-owned DVD-style CHD, a conventional CD-sized PS2 disc, and
interactive navigation/emulator handoff on physical device hardware remain to
be checked. Synthetic DVD and CD fixtures cover those framing paths.

A fourth user-owned CHD, **Metal Slug 2**, was mounted read-only. This
476,241,496-byte CHDv5 image has 2448-byte units, a 20,437-frame raw Mode 1
data track and 19 audio tracks, with CD LZMA/Deflate/FLAC codecs. `IPL.TXT`
selects `MS2CD.PRG` at address zero; its header identifies `NGCD-00241`,
resolving to **Metal Slug 2**. The existing CD framing/decoder needed no changes;
the missing pieces were Neo Geo CD identification, a title table and launcher
system scheduling. The normal and sanitized helpers were checked against this
file under an arbitrary filename, through the launcher resolver and persisted
cache reload with the helper unavailable. The source file was not modified.
Unsupported IPL layouts and unverified game IDs still retain filenames.

CD layout reference: [MAME cdrom.cpp](https://github.com/mamedev/mame/blob/master/src/lib/util/cdrom.cpp).
The bounded ISO reader is local because the launcher had no filesystem reader;
compression is entirely the established libchdr dependency.
