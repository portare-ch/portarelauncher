#!/usr/bin/env python3
"""Export only standard serial keys and their name fields from pinned YAML."""
import argparse
import hashlib
import lzma
import re
from pathlib import Path
import yaml

REVISION = "091ab68708a555198d80a7216965481c7ddd0d98"
SOURCE_SHA256 = "6589964b54348a4af845bb33311375fa1596c55864be750e9fbdc69c8d7dcdcd"

def export(source):
    # Compose YAML nodes: unrelated emulator settings use merge keys and anchors.
    # Inspect only top-level serial keys and each mapping's literal name field.
    root = yaml.compose(source, Loader=yaml.SafeLoader)
    if not isinstance(root, yaml.MappingNode):
        raise ValueError("expected a YAML mapping")
    titles, seen = [], set()
    for key, entry in root.value:
        serial = key.value
        if serial in seen:
            raise ValueError(f"duplicate serial key: {serial}")
        seen.add(serial)
        if not re.fullmatch(r"[A-Z]{4}-[0-9]{5}", serial):
            continue
        if not isinstance(entry, yaml.MappingNode):
            raise ValueError(f"invalid mapping for {serial}")
        names = [value for field, value in entry.value if field.value == "name"]
        if len(names) != 1 or not isinstance(names[0], yaml.ScalarNode) or names[0].tag != "tag:yaml.org,2002:str":
            raise ValueError(f"expected one name for {serial}")
        title = names[0].value
        if not title or any(ord(c) < 32 or ord(c) == 127 for c in title):
            raise ValueError(f"invalid title for {serial}")
        titles.append((serial, title))
    return "".join(f"{serial}\t{title}\n" for serial, title in sorted(titles)).encode("utf-8")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    source = args.source.read_bytes()
    if args.source.suffix == '.xz':
        source = lzma.decompress(source)
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise SystemExit("GameIndex checksum differs from pinned source; update provenance explicitly")
    output = export(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"PS2 titles: {len(output.splitlines())} entries, {len(output)} bytes; PCSX2 {REVISION}")

if __name__ == "__main__":
    main()
