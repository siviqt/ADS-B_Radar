"""Exercise production SD readers and rasterizer, including independently built corrupt blocks."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_atlas
import validate_atlas
from shapely.geometry import LineString

class AtlasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.folder=Path(cls.temp.name);cls.binary=cls.folder/'atlas'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'main'),'-I',str(ROOT/'managed_components/bblanchon__arduinojson/src'),str(ROOT/'tests/atlas.cpp'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def run_reader(self,path,mode):
        result=subprocess.run([str(self.binary),str(path),str(mode)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
        self.assertEqual(result.returncode,0,result.stdout)
        return result.stdout
    def test_worldwide_assets(self):
        print(self.run_reader(ROOT/'assets/world',self.folder))
    def test_all_records_and_coverage(self):
        audit=validate_atlas.validate(ROOT/'assets/world')
        self.assertTrue(audit['world']['full_domain_all_lods'])
        self.assertEqual(audit['world']['blocks'],7776)
        self.assertGreater(audit['world']['vertices'],2000000)
        self.assertEqual(audit['cities']['cities'],236278)
    def test_generator_discards_collapsed_lines(self):
        record,vertices=build_atlas.geometry_record(4,LineString([(1,1),(1.00000001,1)]))
        self.assertIsNone(record);self.assertEqual(vertices,0)
        record,vertices=build_atlas.geometry_record(4,LineString([(1,1),(1.01,1)]))
        self.assertIsNotNone(record);self.assertEqual(vertices,2)
    def test_corrupt_geometry(self):
        root=self.folder/'bad-world';root.mkdir()
        # The geometry header and index are well-formed, but the referenced block CRC is wrong.
        data=struct.pack('<I',0)
        entry=struct.pack('<iiiiIIII',-18000000,-9000000,18000000,9000000,0,len(data),zlib.crc32(data)^1,2)
        (root/'world-v2.idx').write_bytes(struct.pack('<8sIIIIQ',b'ADSBAT2\0',2,1,len(data),zlib.crc32(entry),1)+entry)
        (root/'world-v2-000.avp').write_bytes(struct.pack('<8sQ',b'ADSBG2\0\0',1)+data)
        self.run_reader(root,'corrupt')
    def test_city_record_crc(self):
        root=self.folder/'bad-city';root.mkdir()
        for n in ('world-v2.idx','world-v2-000.avp','cities-v1.idx','cities-v1.dat'):shutil.copyfile(ROOT/'assets/world'/n,root/n)
        idx=(root/'cities-v1.idx').read_bytes()
        for off in range(32,len(idx),80):
            if idx[off:off+64].split(b'\0')[0]==b'sofia' and idx[off+72:off+74]==b'BG':
                row=struct.unpack_from('<I',idx,off+64)[0];break
        else:self.fail('Sofia city row missing')
        with (root/'cities-v1.dat').open('r+b') as f:f.seek(16+row*120+16);f.write(b'!')
        self.run_reader(root,'bad-city')
