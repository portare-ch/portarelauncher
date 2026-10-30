#!/usr/bin/env python3
"""Pinned Libretro/Redump Xbox DAT -> XBE title ID<TAB>title."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import re
from gen_psx_titles import parse, REVISION

SOURCE_SHA256 = '2630f780019b6e360a4dad54d22bb7b65a3ebb87483cccc41f2d754a810811bf'
# Redump's Xbox serial is the XBE certificate's title ID: the publisher's two
# letters, then the low 16 bits in decimal (0x4D530004 is MS-004).
SERIAL = re.compile(r'([A-Z]{2})-([0-9]{3,5})')
# The certificate's region flags: North America, Japan, rest of the world.
NA, JAPAN, WORLD = 1, 2, 4
REGIONS = {'World': NA | JAPAN | WORLD, 'USA': NA, 'Canada': NA, 'Latin America': NA,
           'Japan': JAPAN}
REGIONS.update(dict.fromkeys(('Europe', 'UK', 'Scandinavia', 'Germany', 'Austria', 'Switzerland',
                              'France', 'Italy', 'Spain', 'Portugal', 'Netherlands', 'Sweden',
                              'Australia'), WORLD))
# Asian releases are not consistently one or the other; either flag finds them.
REGIONS.update(dict.fromkeys(('Asia', 'Korea', 'Taiwan', 'China', 'Hong Kong'), JAPAN | WORLD))
REGION = '|'.join(sorted(REGIONS, key=len, reverse=True))
GROUP = re.compile(rf' \(((?:{REGION})(?:, (?:{REGION}))*)\)')
# Neither the title ID nor the flags distinguish languages, revisions or packaging.
LABEL = re.compile(rf' \((?:(?:{REGION})(?:, (?:{REGION}))*|Rev [A-Za-z0-9.]+|'
                   r"[A-Z][a-z](?:,[A-Z][a-z])*|(?:Limited )?Collector's Edition|"
                   r'Online Enabled|Rerelease|GameStop|EB|ESRB RP-M)\)')


def flags(title):
    group = GROUP.search(title)
    if not group:
        return NA | JAPAN | WORLD
    value = 0
    for region in group[1].split(', '):
        value |= REGIONS[region]
    return value


def export(source):
    candidates = {}
    for kind, record in parse(source):
        if kind != 'game':
            continue
        names = [v for k, v in record if k == 'name']
        if len(names) != 1 or not isinstance(names[0], str):
            raise ValueError('expected one game name')
        title = names[0]
        if not title or any(ord(c) < 32 or ord(c) == 127 for c in title):
            raise ValueError('invalid game name')
        for field, rom in record:
            if field != 'rom':
                continue
            for key, value in rom:
                if key != 'serial':
                    continue
                match = SERIAL.fullmatch(value)
                if match and int(match[2]) <= 0xffff:
                    candidates.setdefault((match[1], int(match[2])), set()).add(title)
    titles, collisions = [], {}
    for (publisher, number), names in candidates.items():
        # The disc's flags select every release sold in one of its regions.
        for disc in range(1, 8):
            matching = sorted(name for name in names if flags(name) & disc)
            normalized = {LABEL.sub('', name) for name in matching}
            serial = f'X{chr(ord("A")+disc)}{publisher}-{number:05d}'
            if len(normalized) == 1:
                titles.append((serial, normalized.pop()))
            elif normalized:
                # Localized releases often share one title ID and region. Keep
                # the filename rather than choosing a name by input order.
                collisions[serial] = matching
    return ''.join(f'{s}\t{t}\n' for s, t in sorted(titles)).encode(), collisions


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--collisions', type=Path, required=True)
    args = p.parse_args()
    source = args.source.read_bytes()
    if args.source.suffix == '.xz':
        source = lzma.decompress(source)
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise SystemExit('Xbox DAT checksum differs from pinned source; update provenance explicitly')
    table, collisions = export(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(table)
    args.collisions.write_text(json.dumps(collisions, ensure_ascii=False, indent=2, sort_keys=True)+'\n')
    print(f'Xbox titles: {len(table.splitlines())} entries, {len(table)} bytes; '
          f'{len(collisions)} ambiguous IDs omitted; Libretro {REVISION}')


if __name__ == '__main__':
    main()
