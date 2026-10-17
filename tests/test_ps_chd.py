#!/usr/bin/env python3
"""Synthetic ISO9660 in real uncompressed CHDv5 containers. No copyrighted discs."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HELPER = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else str(Path('ps-chd-id').resolve())

def both32(n):
    return struct.pack('<I', n) + struct.pack('>I', n)

def entry(name, lba, size, flags=0):
    name = name.encode('ascii') if isinstance(name, str) else name
    n = 33 + len(name)
    n += n % 2
    r = bytearray(n)
    r[0] = n
    r[2:10] = both32(lba)
    r[10:18] = both32(size)
    r[25] = flags
    r[28:32] = b'\x01\0\0\x01'
    r[32] = len(name)
    r[33:33+len(name)] = name
    return r

def iso(config=b'BOOT2 = cdrom0:\\SLUS_209.46;1\r\n', name='SYSTEM.CNF;1'):
    data = bytearray(64*2048)
    pvd = bytearray(2048)
    pvd[:7] = b'\x01CD001\x01'
    pvd[80:88] = both32(64)
    pvd[128:132] = b'\0\x08\x08\0'
    pvd[156:190] = entry(b'\0', 20, 4096, 2)
    data[16*2048:17*2048] = pvd
    data[17*2048:17*2048+7] = b'\xffCD001\x01'
    # Deliberately put the file in the second root-directory sector.
    r = entry(name, 24, len(config))
    data[21*2048:21*2048+len(r)] = r
    data[24*2048:24*2048+len(config)] = config
    return data

def sfo(fields=None):
    fields = fields if fields is not None else [
        ('TITLE', 0x0204, b'Killzone: Liberation\0'),
        ('DISC_ID', 0x0204, b'UCUS98646\0'),
        ('DISC_NUMBER', 0x0404, struct.pack('<I', 1)),
    ]
    keys, values, indices = bytearray(), bytearray(), bytearray()
    for name, kind, value in fields:
        cap = (len(value)+3)&~3
        indices.extend(struct.pack('<HHIII',len(keys),kind,len(value),cap,len(values)))
        keys.extend(name.encode()+b'\0')
        values.extend(value+bytes(cap-len(value)))
    key_start = 20+len(indices)
    data_start = (key_start+len(keys)+3)&~3
    return bytearray(struct.pack('<4s4I',b'\0PSF',0x101,key_start,data_start,len(fields)) +
                     indices+keys+bytes(data_start-key_start-len(keys))+values)

def psp_iso(param=None, game_name='PSP_GAME', sfo_name='PARAM.SFO;1'):
    param = sfo() if param is None else param
    data = iso(b'', 'IGNORED;1')
    data[21*2048:22*2048] = bytes(2048)
    r = entry(game_name,24,4096,2)
    data[21*2048:21*2048+len(r)] = r
    # PARAM.SFO is in the second directory sector; metadata can span hunks.
    r = entry(sfo_name,28,len(param))
    data[25*2048:25*2048+len(r)] = r
    data[28*2048:28*2048+len(param)] = param
    return data

def cdframes(data, mode):
    frames = bytearray()
    for at in range(0, len(data), 2048):
        r = bytearray(2448)
        if mode == 'MODE1':
            off = 0
        elif mode == 'MODE1_RAW':
            r[:12] = b'\0' + b'\xff'*10 + b'\0'
            r[15] = 1
            off = 16
        elif mode == 'MODE2_RAW':
            r[:12] = b'\0' + b'\xff'*10 + b'\0'
            r[15] = 2
            off = 24
        elif mode in ('MODE2', 'MODE2_FORM_MIX'):
            off = 8
        else:
            raise ValueError(mode)
        r[off:off+2048] = data[at:at+2048]
        frames.extend(r)
    return frames

def chd(data, unit=2048, metadata=(), parent=False, hunk=None):
    hunk = hunk or unit*4
    count = (len(data)+hunk-1)//hunk
    mapend = 124 + count*4
    meta = bytearray()
    for i, (tag, text) in enumerate(metadata):
        payload = text.encode()+b'\0'
        nextpos = mapend+len(meta)+16+len(payload) if i+1 < len(metadata) else 0
        meta.extend(tag.encode()+struct.pack('>IQ', len(payload), nextpos)+payload)
    first = (mapend+len(meta)+hunk-1)//hunk
    header = bytearray(124)
    header[:8] = b'MComprHD'
    struct.pack_into('>II', header, 8, 124, 5)
    struct.pack_into('>QQQII', header, 32, len(data), 124, mapend if meta else 0, hunk, unit)
    if parent:
        header[104:124] = b'\x01'*20
    result = header + b''.join(struct.pack('>I', first+i) for i in range(count)) + meta
    result.extend(b'\0'*(first*hunk-len(result)))
    result.extend(data)
    result.extend(b'\0'*(count*hunk-len(data)))
    return result

def track(num, mode, frames, pregap=0, stored=False, modern=True):
    text = f'TRACK:{num} TYPE:{mode} SUBTYPE:NONE FRAMES:{frames}'
    if modern:
        text += f' PREGAP:{pregap} PGTYPE:{"V" if stored else ""}{mode} PGSUB:NONE POSTGAP:0'
    return ('CHT2' if modern else 'CHTR', text)

class HelperTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='ps-chd-test-')
        self.path = Path(self.tmp.name)/"arbitrary ' $(literal).ChD"
    def tearDown(self):
        self.tmp.cleanup()
    def run_image(self, data, status=0, serial=b'SLUS-20946\n'):
        self.path.write_bytes(data)
        r = subprocess.run([HELPER, str(self.path)], capture_output=True, timeout=5)
        self.assertEqual(r.returncode, status, r.stderr.decode(errors='replace'))
        self.assertEqual(r.stdout, serial if status == 0 else b'')
    def test_dvd(self):
        self.run_image(chd(iso()))
        # A hunk boundary inside the PVD and directory is also valid.
        self.run_image(chd(iso(), hunk=3072))
    def test_cd_wrapped(self):
        for mode in ('MODE1', 'MODE1_RAW', 'MODE2_RAW', 'MODE2', 'MODE2_FORM_MIX'):
            with self.subTest(mode=mode):
                self.run_image(chd(cdframes(iso(), mode), 2448, [track(1, mode, 64)]))
    def test_track_offsets_and_pregaps(self):
        for modern, stored, pregap in ((True, True, 150), (True, False, 150), (False, False, 0)):
            with self.subTest(modern=modern, stored=stored):
                # Seven audio frames are padded to eight; index 00 is present only for stored pregaps.
                prefix = bytes(8*2448)
                gap = bytes(pregap*2448) if stored else b''
                data = prefix + gap + cdframes(iso(), 'MODE1_RAW')
                frames = 64+(pregap if stored else 0)
                # CHD pads the final track as well.
                data += bytes((-frames % 4)*2448)
                self.run_image(chd(data,2448,[track(1,'AUDIO',7,modern=modern),
                    track(2,'MODE1_RAW',frames,pregap,stored,modern)]))
    def test_boot_variations(self):
        for cnf in (b' boot2\t=\tcdrom0:/DIR/slus_209.46;123 \n', b'BOOT2=SLUS-20946\r',
                    b'BOOT2=slus_20946;1', b'BOOT2=SLUS_209.46;1\nVER=1.0\n'):
            self.run_image(chd(iso(cnf, 'system.cnf;123')))
    def test_ps1_boot(self):
        for cnf in (b'BOOT = cdrom:\\SLUS_209.46;1\r\n', b' boot\t= cdrom:/dir/slus_209.46;123 \n'):
            self.run_image(chd(cdframes(iso(cnf),'MODE2_RAW'),2448,[track(1,'MODE2_RAW',64)]))
        self.run_image(chd(iso(b'BOOT = SCUS_949.06;1\nBOOT2 = SLUS_209.46;1')),0)
        self.run_image(chd(iso(b'BOOT = SLUS_209.46;1\nBOOT2 = invalid.elf;1')),1)
    def test_ps1_volume_includes_audio(self):
        data=iso(b'BOOT = cdrom:\\SLUS_209.46;1')
        data[16*2048+80:16*2048+88]=both32(128)
        frames=cdframes(data,'MODE2_RAW')+bytes(64*2448)
        self.run_image(chd(frames,2448,[track(1,'MODE2_RAW',64),track(2,'AUDIO',64)]))
        # A directory extent in an audio track is never read as ISO data.
        data[16*2048+158:16*2048+166]=both32(80)
        self.run_image(chd(cdframes(data,'MODE2_RAW')+bytes(64*2448),2448,
                           [track(1,'MODE2_RAW',64),track(2,'AUDIO',64)]),1)
    def test_psp(self):
        self.run_image(chd(psp_iso()), serial=b'UCUS-98646\n')
        for mode in ('MODE1', 'MODE1_RAW', 'MODE2_RAW'):
            self.run_image(chd(cdframes(psp_iso(),mode),2448,[track(1,mode,64)]),serial=b'UCUS-98646\n')
        param = sfo([('TITLE',0x0204,b'x'*3000+b'\0'),('DISC_ID',0x0204,b'ucus-98646\0')])
        self.run_image(chd(psp_iso(param,'psp_game;1','param.sfo;123'),hunk=3072),serial=b'UCUS-98646\n')
        frames = bytes(8*2448)+bytes(150*2448)+cdframes(psp_iso(),'MODE1_RAW')+bytes(2*2448)
        self.run_image(chd(frames,2448,[track(1,'AUDIO',7),track(2,'MODE1_RAW',214,150,True)]),serial=b'UCUS-98646\n')
    def test_psp_invalid_sfo(self):
        invalid = [b'\0PSF',sfo([('TITLE',0x0204,b'UCUS98646\0')]),
            sfo([('DISC_ID',0x0404,b'UCUS98646\0')]),
            sfo([('DISC_ID',0x0204,b'UCUS98646')]),
            sfo([('DISC_ID',0x0204,b'UCUS98X46\0')]),
            sfo([('DISC_ID',0x0204,b'UCUS98646\0junk\0')]),
            sfo([('DISC_ID',0x0204,b'UCUS98646\0'),('DISC_ID',0x0204,b'UCES00279\0')])]
        # Header/table/key/value offsets must all fit before dereferencing anything.
        for offset,fmt,value in ((0,'I',0),(4,'I',0x102),(8,'I',1),(12,'I',0xffffffff),
                                 (16,'I',257),(20,'H',65535),(24,'I',0xffffffff),
                                 (28,'I',0xffffffff),(32,'I',0xffffffff)):
            param = sfo(); struct.pack_into('<'+fmt,param,offset,value); invalid.append(param)
        param=sfo(); keys,data=struct.unpack_from('<II',param,8)
        param[keys:data]=b'x'*(data-keys); invalid.append(param)
        for param in invalid:
            with self.subTest(param=param[:40]): self.run_image(chd(psp_iso(param)),1)
    def test_psp_directory_bounds(self):
        self.run_image(chd(psp_iso(sfo()+bytes(65536))),1)
        self.run_image(chd(psp_iso(sfo_name='PARAM.SFO;bad')),1)
        self.run_image(chd(psp_iso(game_name='OTHER')),1)
        data=psp_iso(); data[21*2048+25]=0  # PSP_GAME must be a directory.
        self.run_image(chd(data),1)
        data=psp_iso(); data[21*2048+10:21*2048+18]=both32(2*1024*1024)
        self.run_image(chd(data),1)
        data=psp_iso(); data[25*2048+2:25*2048+10]=both32(10000)
        self.run_image(chd(data),1)
        data=psp_iso(); data[25*2048+25]=0x80  # Multi-extent files are unsupported.
        self.run_image(chd(data),1)
        data=psp_iso(); first=data[21*2048]
        r=entry('SYSTEM.CNF;1',30,30); data[21*2048+first:21*2048+first+len(r)]=r
        self.run_image(chd(data),1)  # Mixed PS1/PS2 and PSP layouts are ambiguous.
    def test_invalid(self):
        for cnf in (b'BOOTX=SLUS_209.46;1', b'BOOT2=homebrew.elf;1', b'BOOT2=SLUS_20.946;1',
                    b'BOOT2=SLUS_209.46;A', b'random SLUS_209.46', b'BOOT2=SLUS_209.46\0'):
            self.run_image(chd(iso(cnf)), 1)
        self.run_image(chd(iso(name='OTHER.CNF;1')), 1)
        self.run_image(chd(iso(),parent=True), 1)
        self.run_image(b'truncated', 1)
        self.run_image(chd(iso())[:4000], 1)
        self.run_image(chd(bytes(64*2048)), 1)
        self.run_image(chd(iso(), unit=512), 1)
    def test_bounds(self):
        data=iso()
        data[16*2048+156+10:16*2048+156+18]=both32(2*1024*1024)
        self.run_image(chd(data),1)
        data=iso()
        data[21*2048+10:21*2048+18]=both32(32769)
        self.run_image(chd(data),1)
        data=iso()
        data[21*2048+2:21*2048+10]=both32(10000)
        self.run_image(chd(data),1)
        data=iso(); data[21*2048]=255
        self.run_image(chd(data),1)
        raw=cdframes(iso(),'MODE2_RAW')
        raw[16*2448+18]=raw[16*2448+22]=0x20
        self.run_image(chd(raw,2448,[track(1,'MODE2_RAW',64)]),1)
        self.run_image(chd(cdframes(iso(),'MODE1_RAW'),2448,[('CHT2','TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:999999')]),1)
    def test_operational(self):
        r=subprocess.run([HELPER,str(self.path)],capture_output=True)
        self.assertEqual(r.returncode,2)
        r=subprocess.run([HELPER,'relative.chd'],capture_output=True)
        self.assertEqual(r.returncode,2)
    @unittest.skipUnless(shutil.which('chdman'), 'optional chdman compression integration')
    def test_compressed_chdman(self):
        raw=Path(self.tmp.name)/'disc.iso'; raw.write_bytes(iso())
        # createraw also covers older chdman releases predating createdvd.
        subprocess.run(['chdman','createraw','-i',str(raw),'-o',str(self.path),
                        '-hs','8192','-us','2048','-c','lzma,zlib,huff'],check=True,capture_output=True)
        r=subprocess.run([HELPER,str(self.path)],capture_output=True,timeout=5)
        self.assertEqual((r.returncode,r.stdout),(0,b'SLUS-20946\n'),r.stderr)
    @unittest.skipUnless(shutil.which('chdman'), 'optional chdman compression integration')
    def test_compressed_psp_chdman(self):
        raw=Path(self.tmp.name)/'psp.iso'; raw.write_bytes(psp_iso())
        subprocess.run(['chdman','createraw','-i',str(raw),'-o',str(self.path),
                        '-hs','8192','-us','2048','-c','lzma,zlib,huff'],check=True,capture_output=True)
        r=subprocess.run([HELPER,str(self.path)],capture_output=True,timeout=5)
        self.assertEqual((r.returncode,r.stdout),(0,b'UCUS-98646\n'),r.stderr)
    @unittest.skipUnless(shutil.which('chdman'), 'optional chdman compression integration')
    def test_compressed_cd_chdman(self):
        for stored in (False, True):
            with self.subTest(stored=stored):
                raw = cdframes(iso(), 'MODE1_RAW')
                # Drop CHD subcode slots to make an ordinary 2352-byte BIN file.
                data = b''.join(raw[i:i+2352] for i in range(0,len(raw),2448))
                disc = bytes(7*2352) + (bytes(150*2352) if stored else b'') + data
                folder = Path(self.tmp.name)
                (folder/'disc.bin').write_bytes(disc)
                gap = '    INDEX 00 00:00:07\n    INDEX 01 00:02:07\n' if stored else '    PREGAP 00:02:00\n    INDEX 01 00:00:07\n'
                (folder/'disc.cue').write_text('FILE "disc.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n  TRACK 02 MODE1/2352\n'+gap)
                if self.path.exists(): self.path.unlink()
                subprocess.run(['chdman','createcd','-i',str(folder/'disc.cue'),'-o',str(self.path)],check=True,capture_output=True)
                r=subprocess.run([HELPER,str(self.path)],capture_output=True,timeout=5)
                self.assertEqual((r.returncode,r.stdout),(0,b'SLUS-20946\n'),r.stderr)

if __name__=='__main__':
    unittest.main()
