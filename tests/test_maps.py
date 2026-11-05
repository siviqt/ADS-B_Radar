from pathlib import Path
import importlib.util
import json
import subprocess
import tempfile
import unittest
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('maps',ROOT/'tools/generate_map_pack.py')
maps=importlib.util.module_from_spec(spec);spec.loader.exec_module(maps)

class MapTests(unittest.TestCase):
    def test_projection(self):
        lat,lon=42.7,23.3
        for radius in maps.RANGES:
            for x,y in [(0,0),(400,209),(799,417),(241,97)]:
                a,b=maps.coordinate(x,y,lat,lon,radius)
                self.assertAlmostEqual(400+(b-lon)*60*__import__('math').cos(__import__('math').radians(lat))*400/radius,x)
                self.assertAlmostEqual(209-(a-lat)*60*400/radius,y)
    def test_pack_and_failures(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);tiles=root/'tiles/0/0';tiles.mkdir(parents=True)
            Image.new('RGB',(256,256),(255,0,0)).save(tiles/'0.png')
            output=root/'app/maps'
            maps.generate(0,0,[1],root/'tiles',0,output,'Synthetic test tiles')
            first={p.name:p.read_bytes() for p in output.iterdir()}
            maps.generate(0,0,[1],root/'tiles',0,output,'Synthetic test tiles')
            self.assertEqual(first,{p.name:p.read_bytes() for p in output.iterdir()})
            binary=root/'test'
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'main'),'-I',str(ROOT/'managed_components/bblanchon__arduinojson/src'),str(ROOT/'tests/map_pack.cpp'),'-o',str(binary)],check=True)
            def check(state):subprocess.run([str(binary),str(root/'app'),state],check=True)
            check('valid')
            pixels=output/'1.rgb565';data=pixels.read_bytes()
            pixels.write_bytes(data[:-1]);check('invalid')
            pixels.write_bytes(b'X'+data[1:]);check('invalid')
            pixels.write_bytes(data+b'X');check('invalid')
            pixels.write_bytes(data)
            manifest=output/'manifest.json';original=json.loads(manifest.read_text())
            for key,value in [('version',2),('width',801),('projection','mercator'),('center_lat',1),('attribution','')]:
                d=dict(original);d[key]=value;manifest.write_text(json.dumps(d));check('invalid')
            manifest.write_text(' '*8193);check('invalid')
            manifest.write_text('{');check('invalid')
            manifest.unlink();check('invalid')
