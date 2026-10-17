#!/usr/bin/env python3
"""Pinned Libretro/Redump PS1 DAT -> sorted serial<TAB>title, offline at build time."""
import argparse
import hashlib
import json
from pathlib import Path
import re

REVISION = '12c21cf79f1e20eff6ac68b80875816c717d0a7a'
SOURCE_SHA256 = '52978215854d307bfb5353052c9ce3c34995310a970d586f0100bb67f7de01a8'
TOKEN = re.compile(r'\s+|[()]|"(?:\\.|[^"\\])*"|[^\s()"]+')
SERIAL = re.compile(r'(?<![A-Z0-9])[A-Z]{4}-[0-9]{5}(?![A-Z0-9])')
REVISION_LABEL = re.compile(r' \((?:Rev [A-Za-z0-9.]+|[vV][0-9][A-Za-z0-9.]*)\)')

def parse(source):
    text = source.decode('utf-8') if isinstance(source, bytes) else source
    tokens, at = [], 0
    for match in TOKEN.finditer(text):
        if match.start() != at:
            raise ValueError('invalid DAT token')
        at = match.end()
        if not match.group().isspace(): tokens.append(match.group())
    if at != len(text): raise ValueError('trailing DAT garbage')
    at = 0
    def block(depth=0):
        nonlocal at
        if depth > 8: raise ValueError('DAT nesting limit')
        out = []
        while at < len(tokens) and tokens[at] != ')':
            key = tokens[at]; at += 1
            if key in ('(', ')') or at == len(tokens): raise ValueError('invalid DAT pair')
            value = tokens[at]; at += 1
            if value == '(':
                value = block(depth+1)
                if at == len(tokens) or tokens[at] != ')': raise ValueError('unclosed DAT block')
                at += 1
            elif value.startswith('"'):
                # DAT uses quoted strings; these source escapes follow JSON spelling.
                value = json.loads(value)
            elif value == ')': raise ValueError('missing DAT value')
            out.append((key,value))
        return out
    out = block()
    if at != len(tokens): raise ValueError('unexpected closing parenthesis')
    return out

def export(source):
    candidates = {}
    for kind, record in parse(source):
        if kind != 'game': continue
        names = [v for k,v in record if k == 'name']
        if len(names) != 1 or not isinstance(names[0],str): raise ValueError('expected one game name')
        title = names[0]
        if not title or any(ord(c)<32 or ord(c)==127 for c in title): raise ValueError('invalid game name')
        for field, rom in record:
            if field != 'rom': continue
            for key,value in rom:
                if key == 'serial':
                    for serial in SERIAL.findall(value): candidates.setdefault(serial,set()).add(title)
    titles, collisions = [], {}
    for serial,names in sorted(candidates.items()):
        normalized = {REVISION_LABEL.sub('',name) for name in names}
        if len(normalized) == 1:
            titles.append((serial,normalized.pop()))
        else:
            # A serial alone cannot choose between different names/editions.
            # Omit ambiguous keys, rather than assigning a record by input order.
            collisions[serial] = sorted(names)
    table = ''.join(f'{s}\t{t}\n' for s,t in titles).encode('utf-8')
    return table,collisions

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path); p.add_argument('output',type=Path)
    p.add_argument('--collisions',type=Path,required=True)
    args=p.parse_args(); source=args.source.read_bytes()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise SystemExit('PS1 DAT checksum differs from pinned source; update provenance explicitly')
    table,collisions=export(source)
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_bytes(table)
    args.collisions.write_text(json.dumps(collisions,ensure_ascii=False,indent=2,sort_keys=True)+'\n')
    print(f'PS1 titles: {len(table.splitlines())} entries, {len(table)} bytes; {len(collisions)} ambiguous serials omitted; Libretro {REVISION}')
if __name__=='__main__': main()
