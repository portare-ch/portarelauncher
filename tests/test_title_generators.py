#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile
import json
import lzma
spec=importlib.util.spec_from_file_location('generator',Path(__file__).resolve().parents[1]/'tools/gen_ps2_titles.py')
g=importlib.util.module_from_spec(spec); spec.loader.exec_module(g)
class GeneratorTests(unittest.TestCase):
    def test_names_and_sort(self):
        source='''SLUS-20946:
  name: "Grand Theft Auto - San Andreas"
  name-en: "ignored"
SLES-12345:
  name: 'ゲーム [Disc 2]'
not-a-serial:
  name: ignored
'''
        self.assertEqual(g.export(source),'SLES-12345\tゲーム [Disc 2]\nSLUS-20946\tGrand Theft Auto - San Andreas\n'.encode())
    def test_duplicates_and_invalid(self):
        for source in ('SLUS-20946: {name: A}\nSLUS-20946: {name: B}',
                       'SLUS-20946: {name: A, name: B}', 'SLUS-20946: {name: "A\\tB"}',
                       'SLUS-20946: {region: PAL}'):
            with self.assertRaises(ValueError): g.export(source)
    def test_unrelated_merges(self):
        self.assertEqual(g.export('SLUS-20946:\n  name: A\n  settings: &a {x: 1}\n  other: {<<: *a}\n'),b'SLUS-20946\tA\n')

psx_spec=importlib.util.spec_from_file_location('psx_generator',Path(__file__).resolve().parents[1]/'tools/gen_psx_titles.py')
psx=importlib.util.module_from_spec(psx_spec); psx_spec.loader.exec_module(psx)
class PS1GeneratorTests(unittest.TestCase):
    def test_names_and_serials(self):
        table,collisions=psx.export('game ( name "Lomax (USA)" rom ( serial "SCUS-94906" ) )')
        self.assertEqual(table,b'SCUS-94906\tLomax (USA)\n'); self.assertEqual(collisions,{})
    def test_revision_collapse_and_ambiguity(self):
        a='game ( name "Game (USA) (Rev 1)" rom ( serial "SCUS-94906" ) )'
        b='game ( name "Game (USA) (Rev 2)" rom ( serial "SCUS-94906" ) )'
        self.assertEqual(psx.export(a+b),(b'SCUS-94906\tGame (USA)\n',{}))
        c='game ( name "Other game (Disc 2)" rom ( serial "SCUS-94906" ) )'
        self.assertEqual(psx.export(a+c),psx.export(c+a))
        table,collisions=psx.export(a+c)
        self.assertEqual(table,b''); self.assertEqual(len(collisions['SCUS-94906']),2)
    def test_malformed(self):
        for source in ('game ( name "A"', 'game ( name "A" name "B" )', 'game ( name "A" ) )'):
            with self.assertRaises(ValueError): psx.export(source)

class PinnedGeneratorTests(unittest.TestCase):
    def test_pinned_exports(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            for system,source_path in (
                ('ps2','pcsx2/GameIndex.yaml.xz'),
                ('psx','libretro/PlayStation.dat.xz'),
                ('psp','libretro/PlayStationPortable.dat.xz'),
            ):
                with self.subTest(system=system):
                    out=Path(directory)/f'{system}.tsv'; collisions=Path(directory)/'collisions.json'
                    command=[sys.executable,str(root/f'tools/gen_{system}_titles.py')]
                    extra=['--collisions',str(collisions)] if system != 'ps2' else []
                    subprocess.run(command+[str(root/'third_party'/source_path),str(out)]+extra,
                                   check=True,capture_output=True)
                    self.assertEqual(out.read_bytes(),(root/f'data/{system}-titles.tsv').read_bytes())
                    if extra:
                        self.assertEqual(json.loads(collisions.read_text()),
                                         json.loads((root/f'data/{system}-title-collisions.json').read_text()))
                    # Check the decompressed source hash, rather than accepting
                    # a substituted catalogue inside a valid XZ container.
                    source=Path(directory)/'changed.xz'; source.write_bytes(lzma.compress(b'changed'))
                    result=subprocess.run(command+[str(source),str(out)]+extra,capture_output=True)
                    self.assertNotEqual(result.returncode,0)
                    self.assertIn(b'checksum differs',result.stderr)
if __name__=='__main__': unittest.main()
