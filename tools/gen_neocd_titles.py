#!/usr/bin/env python3
"""Pinned Libretro/Redump Neo Geo CD DAT -> program-header ID<TAB>title."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import re
from gen_psx_titles import parse, REVISION

SOURCE_SHA256 = '265756750d6f54ff5ebf8622ad3ad582135e809c196ad6c58a4425e1a85784bb'
# Retail catalogue numbers differ from the program header for these publishers.
# Hardware IDs cross-checked against FBNeo's neocdlist_games.h; see NOTICE.
PROGRAM_IDS = {
    'ADCD-001': 0x0022, 'ADCD-002': 0x0037, 'ADCD-003': 0x0038,
    'ADCD-004': 0x0050, 'ADCD-005': 0x0053, 'ADCD-006': 0x0057,
    'ADCD-007': 0x0064, 'ADCD-008': 0x0074, 'ADCD-009': 0x0090,
    'ADCD-010': 0x0203, 'ADCD-011': 0x0212, 'ADCD-012': 0x0217,
    'ADCD-013': 0x0224, 'ADCD-100': 0x0005, 'ADCD-101': 0x0009,
    'ADCD-102': 0x0054, 'ADCD-103': 0x0204, 'ADCD-104': 0x0211,
    'DECD-002': 0x0065, 'DECD-003': 0x0066, 'DECD-004': 0x0079,
    'AICD-048': 0x0048, 'AICD-051': 0x0051,
    'NGCD-069': 0x069C,
    # Treasure of the Caribbean reuses Janshin's program ID. Keep both
    # candidates so the collision is omitted, rather than mislabelling it.
    'NGCD-500': 0x0048,
}
SERIAL = re.compile(r'([A-Z]{4}-[0-9]{3})(?:[01E])?')
# A program ID does not distinguish retail regions, languages or revisions.
LABEL = re.compile(r' \((?:World|Japan|USA|Europe|Export|France|Germany|Japan, USA|'
                   r'Rev [A-Za-z0-9.]+|[A-Z][a-z](?:,[A-Z][a-z])*)\)')


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
                if not match:
                    continue
                retail = match[1]
                game_id = PROGRAM_IDS.get(retail)
                if game_id is None and retail.startswith('NGCD-'):
                    # Commercial SNK IDs use BCD. Unknown/homebrew catalogue
                    # numbers must not be guessed into hardware identities.
                    number = int(retail[5:])
                    if 0 < number <= 251:
                        game_id = int(retail[5:], 16)
                if game_id is not None:
                    candidates.setdefault(f'NGCD-{game_id:05X}', set()).add(title)
    titles, collisions = [], {}
    for serial, names in sorted(candidates.items()):
        normalized = {LABEL.sub('', name) for name in names}
        if len(normalized) == 1:
            titles.append((serial, normalized.pop()))
        else:
            collisions[serial] = sorted(names)
    return ''.join(f'{s}\t{t}\n' for s, t in titles).encode(), collisions


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
        raise SystemExit('Neo Geo CD DAT checksum differs from pinned source; update provenance explicitly')
    table, collisions = export(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(table)
    args.collisions.write_text(json.dumps(collisions, ensure_ascii=False, indent=2, sort_keys=True)+'\n')
    print(f'Neo Geo CD titles: {len(table.splitlines())} entries, {len(table)} bytes; '
          f'{len(collisions)} ambiguous IDs omitted; Libretro {REVISION}')


if __name__ == '__main__':
    main()
