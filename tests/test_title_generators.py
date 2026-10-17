#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile
import json
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

class PSPGeneratorTests(unittest.TestCase):
    def test_pinned_export(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory)/'psp.tsv'; collisions=Path(directory)/'collisions.json'
            subprocess.run([sys.executable,str(root/'tools/gen_psp_titles.py'),
                str(root/'third_party/libretro/PlayStationPortable.dat'),str(out),
                '--collisions',str(collisions)],check=True,capture_output=True)
            self.assertEqual(out.read_bytes(),(root/'data/psp-titles.tsv').read_bytes())
            self.assertEqual(json.loads(collisions.read_text()),json.loads((root/'data/psp-title-collisions.json').read_text()))
            self.assertIn(b'UCUS-98646\tKillzone - Liberation (USA)',out.read_bytes())
            # Source substitutions must be explicit, including attribution/hash updates.
            source=Path(directory)/'changed.dat'; source.write_text('game ( name "Changed" )')
            result=subprocess.run([sys.executable,str(root/'tools/gen_psp_titles.py'),str(source),
                str(out),'--collisions',str(collisions)],capture_output=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn(b'checksum differs',result.stderr)
if __name__=='__main__': unittest.main()
