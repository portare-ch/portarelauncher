#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile
import json
import lzma
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
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

neocd_spec=importlib.util.spec_from_file_location('neocd_generator',Path(__file__).resolve().parents[1]/'tools/gen_neocd_titles.py')
neocd=importlib.util.module_from_spec(neocd_spec); neocd_spec.loader.exec_module(neocd)
class NeoCDGeneratorTests(unittest.TestCase):
    def test_program_ids_and_retail_variants(self):
        source='''game ( name "Metal Slug 2 (World) (En,Ja)" rom ( serial "NGCD-2410" ) )
game ( name "Metal Slug 2 (World) (En,Ja)" rom ( serial "NGCD-2411" ) )
game ( name "ADK World (Japan)" rom ( serial "ADCD-103" ) )
game ( name "Fatal Fury 3 (Export) (Rev 2)" rom ( serial "NGCD-069E" ) )
game ( name "Fatal Fury 3 (Japan)" rom ( serial "NGCD-069" ) )'''
        self.assertEqual(neocd.export(source),(b'NGCD-00204\tADK World\nNGCD-00241\tMetal Slug 2\nNGCD-0069C\tFatal Fury 3\n',{}))
    def test_ambiguity_and_unknown_catalogue_numbers(self):
        a='game ( name "Game A" rom ( serial "NGCD-2410" ) )'
        b='game ( name "Game B" rom ( serial "NGCD-2411" ) )'
        self.assertEqual(neocd.export(a+b),neocd.export(b+a))
        self.assertEqual(neocd.export(a+b)[0],b'')
        self.assertEqual(neocd.export('game ( name "Unknown" rom ( serial "NGCD-999" ) )'),(b'',{}))
        reused='game ( name "Janshin" rom ( serial "AICD-048" ) )game ( name "Treasure" rom ( serial "NGCD-500" ) )'
        self.assertEqual(neocd.export(reused),(b'',{'NGCD-00048':['Janshin','Treasure']}))

xbox_spec=importlib.util.spec_from_file_location('xbox_generator',Path(__file__).resolve().parents[1]/'tools/gen_xbox_titles.py')
xbox=importlib.util.module_from_spec(xbox_spec); xbox_spec.loader.exec_module(xbox)
class XboxGeneratorTests(unittest.TestCase):
    def test_region_flags(self):
        source='''game ( name "Halo - Combat Evolved (USA) (Rev 2)" rom ( serial "MS-004" ) )
game ( name "Halo - Combat Evolved (Europe) (Fr,De)" rom ( serial "MS-004" ) )
game ( name "Halo (Japan)" rom ( serial "MS-004" ) )
game ( name "Halo Movies (Japan)" rom ( serial "MS-004" ) )'''
        table,collisions=xbox.export(source)
        # B, E and F: North America, the rest of the world, or both.
        self.assertEqual(table,b'XBMS-00004\tHalo - Combat Evolved\nXEMS-00004\tHalo - Combat Evolved\n'
                               b'XFMS-00004\tHalo - Combat Evolved\n')
        self.assertEqual(sorted(collisions),['XCMS-00004','XDMS-00004','XGMS-00004','XHMS-00004'])
        self.assertEqual(collisions['XCMS-00004'],['Halo (Japan)','Halo Movies (Japan)'])
        self.assertEqual(xbox.export('\n'.join(reversed(source.splitlines()))),(table,collisions))
    def test_labels_and_serials(self):
        source='''game ( name "Doom 3 (USA) (Limited Collector's Edition)" rom ( serial "AV-032" ) )
game ( name "Doom 3 (Europe, Australia) (En,Fr,De)" rom ( serial "AV-032" ) )
game ( name "Game (Korea) (En,Ko)" rom ( serial "XX-1234" ) )
game ( name "Show (Japan) (Disc 1) (Taikenban)" rom ( serial "XK-030-0" ) )
game ( name "Show (Japan) (Disc 1) (Taikenban)" rom ( serial "XK-030" ) )
game ( name "Recovery (Build 5659) (USA)" rom ( serial "MS-999" ) )
game ( name "Too big (USA)" rom ( serial "MS-65536" ) )'''
        rows=dict(line.split('\t') for line in xbox.export(source)[0].decode().splitlines())
        self.assertEqual({rows[k] for k in rows if 'AV-' in k},{'Doom 3'})
        # Neither release was sold in Japan, so a Japan-only disc finds nothing.
        self.assertEqual(sorted(k[1] for k in rows if 'AV-' in k),list('BDEFGH'))
        # Asian releases match either the Japan or the rest-of-world flag.
        self.assertNotIn('XBXX-01234',rows)
        self.assertEqual(rows['XCXX-01234'],'Game'); self.assertEqual(rows['XEXX-01234'],'Game')
        self.assertEqual(rows['XCXK-00030'],'Show (Disc 1) (Taikenban)')
        self.assertEqual(rows['XBMS-00999'],'Recovery (Build 5659)')
        self.assertFalse([k for k in rows if k.endswith('65536')])

class PinnedGeneratorTests(unittest.TestCase):
    def test_pinned_exports(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            for system,source_path in (
                ('ps2','pcsx2/GameIndex.yaml.xz'),
                ('psx','libretro/PlayStation.dat.xz'),
                ('psp','libretro/PlayStationPortable.dat.xz'),
                ('neocd','libretro/NeoGeoCD.dat.xz'),
                ('xbox','libretro/Xbox.dat.xz'),
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
