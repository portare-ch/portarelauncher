#!/usr/bin/env python3
"""Pinned Libretro/Redump PSP DAT -> offline serial/title table."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
from gen_psx_titles import REVISION, export

SOURCE_SHA256 = '9e7ad38dd1c53720b27914ca4379a3bd6eddfd5519370b592e8131c886ede313'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path); p.add_argument('output',type=Path)
    p.add_argument('--collisions',type=Path,required=True)
    args = p.parse_args(); source = args.source.read_bytes()
    if args.source.suffix == '.xz':
        source = lzma.decompress(source)
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise SystemExit('PSP DAT checksum differs from pinned source; update provenance explicitly')
    table,collisions = export(source)
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_bytes(table)
    args.collisions.write_text(json.dumps(collisions,ensure_ascii=False,indent=2,sort_keys=True)+'\n')
    print(f'PSP titles: {len(table.splitlines())} entries, {len(table)} bytes; {len(collisions)} ambiguous serials omitted; Libretro {REVISION}')
if __name__=='__main__': main()
