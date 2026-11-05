from pathlib import Path
import importlib.util
import json
import hashlib
import zlib
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('package',ROOT/'tools/package.py')
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
SCHEMA=package.DEFAULT_SCHEMA

class PackageTests(unittest.TestCase):
    def test_assembly_records_resolved_bsp_major(self):
        with tempfile.TemporaryDirectory() as tmp:
            port=Path(tmp)/'repo';idf=Path(tmp)/'idf'
            for directory in (port/'build',port/'tools/licenses',idf/'components'):
                directory.mkdir(parents=True,exist_ok=True)
            (port/'LICENSE').write_text('Copyright (c) 2026 Neil\nMIT')
            (idf/'LICENSE').write_text('Apache-2.0')
            (port/'tools/licenses/BUILTIN_FONTS.txt').write_text('Font notices')
            (port/'build.sh').write_text('# fixture')
            (port/'CMakeLists.txt').write_text('# fixture')
            for name in ('sdkconfig','sdkconfig.defaults','partitions.csv'):(port/name).write_text('# fixture')
            (port/'dependencies.lock').write_text('dependencies:\n  badgeteam/badge-bsp:\n    version: 2.0.0\n')
            (port/'build/project_description.json').write_text(json.dumps(dict(idf_path=str(idf),project_version='0.3.2')))
            for name in ('adsb_bringup.elf','adsb_bringup.map'):(port/'build'/name).write_bytes(b'fixture')
            image=port/'build/adsb_bringup.bin';image.write_bytes(b'\xe9'+bytes(255))
            icons=Path(tmp)/'icons';icons.mkdir()
            for size in (16,32,64):Image.new('RGB',(size,size)).save(icons/f'icon{size}.png')
            atlas=Path(tmp)/'atlas';atlas.mkdir()
            names=['world-v2.idx','world-v2-000.avp','cities-v1.idx','cities-v1.dat','DATA_NOTICES.txt']
            for name in names:(atlas/name).write_bytes(b'asset')
            manifest=dict(version=2,language='en',coverage=[-180,-90,180,90],files={n:dict(bytes=5,sha256=hashlib.sha256(b'asset').hexdigest(),crc32=zlib.crc32(b'asset')) for n in names})
            (atlas/'atlas-v2.json').write_text(json.dumps(manifest))
            output=Path(tmp)/'package';records=Path(tmp)/'records'
            with patch.object(package,'ROOT',port):
                record=package.assemble(image,output,SCHEMA,11,'0.3.2',icons,records,atlas)
                self.assertEqual(record['bsp_version'],2)
                self.assertEqual((output/'LICENSE').read_bytes(),(port/'LICENSE').read_bytes())
                self.assertEqual(json.loads((output/'metadata.json').read_text())['author'],'ADS-B Radar contributors; upstream Neil')
                self.assertIn('dependencies.lock',record['source_sha256'])
                self.assertIn('LICENSE',record['source_sha256'])
                self.assertFalse(any(Path(name).is_absolute() or '..' in Path(name).parts for name in record['source_sha256']))
                self.assertEqual(package.validate(output,SCHEMA,expected_bsp_major=2)['application'][0]['bsp_version'],2)
                metadata_path=output/'metadata.json'
                self.assertEqual(record['package_sha256']['metadata.json'],hashlib.sha256(metadata_path.read_bytes()).hexdigest())
                for incorrect in (1,None):
                    metadata=json.loads(metadata_path.read_text())
                    if incorrect is None:metadata['application'][0].pop('bsp_version',None)
                    else:metadata['application'][0]['bsp_version']=incorrect
                    metadata_path.write_text(json.dumps(metadata))
                    with self.assertRaisesRegex(ValueError,'BSP major'):
                        package.validate(output,SCHEMA,expected_bsp_major=2)
                with self.assertRaisesRegex(ValueError,'version does not match'):
                    package.assemble(image,output,SCHEMA,12,'0.3.1',icons,records,atlas)

    def test_atlas_assets_sd_requirement_and_integrity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'application.bin').write_bytes(b'\xe9'+bytes(255));(root/'LICENSE').write_text('MIT')
            Image.new('RGB',(16,16)).save(root/'icon16.png')
            names=['world-v2.idx','world-v2-000.avp','cities-v1.idx','cities-v1.dat','DATA_NOTICES.txt']
            for name in names:(root/name).write_bytes(b'Test asset')
            manifest=dict(version=2,language='en',coverage=[-180,-90,180,90],files={n:dict(bytes=(root/n).stat().st_size,sha256=hashlib.sha256((root/n).read_bytes()).hexdigest(),crc32=zlib.crc32((root/n).read_bytes())) for n in names})
            metadata=dict(name='ADS-B Radar',description='Test package',categories=['Tools'],version='0.3.0',icon={'16x16':'icon16.png'},author='Neil',license_type='MIT',license_file='LICENSE',application=[dict(targets=['tanmatsu'],type='appfs',revision=9,executable='application.bin',external_only=True,assets=[dict(source_file=n,target_file=n) for n in ['atlas-v2.json',*names]])])
            def write():
                (root/'metadata.json').write_text(json.dumps(metadata));(root/'atlas-v2.json').write_text(json.dumps(manifest))
            write();package.validate(root,SCHEMA)
            metadata['application'][0]['external_only']=False;write()
            with self.assertRaises(ValueError):package.validate(root,SCHEMA)
            metadata['application'][0]['external_only']=True;write()
            (root/'world-v2.idx').write_bytes(b'Corrupt')
            with self.assertRaises(ValueError):package.validate(root,SCHEMA)
            (root/'world-v2.idx').write_bytes(b'Test asset')
            metadata['application'][0]['assets'].pop();write()
            with self.assertRaises(ValueError):package.validate(root,SCHEMA)

    def test_missing_unsafe_and_invalid_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)
            metadata=dict(name='ADS-B Radar',description='Test package',categories=['Tools'],version='0.2.0',
                icon={'16x16':'icon16.png'},author='Neil',license_type='MIT',license_file='LICENSE',
                application=[dict(targets=['tanmatsu'],type='appfs',revision=2,executable='application.bin')])
            def write():(output/'metadata.json').write_text(json.dumps(metadata))
            write();(output/'LICENSE').write_text('MIT');(output/'application.bin').write_bytes(b'\xe9'+bytes(255))
            Image.new('RGB',(16,16)).save(output/'icon16.png')
            package.validate(output,SCHEMA)
            for name in ('icon16.png','LICENSE','application.bin'):
                path=output/name;data=path.read_bytes();path.unlink()
                with self.assertRaises(ValueError):package.validate(output,SCHEMA)
                path.write_bytes(data)
            metadata['application'][0]['executable']='../application.bin';write()
            with self.assertRaises(ValueError):package.validate(output,SCHEMA)
            metadata['application'][0]['executable']='application.bin'
            metadata['application'][0]['targets']=['konsool'];write()
            with self.assertRaises(ValueError):package.validate(output,SCHEMA)
            metadata['application'][0]['targets']=['tanmatsu'];write()
            Image.new('RGB',(32,32)).save(output/'icon16.png')
            with self.assertRaises(ValueError):package.validate(output,SCHEMA)
